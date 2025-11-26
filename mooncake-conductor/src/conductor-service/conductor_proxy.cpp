//  conductor_proxy.cpp
#include "conductor_proxy.h"
#include "conductor_utils.h"
#include "types.h"

#include <nlohmann/json.hpp>
#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <ranges>
#include <future>


namespace mooncake_conductor {

ConductorProxy::ConductorProxy(const Config& config)
    : config_(config),
      node_client_pools_(std::make_shared<ClientPools>()),
      stream_client_pools_(std::make_shared<ClientPools>()),
      http_server_(std::thread::hardware_concurrency(), config.listen_port) {
    using namespace coro_http;
    prefill_capable_nodes_.clear();
    decoding_capable_nodes_.clear();
    mixed_capable_nodes_.clear();

    http_server_.set_http_handler<GET, POST>(
        R"(/(.*))",
        [this](coro_http_request& req, coro_http_response& resp) 
            -> async_simple::coro::Lazy<void> {
            co_await handle_request(req, resp); // 全链路协程入口
        }
    );
}


ConductorProxy::~ConductorProxy() {
    if (running_) {
        this->stop();
    }
}

std::string ConductorProxy::generate_request_id() {
    return mooncake_conductor::uuid_to_str(mooncake::generate_uuid());
}

void ConductorProxy::register_node(std::string host, uint16_t port, NodeCapability capability) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    NodeCapability prev_cap = NodeCapability::NONE;
    std::shared_ptr<NodeInfo> new_node_ptr = nullptr;

    auto existing = std::ranges::find_if(registered_nodes_, 
        [&](const auto& node) { return node->host == host && node->port == port; });
    if (existing != registered_nodes_.end()) {
        prev_cap = (*existing)->get_capability();
        (*existing)->set_capability(capability);
        new_node_ptr = *existing;
    } else {
        new_node_ptr = std::make_shared<NodeInfo>(std::move(host), port, capability);
        registered_nodes_.push_back(new_node_ptr);
    }
    
    // TODO: 不再全量清空，仅做增量处理
    update_node_categories(new_node_ptr, prev_cap);
    update_client_pools(new_node_ptr, prev_cap);
    
    LOG(INFO) << "mixed_capable_nodes_ " << mixed_capable_nodes_.size() << " nodes";
    LOG(INFO) << "prefill_capable_nodes_ " << prefill_capable_nodes_.size() << " nodes";
    LOG(INFO) << "decoding_capable_nodes_ " << decoding_capable_nodes_.size() << " nodes";
    LOG(INFO)  << "Node " << host << ":" << port << "has been registed";
}


void ConductorProxy::update_client_pools(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    
    auto node_key = new_node_ptr->get_node_key();
    NodeCapability curr_cap = new_node_ptr->get_capability();
    
    if (prev_cap == curr_cap) {
        return;
    }
    
    // 配置参数
    coro_io::client_pool<HttpClient>::pool_config non_stream_pool_cfg;
    non_stream_pool_cfg.max_connection = config_.max_connections_per_host;
    non_stream_pool_cfg.idle_timeout = std::chrono::milliseconds(config_.connect_timeout_ms);
    non_stream_pool_cfg.connect_retry_count = config_.connect_retry_count;
    
    coro_io::client_pool<HttpClient>::pool_config stream_pool_cfg;
    stream_pool_cfg.max_connection = config_.max_connections_per_host / 2;
    stream_pool_cfg.idle_timeout = std::chrono::milliseconds(config_.stream_connect_timeout_ms);
    stream_pool_cfg.connect_retry_count = config_.connect_retry_count;
    
    // 分析之前和当前的能力需求
    bool prev_needs_stream = (prev_cap == NodeCapability::DECODING) || 
                            (prev_cap == NodeCapability::BOTH) ||
                            (config_.enable_mixed_deployment && prev_cap == NodeCapability::BOTH);
    
    bool curr_needs_stream = (curr_cap == NodeCapability::DECODING) || 
                            (curr_cap == NodeCapability::BOTH) ||
                            (config_.enable_mixed_deployment && curr_cap == NodeCapability::BOTH);
    
    // 非流式连接池：所有节点都需要，始终更新
    auto non_stream_pool = node_client_pools_->at(node_key, non_stream_pool_cfg);
    if (!non_stream_pool) {
        LOG(ERROR) << "Failed to update non-stream client pool for node: " << node_key;
    } else {
        LOG(INFO) << "Updated non-stream pool for " << node_key;
    }
    
    // 流式连接池：增量更新逻辑
    if (prev_needs_stream && !curr_needs_stream) {
        // 之前需要流式池，现在不需要：由于client_pools没有erase，我们无法移除
        // 连接池会保持但不再使用，依赖超时机制自动清理
        LOG(INFO) << "Node " << node_key << " no longer needs stream pool, keeping for timeout cleanup";
    } else if (!prev_needs_stream && curr_needs_stream) {
        // 之前不需要，现在需要：创建
        auto stream_pool = stream_client_pools_->at(node_key, stream_pool_cfg);
        if (!stream_pool) {
            LOG(ERROR) << "Failed to create stream client pool for node: " << node_key;
        } else {
            LOG(INFO) << "Created stream pool for " << node_key << " (capability changed)";
        }
    } else if (curr_needs_stream) {
        // 持续需要流式池：更新配置
        auto stream_pool = stream_client_pools_->at(node_key, stream_pool_cfg);
        if (!stream_pool) {
            LOG(ERROR) << "Failed to update stream client pool for node: " << node_key;
        } else {
            LOG(INFO) << "Refreshed stream pool for " << node_key;
        }
    }
}

void ConductorProxy::update_node_categories(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap) {
    // 注意：调用此函数前必须已持有 nodes_mutex_ 锁
    
    NodeCapability curr_cap = new_node_ptr->get_capability();
    if (prev_cap == curr_cap) return;

    auto remove_node = [&](auto& vec) {
        vec.erase(std::remove(vec.begin(), vec.end(), new_node_ptr), vec.end());
    };

    static const std::unordered_map<NodeCapability, std::vector<std::shared_ptr<NodeInfo>>*> cap_map = {
        {NodeCapability::PREFILL, &prefill_capable_nodes_},
        {NodeCapability::DECODING, &decoding_capable_nodes_},
        {NodeCapability::BOTH, &mixed_capable_nodes_}
    };

    if (prev_cap != NodeCapability::NONE) {
        if (auto it = cap_map.find(prev_cap); it != cap_map.end()) {
            remove_node(*(it->second));
        }
    }
    
    if (auto it = cap_map.find(curr_cap); it != cap_map.end()) {
        auto& vec = *(it->second);
        if (std::find(vec.begin(), vec.end(), new_node_ptr) == vec.end()) {
            vec.push_back(new_node_ptr);
        }
    }
}

bool ConductorProxy::start() {
    if (running_.exchange(true)) return false;
    
    if (registered_nodes_.empty()) {
        LOG(ERROR) << "Warning: No nodes registered.";
    }
    
    try {
        http_server_.async_start();
        LOG(INFO) << "Mooncake Conductor started on port " << config_.listen_port
                  << " with " << registered_nodes_.size() << " nodes";
        LOG(INFO) << " mixed capable nodes: " << mixed_capable_nodes_.size() << " nodes";
        LOG(INFO) << " prefill capable nodes: " << prefill_capable_nodes_.size() << " nodes";
        LOG(INFO) << " decoding capable nodes: " << decoding_capable_nodes_.size() << " nodes";
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "HTTP server start failed: " << e.what();
        running_.store(false);
        return false;
    }
}

void ConductorProxy::stop() {
    if (!running_.exchange(false)) return;
    http_server_.stop();
    LOG(INFO) << "Mooncake Conductor stopped" << std::endl;
}


bool ConductorProxy::is_stream_req(coro_http::coro_http_request& req) {
    return nlohmann::json::parse(req.get_body()).value("stream", false);
}

RequestCategory ConductorProxy::classify_request(coro_http::coro_http_request& req) {
    std::string_view path = req.get_url();
    std::string_view method = req.get_method();

    for (const auto& rule : kRouteRules) {
        if (method == rule.method && path.starts_with(rule.path_prefix)) {
            return rule.category;
        }
    }
    return RequestCategory::OTHERS;
}

async_simple::coro::Lazy<void> ConductorProxy::handle_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
    if (!running_.load(std::memory_order_acquire)) {
        resp.set_status_and_content(cinatra::status_type::service_unavailable, 
                                   "Service unavailable");
        co_return;
    }

    std::shared_ptr<NodeInfo> target_node;

    // 1. 分类请求
    auto category = classify_request(req);
    
    if (category == RequestCategory::INFERENCE) {
        // target_node = make_scheduling_decision(req); // TODO: 增加决策的逻辑，选择最佳prefill节点或混部节点
        target_node = mixed_capable_nodes_[0]; 
    } else {
        // 拒绝通过其它端点访问Conductor
        resp.set_status_and_content(cinatra::status_type::method_not_allowed, 
                                   "Non-inference endpoints are currently "
                                   "not supported for accessing Mooncake Conductor");
        co_return;
    } 
    
    if (!target_node) {
        resp.set_status_and_content(cinatra::status_type::internal_server_error, 
                                   "Failed to select target node");
        co_return;
    }
    
    co_await direct_forward(req, resp, target_node);
}

async_simple::coro::Lazy<void> ConductorProxy::direct_forward(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> node) 
{
    // 1. 快速校验前置条件
    if (!validate_request_method(req, resp)) {
        co_return;
    }

    // 2. 准备转发基础数据
    const auto [target_url, headers] = prepare_forward_context(req, node);
    LOG(INFO) << "Forwarding to " << target_url;

    try {
        // 3. 分发到具体处理逻辑
        if (req.get_method() == "GET") {
            co_await handle_get_forward(req, resp, node, target_url, headers);
        } 
        else if (req.get_method() == "POST") {
            if (is_stream_req(req)) {
                co_await handle_stream_post_forward(req, resp, node, target_url, headers);
            } else {
                co_await handle_normal_post_forward(req, resp, node, target_url, headers);
            }
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Forwarding exception: " << e.what();
        resp.set_status_and_content(
            coro_http::status_type::internal_server_error,
            "Internal forwarding error"
        );
    }
}

bool ConductorProxy::validate_request_method(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp) 
{
    const auto& method = req.get_method();
    if (method != "GET" && method != "POST") {
        resp.set_status_and_content(
            coro_http::status_type::method_not_allowed,
            "Method not supported by conductor"
        );
        return false;
    }
    return true;
}

std::pair<std::string, std::unordered_map<std::string, std::string>> 
ConductorProxy::prepare_forward_context(
    coro_http::coro_http_request& req,
    const std::shared_ptr<NodeInfo>& node) 
{
    auto target_url = "http://" + node->get_node_key() + std::string(req.get_url());
    auto headers = build_forward_headers(req.get_headers());
    headers["Connection"] = "keep-alive";
    return {std::move(target_url), std::move(headers)};
}

async_simple::coro::Lazy<void> ConductorProxy::handle_get_forward(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> node,
    std::string target_url,
    std::unordered_map<std::string, std::string> headers) 
{
    auto result = co_await node_client_pools_->send_request(
        node->get_node_key(),
        [&](coro_http::coro_http_client& client) -> async_simple::coro::Lazy<cinatra::resp_data> {
            co_return co_await client.async_get(std::move(target_url), std::move(headers));
        }
    );
    co_await forward_backend_response(resp, result, std::move(target_url));
}

async_simple::coro::Lazy<void> ConductorProxy::handle_normal_post_forward(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> node,
    std::string target_url,
    std::unordered_map<std::string, std::string> headers) 
{
    auto content = std::string(req.get_body());

    auto result = co_await node_client_pools_->send_request(
        node->get_node_key(),
        [&](coro_http::coro_http_client& client) -> async_simple::coro::Lazy<cinatra::resp_data> {
            co_return co_await client.async_post(
                std::move(target_url),
                std::move(content),
                cinatra::JSON,
                std::move(headers)
            );
        }
    );
    co_await forward_backend_response(resp, result, std::move(target_url));
}

async_simple::coro::Lazy<void> ConductorProxy::handle_stream_post_forward(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> node,
    std::string target_url,
    std::unordered_map<std::string, std::string> headers) 
{
    // SSE响应
    configure_sse_response(resp);

    // 回调处理器
    auto chunk_handler = create_chunk_handler(resp);

    co_await stream_client_pools_->send_request(
        node->get_node_key(),
        [&](coro_http::coro_http_client& client) -> async_simple::coro::Lazy<void> {
            if (!client.has_chunked_callback()) {
                client.set_chunked_callback(std::move(chunk_handler));
            }
            co_await client.async_post(
                std::move(target_url),
                std::string(req.get_body()),
                cinatra::JSON,
                std::move(headers)
            );
        }
    );
}

void ConductorProxy::configure_sse_response(coro_http::coro_http_response& resp) {
    resp.set_format_type(cinatra::format_type::chunked);
    resp.add_header("Content-Type", "text/event-stream");
    resp.add_header("Cache-Control", "no-cache");
    resp.add_header("Connection", "keep-alive");
}

std::unordered_map<std::string, std::string> 
ConductorProxy::build_forward_headers(const auto& original_headers) 
{
    std::unordered_map<std::string, std::string> headers;
    for (const auto& [k, v] : original_headers) {
        if (k != "host" && k != "connection" && k != "content-length") {
            headers.emplace(std::string(k), std::string(v));
        }
    }
    headers.emplace("Connection", "keep-alive");
    return headers;
}

std::function<async_simple::coro::Lazy<void>(std::string_view)> 
ConductorProxy::create_chunk_handler(coro_http::coro_http_response& resp) 
{
    bool has_begin = false;
    return [&resp, has_begin = std::move(has_begin)]
           (std::string_view data) mutable -> async_simple::coro::Lazy<void> {
        if (!has_begin) {
            has_begin = co_await resp.get_conn()->begin_chunked();
        }
        co_await resp.get_conn()->write_chunked(data);

        if (data.starts_with(SSE_END_TOKEN)) {
            co_await resp.get_conn()->end_chunked();
        }
    };
}

async_simple::coro::Lazy<void> ConductorProxy::forward_backend_response(
    coro_http::coro_http_response& resp,
    const tl::expected<cinatra::resp_data, std::errc>& result,
    std::string target_url) 
{
    if (!result) {
        handle_forward_error(resp, target_url, 
            "Forward request failed", 
            static_cast<int>(result.error()));
        co_return;
    }

    const auto& response_data = result.value();
    if (response_data.net_err) {
        handle_forward_error(resp, target_url,
            "Network error: " + response_data.net_err.message(),
            response_data.net_err.value());
        co_return;
    }

    if (response_data.status != 200) {
        LOG(WARNING) << "Non-200 response from " << target_url
                     << ", status: " << response_data.status;
    }

    resp.set_status_and_content(
        static_cast<coro_http::status_type>(response_data.status),
        std::string(response_data.resp_body)
    );

    LOG(INFO) << "Successfully forwarded response to client, status: "
              << response_data.status
              << ", body size: " << response_data.resp_body.size();
}

void ConductorProxy::handle_forward_error(
    coro_http::coro_http_response& resp,
    const std::string& target_url,
    const std::string& error_msg,
    int error_code) 
{
    LOG(ERROR) << "Forwarding error to " << target_url
               << " [code:" << error_code << "]: " << error_msg;
    resp.set_status_and_content(
        coro_http::status_type::bad_gateway,
        error_msg
    );
}


} // namespace mooncake_conductor