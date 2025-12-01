//  conductor_proxy.cpp
#include "conductor_proxy.h"
#include "conductor_utils.h"
#include "types.h"

#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <ranges>
#include <future>


namespace mooncake_conductor {

static std::function<async_simple::coro::Lazy<void>(std::string_view)> 
create_inline_chunk_handler(
    std::shared_ptr<decltype(std::declval<coro_http::coro_http_response>().get_conn())> conn_ptr)
{
    struct ChunkState {
        bool has_begin = false;
    };
    
    auto state = std::make_shared<ChunkState>();
    
    return [conn_ptr, state](std::string_view data) mutable -> async_simple::coro::Lazy<void> {
        if (!state->has_begin) {
            state->has_begin = co_await (*conn_ptr)->begin_chunked();
        }
        co_await (*conn_ptr)->write_chunked(data);

        if (data.starts_with(SSE_END_TOKEN)) {
            co_await (*conn_ptr)->end_chunked();
        }
    };
}

ConductorProxy::ConductorProxy(const Config& config)
    : config_(config),
      http_server_(std::thread::hardware_concurrency(), config.listen_port),
      api_key_header_([] {
          if (char* key = std::getenv("OPENAI_API_KEY")) {
              return std::string("Bearer ") + std::string(key);
          }
          return std::string("Bearer "); // 空key
      }())
{
    using namespace coro_http;


    non_stream_pool_cfg_.max_connection = config_.max_connections_per_host;
    non_stream_pool_cfg_.idle_timeout = std::chrono::milliseconds(config_.connect_timeout_ms);
    non_stream_pool_cfg_.connect_retry_count = config_.connect_retry_count;

    stream_pool_cfg_.max_connection = config_.max_connections_per_host / 2;
    stream_pool_cfg_.idle_timeout = std::chrono::milliseconds(config_.stream_connect_timeout_ms);
    stream_pool_cfg_.connect_retry_count = config_.connect_retry_count;

    non_stream_client_pools_ = std::make_shared<ClientPools>(non_stream_pool_cfg_);
    stream_client_pools_ = std::make_shared<ClientPools>(stream_pool_cfg_);

    prefill_capable_nodes_.clear();
    decoding_capable_nodes_.clear();
    mixed_capable_nodes_.clear();

    http_server_.set_http_handler<GET, POST>(
        R"(/(.*))",
        [this](coro_http_request& req, coro_http_response& resp) 
            -> async_simple::coro::Lazy<void> {
            static std::atomic<uint64_t> request_count{0};
            Timer timer{ "Request " + std::to_string(++request_count) + " "};
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
    
    {
        std::lock_guard<std::shared_mutex> lock(nodes_mutex_);
        
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
        update_node_categories(new_node_ptr, prev_cap);
        update_client_pools(new_node_ptr, prev_cap);
    }

    LOG(INFO) << "mixed_capable_nodes_ " << mixed_capable_nodes_.size() << " nodes";
    LOG(INFO) << "prefill_capable_nodes_ " << prefill_capable_nodes_.size() << " nodes";
    LOG(INFO) << "decoding_capable_nodes_ " << decoding_capable_nodes_.size() << " nodes";
    LOG(INFO)  << "Node " << host << ":" << port << "has been registed";
}


void ConductorProxy::update_client_pools(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap) {
    std::lock_guard<std::shared_mutex> lock(pools_mutex_);
    
    auto node_key = new_node_ptr->get_node_key();
    NodeCapability curr_cap = new_node_ptr->get_capability();
    
    if (prev_cap == curr_cap) {
        return;
    }
    
    // 分析之前和当前的能力需求
    bool prev_needs_stream = (prev_cap == NodeCapability::DECODING) || 
                            (prev_cap == NodeCapability::BOTH) ||
                            ((!config_.enable_pd_separation) && prev_cap == NodeCapability::BOTH);
    
    bool curr_needs_stream = (curr_cap == NodeCapability::DECODING) || 
                            (curr_cap == NodeCapability::BOTH) ||
                            ((!config_.enable_pd_separation) && curr_cap == NodeCapability::BOTH);
    
    // 非流式连接池：所有节点都需要，始终更新
    auto non_stream_pool = non_stream_client_pools_->at(node_key, non_stream_pool_cfg_);
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
        auto stream_pool = stream_client_pools_->at(node_key, stream_pool_cfg_);
        if (!stream_pool) {
            LOG(ERROR) << "Failed to create stream client pool for node: " << node_key;
        } else {
            LOG(INFO) << "Created stream pool for " << node_key << " (capability changed)";
        }
    } else if (curr_needs_stream) {
        // 持续需要流式池：更新配置
        auto stream_pool = stream_client_pools_->at(node_key, stream_pool_cfg_);
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
    LOG(INFO) << "Mooncake Conductor stopped";
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

    auto category = classify_request(req);
    
    if (category == RequestCategory::INFERENCE) {
        if (config_.enable_pd_separation) {
            co_await handle_pd_separation(req, resp);
            co_return;
        } else {
            auto target_node = select_mixed_node();
            if (!target_node) {
                resp.set_status_and_content(cinatra::status_type::internal_server_error, 
                                        "Failed to select target node:"
                                        " no nodes available");
                co_return;
            }

            co_await direct_forward(req, resp, target_node);
        }
    } else {
        // 拒绝通过其它端点访问Conductor
        resp.set_status_and_content(cinatra::status_type::method_not_allowed, 
                                   "Non-inference endpoints are currently "
                                   "not supported for accessing Mooncake Conductor");
    }
}

std::shared_ptr<NodeInfo> ConductorProxy::select_mixed_node() {
    std::lock_guard<std::shared_mutex> lock(nodes_mutex_);
    if (mixed_capable_nodes_.empty()) {
        LOG(ERROR) << "No mixed-capable nodes available";
        return nullptr;
    }
    // TODO: 后需通过计算策略选择最佳混合节点
    static size_t index = 0;
    auto node = mixed_capable_nodes_[index % mixed_capable_nodes_.size()];
    index++;
    return node;
}

/******************************************  PD分离相关：Begin ******************************************************/

std::shared_ptr<NodeInfo> ConductorProxy::select_prefill_node() {
    std::lock_guard<std::shared_mutex> lock(nodes_mutex_);
    if (prefill_capable_nodes_.empty()) {
        LOG(ERROR) << "No prefill-capable nodes available";
        return nullptr;
    }
    // TODO: 后需通过计算策略选择最佳P节点
    static size_t index = 0;
    auto node = prefill_capable_nodes_[index % prefill_capable_nodes_.size()];
    index++;
    return node;
}

std::shared_ptr<NodeInfo> ConductorProxy::select_decode_node() {
    std::lock_guard<std::shared_mutex> lock(nodes_mutex_);
    if (decoding_capable_nodes_.empty()) {
        LOG(ERROR) << "No decode-capable nodes available";
        return nullptr;
    }
    // TODO: 后需通过计算策略选择最佳D节点
    static size_t index = 0;
    auto node = decoding_capable_nodes_[index % decoding_capable_nodes_.size()];
    index++;
    return node;
}


std::string ConductorProxy::generate_pd_request_id(const std::string& prefill_addr, 
                                                 const std::string& decode_addr) {
    return prefill_addr + "___" + decode_addr + "___" + generate_request_id();
}

async_simple::coro::Lazy<void> ConductorProxy::handle_pd_separation(
    coro_http::coro_http_request& req, 
    coro_http::coro_http_response& resp) {
    
    LOG(INFO) << "Processing request in PD-separation mode, transfer_mode: " 
              << (config_.pd_transfer_mode == PDTransferMode::SYNC ? "SYNC" : "ASYNC");
    
    // 1. 选择Prefill和Decode节点
    auto prefill_node = select_prefill_node();
    auto decode_node = select_decode_node();
    
    if (!prefill_node || !decode_node) {
        resp.set_status_and_content(
            cinatra::status_type::internal_server_error,
            "No available prefill or decode nodes"
        );
        co_return;
    }
    
    // 2. 根据传输模式选择处理逻辑
    if (config_.pd_transfer_mode == PDTransferMode::SYNC) {
        co_await handle_sync_pd_transfer(req, resp, prefill_node, decode_node);
    } else {
        // co_await handle_async_pd_transfer(req, resp, prefill_node, decode_node);
        LOG(WARNING) << "Async PD transfer mode not implemented, using sync fallback...";
        co_await handle_sync_pd_transfer(req, resp, prefill_node, decode_node);
    }
}

async_simple::coro::Lazy<void> ConductorProxy::handle_sync_pd_transfer(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> prefill_node,
    std::shared_ptr<NodeInfo> decode_node) {

    try {

        std::string request_id = generate_pd_request_id(
            prefill_node->get_node_key(), 
            decode_node->get_node_key()
        );

        auto headers = generate_pd_separation_forward_headers(request_id);

        // 2. 使用现有的send_request接口转发到Prefill节点
        auto prefill_result = co_await send_prefill_request(prefill_node, req, headers);
        
        if (!prefill_result.has_value()) {
            LOG(ERROR) << "Prefill request failed";
            resp.set_status_and_content(
                cinatra::status_type::bad_gateway,
                "Prefill processing failed"
            );
            co_return;
        }
        
        // 3. 验证Prefill响应
        const auto& prefill_data = prefill_result.value();
        auto prefill_resp_status = static_cast<coro_http::status_type>(prefill_data.status);
        if (prefill_resp_status != cinatra::status_type::ok) {
            LOG(ERROR) << "Prefill response status: " 
                       << cinatra::to_http_status_string(prefill_resp_status);
            resp.set_status_and_content(
                prefill_resp_status,
                std::string(prefill_data.resp_body)
            );
            co_return;
        }
        // 5. 提取KV传输参数（如果存在）
        auto kv_transfer_params = extract_kv_transfer_params(prefill_data.resp_body);
        // 6. 准备并发送Decode请求
        co_await send_decode_request(req, resp, decode_node, headers, kv_transfer_params);

    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception in sync PD transfer: " << e.what();
        resp.set_status_and_content(
            cinatra::status_type::internal_server_error,
            "Internal PD transfer error"
        );
    }
}


async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>> 
ConductorProxy::send_prefill_request(
    std::shared_ptr<NodeInfo> prefill_node,
    coro_http::coro_http_request& original_req,
    const std::unordered_map<std::string, std::string>& headers) {
    try {
        // 构建目标URL和请求体
        auto addr = std::string(prefill_node->get_node_key());
        auto endpoint = std::string(original_req.get_url());
        auto prefill_req_body = generate_prefill_request_body(original_req);

        // 使用现有的非流式连接池接口发送请求
        co_return co_await handle_non_stream_post_forward(std::move(addr), std::move(endpoint),
                                                          std::move(prefill_req_body), headers);
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception in send_prefill_request: " << e.what();
        co_return tl::make_unexpected(std::errc::io_error);
    }
}


async_simple::coro::Lazy<void> ConductorProxy::send_decode_request(
    coro_http::coro_http_request& req,
    coro_http::coro_http_response& resp,
    std::shared_ptr<NodeInfo> decode_node,
    const std::unordered_map<std::string, std::string>& headers,
    const std::optional<nlohmann::json>& kv_params_opt) {
    
    try {
        // 生成Decode请求体
        std::string decode_body = generate_decode_request_body(req, kv_params_opt);
        auto addr = std::string(decode_node->get_node_key());
        auto endpoint = std::string(req.get_url());

        if (is_stream_req(req)) {
            co_await handle_stream_post_forward(resp, std::move(addr), std::move(endpoint), std::move(decode_body), headers);
        } else {
            auto result = co_await handle_non_stream_post_forward(std::move(addr), std::move(endpoint), std::move(decode_body), headers);
            co_await forward_backend_response(resp, result);
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception in execute_decode_stage: " << e.what();
        resp.set_status_and_content(
            cinatra::status_type::internal_server_error,
            "Decode stage error"
        );
    }
}

std::unordered_map<std::string, std::string> 
ConductorProxy::generate_pd_separation_forward_headers(const std::string& request_id) {
    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = api_key_header_;
    headers["X-Request-Id"] = request_id;
    headers["Connection"] = "keep-alive";
    return headers;
}

std::string ConductorProxy::generate_prefill_request_body(coro_http::coro_http_request& original_req) {
    try {
        nlohmann::json req_json = nlohmann::json::parse(original_req.get_body());

        req_json["max_tokens"] = 1;
        req_json["min_tokens"] = 1;
        req_json["stream"] = false;
        
        if (req_json.contains("stream_options")) {
            req_json.erase("stream_options");
        }

        req_json["kv_transfer_params"] = {
            {"do_remote_decode", true},
            {"do_remote_prefill", false}
        };
        
        return req_json.dump();
    } catch (const std::exception& e) {
        return std::string(original_req.get_body());
    }
}

std::string ConductorProxy::generate_decode_request_body(
    coro_http::coro_http_request& original_req,
    const std::optional<nlohmann::json>& prefill_kv_params = std::nullopt) 
{
    try {
        nlohmann::json req_json = nlohmann::json::parse(original_req.get_body());
        if (prefill_kv_params) {
            req_json["kv_transfer_params"] = *prefill_kv_params;
        }
        return req_json.dump();
    } catch (const std::exception& e) {
        return std::string(original_req.get_body());
    }
}


std::optional<nlohmann::json> ConductorProxy::extract_kv_transfer_params(
    const std::string_view response_body) {
    
    try {
        if (response_body.empty()) {
            LOG(WARNING) << "Empty prefill response body";
            return std::nullopt;
        }

        nlohmann::json response_json;
        try {
            response_json = nlohmann::json::parse(response_body);
        } catch (const nlohmann::json::parse_error& e) {
            LOG(ERROR) << "Failed to parse prefill response JSON: " << e.what();
            return std::nullopt;
        }
        
        if (response_json.contains("kv_transfer_params")) {
            auto kv_params = response_json["kv_transfer_params"];
            if (!kv_params.is_object()) {
                LOG(WARNING) << "kv_transfer_params exists but is not a valid object: " 
                             << kv_params.type_name();
                return std::nullopt;
            }
            return kv_params;
        } else {
            LOG(WARNING) << "No kv_transfer_params found in prefill response";
            return std::nullopt;
        }
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while extracting KV transfer params: " << e.what();
        return std::nullopt;
    }
}

/******************************************  PD分离相关：END ******************************************************/


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
    const auto headers = build_forward_headers(req.get_headers());
    auto addr = std::string(node->get_node_key());
    auto endpoint = std::string(req.get_url());
    auto body = std::string(req.get_body());

    try {
        // 3. 分发到具体处理逻辑
        if (req.get_method() == "GET") {
            auto result = co_await handle_get_forward(std::move(addr), std::move(endpoint), std::move(headers));
            co_await forward_backend_response(resp, result);
        } 
        else if (req.get_method() == "POST") {
            if (is_stream_req(req)) {
                co_await handle_stream_post_forward(resp, std::move(addr), std::move(endpoint), std::move(body), std::move(headers));
            } else {
                auto result = co_await handle_non_stream_post_forward(std::move(addr), std::move(endpoint), std::move(body), std::move(headers));
                co_await forward_backend_response(resp, result);
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

async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>>
ConductorProxy::handle_get_forward(
    std::string addr,
    std::string endpoint,
    std::unordered_map<std::string, std::string> headers) 
{
    co_return co_await non_stream_client_pools_->send_request(
        std::string(addr),
        [addr = std::move(addr), endpoint = std::move(endpoint), headers = std::move(headers)]
        (coro_http::coro_http_client& client) -> async_simple::coro::Lazy<cinatra::resp_data> {
            auto uri = "http://" + std::move(addr) + std::move(endpoint);
            co_return co_await client.async_get(std::move(uri), std::move(headers));
        }
    );
}

async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>>
ConductorProxy::handle_non_stream_post_forward(
    std::string addr,
    std::string endpoint,
    std::string body,
    std::unordered_map<std::string, std::string> headers)
{
    co_return co_await non_stream_client_pools_->send_request(
        std::string(addr),
        [addr = std::move(addr), endpoint = std::move(endpoint), 
         body = std::move(body), headers = std::move(headers)]
        (coro_http::coro_http_client& client) mutable -> async_simple::coro::Lazy<cinatra::resp_data> {
            auto uri = "http://" + std::move(addr) + std::move(endpoint);
            co_return co_await client.async_post(
                std::move(uri),
                std::move(body),
                cinatra::JSON,
                std::move(headers)
            );
        }
    );
}

async_simple::coro::Lazy<void> ConductorProxy::handle_stream_post_forward(
    coro_http::coro_http_response& resp,
    std::string addr,
    std::string endpoint,
    std::string body,
    std::unordered_map<std::string, std::string> headers)
{
    // SSE响应
    configure_sse_response(resp);

    // 连接声明周期
    auto conn = resp.get_conn();
    auto conn_ptr = std::make_shared<decltype(conn)>(std::move(conn));

    co_await stream_client_pools_->send_request(
        std::string(addr),
        [addr = std::move(addr), endpoint = std::move(endpoint), 
         body = std::move(body), headers = std::move(headers),
         conn_ptr]
        (coro_http::coro_http_client& client) mutable -> async_simple::coro::Lazy<void> {
            if (!client.has_chunked_callback()) {
                client.set_chunked_callback(create_inline_chunk_handler(conn_ptr));
            }
            auto uri = "http://" + std::move(addr) + std::move(endpoint);
            co_await client.async_post(
                std::move(uri),
                std::move(body),
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
    headers.reserve(original_headers.size() + 1);  // +1 为Connection头预留
    
    for (const auto& [k, v] : original_headers) {
        std::string_view key_sv(k);
        std::string_view value_sv(v);
        if (key_sv != "host" && key_sv != "connection" && key_sv != "content-length") {
            headers.emplace(key_sv, value_sv);
        }
    }
    
    headers.try_emplace("Connection", "keep-alive");
    return headers;
}

async_simple::coro::Lazy<void> ConductorProxy::forward_backend_response(
    coro_http::coro_http_response& resp,
    const tl::expected<cinatra::resp_data, std::errc>& result) 
{
    if (!result) {
        handle_forward_error(resp, "Forward request failed", 
                                static_cast<int>(result.error()));
        co_return;
    }

    const auto& response_data = result.value();
    if (response_data.net_err) {
        handle_forward_error(resp, "Network error: " + response_data.net_err.message(),
            response_data.net_err.value());
        co_return;
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
    const std::string& error_msg,
    int error_code) 
{
    resp.set_status_and_content(
        coro_http::status_type::bad_gateway,
        error_msg
    );
}
} // namespace mooncake_conductor