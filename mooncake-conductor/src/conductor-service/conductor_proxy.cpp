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
    
    // 注册推理端点
    http_server_.set_http_handler<POST>(
        "/v1/completions",
        [&](coro_http_request& req, coro_http_response& resp) mutable {
            async_simple::coro::syncAwait(
                [this, &req, &resp]() -> async_simple::coro::Lazy<void> {
                    co_await handle_inference_request(req, resp);
                }()
            );
        },
        coro_http::json
    );

    http_server_.set_http_handler<POST>(
        "/v1/chat/completions",
        [&](coro_http_request& req, coro_http_response& resp) mutable {
            async_simple::coro::syncAwait(
                [this, &req, &resp]() -> async_simple::coro::Lazy<void> {
                    co_await handle_inference_request(req, resp);
                }()
            );
        },
        coro_http::json
    );

    http_server_.set_http_handler<GET, POST>(
        R"(/(.*))",
        [this](coro_http_request& req, coro_http_response& resp) {
            handle_other_request(req, resp);
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




// ***************************************** New **********************************************//

RequestCategory ConductorProxy::classify_request(coro_http::coro_http_request& req) {

    std::string path = std::string(req.get_url());
    std::string method = std::string(req.get_method());
    
    if ((path.starts_with("/v1/chat/completions") || path.starts_with("/v1/completions")) &&
        method == "POST") {
        return RequestCategory::INFERENCE;
    }
    
    return RequestCategory::OTHERS;
}


void ConductorProxy::handle_other_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
    if (!running_.load(std::memory_order_acquire)) {
        resp.set_status_and_content(cinatra::status_type::service_unavailable, 
                                   "Service unavailable");
        return;
    }
    
    LOG(INFO) << "Beginning to get types...";

    // 1. 分类请求
    auto category = classify_request(req);
    
    if (category == RequestCategory::INFERENCE) {
        async_simple::coro::syncAwait(
            [this, &req, &resp]() -> async_simple::coro::Lazy<void> {
                co_await handle_inference_request(req, resp);
            }()
        );
        return;
    }
    
    
    // 2. 随机选择节点
    std::shared_ptr<NodeInfo> target_node = prefill_capable_nodes_[0];
    
    if (!target_node) {
        resp.set_status_and_content(cinatra::status_type::internal_server_error, 
                                   "Failed to select target node");
        return;
    }
    
    // 3. 直接转发请求
    direct_forward(req, resp, target_node);
    
    
    // // 2. 广播到所有节点
    // std::lock_guard<std::mutex> lock(nodes_mutex_);
    
    // if (registered_nodes_.empty()) {
    //     resp.set_status_and_content(cinatra::status_type::service_unavailable, 
    //                                "No nodes available");
    //     return;
    // }
    
    // // 3. 获取原始请求路径
    // std::string req_path = std::string(req.get_url());

    
    
    // // 4. 广播请求
    // auto responses = broadcast_request_to_nodes(req, registered_nodes_);
    
    // // 5. 聚合响应
    // auto aggregated_response = aggregate_responses(req_path, responses);
    
    // // 6. 设置响应
    // resp.set_status(cinatra::status_type::ok);
    
    // if (req_path == "/metrics") {
    //     resp.add_header("Content-Type", "text/plain; version=0.0.4");
    // } else {
    //     resp.add_header("Content-Type", "application/json");
    // }
    
    // resp.set_content(aggregated_response);
}


void ConductorProxy::direct_forward(coro_http::coro_http_request& req, 
                                   coro_http::coro_http_response& resp, 
                                   std::shared_ptr<NodeInfo> node) {
    auto node_key = node->get_node_key();
    std::string target_url = "http://" + node_key + std::string(req.get_url());
    LOG(INFO) << "Directly send to " << target_url;
    
    // 构建请求
    std::unordered_map<std::string, std::string> headers;
    for (const auto& [k, v] : req.get_headers()) {
        if (k != "host" && k != "connection" && k != "content-length") {
            headers.emplace(std::string(k), std::string(v));
        }
    }
    headers.emplace("Connection", "keep-alive");
    
    auto method = req.get_method() == "GET" ? 
        cinatra::http_method::GET : cinatra::http_method::POST;
    
    auto result = async_simple::coro::syncAwait(
        [&]() -> async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>> {
            auto ret = co_await node_client_pools_->send_request(
                node_key,
                [&](coro_http::coro_http_client& client) {
                    return client.async_get(std::move(target_url), std::move(headers));
                });
            co_return ret;
        }()
    );
    
    // 将结果直接转发返还给真实的Client

    if (!result) {
        LOG(ERROR) << "Forward request failed to " << target_url 
                   << ", error: " << static_cast<int>(result.error());
        resp.set_status_and_content(coro_http::status_type::bad_gateway, 
                                   "Forward request failed");
        return;
    }
    
    auto& response_data = result.value();
    if (response_data.net_err) {
        LOG(ERROR) << "Network error to " << target_url 
                   << ", error: " << response_data.net_err.message();
        resp.set_status_and_content(coro_http::status_type::bad_gateway,
                                   "Network error: " + response_data.net_err.message());
        return;
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


// std::vector<std::pair<std::string, std::string>> ConductorProxy::broadcast_request_to_nodes(
//     coro_http::coro_http_request& req, 
//     const std::vector<std::shared_ptr<NodeInfo>>& nodes) {
    
//     std::vector<std::pair<std::string, std::string>> results;
//     results.reserve(nodes.size());
    
//     // 1. 配置超时
//     auto management_timeout = std::chrono::milliseconds(config_.connect_timeout_ms);
//     auto global_timeout = std::chrono::milliseconds(config_.connect_timeout_ms * 2);
//     auto start_time = std::chrono::steady_clock::now();
    
//     // 2. 并行发送请求
//     std::vector<std::future<std::pair<std::string, std::string>>> futures;
//     futures.reserve(nodes.size());
    
//     for (auto& node : nodes) {
//         auto node_key = node->get_node_key();
//         futures.emplace_back(std::async(std::launch::async, [this, &req, node_key, node, management_timeout]() {
//             try {
//                 // 获取client
//                 std::lock_guard<std::mutex> lock(this->pools_mutex_);
//                 auto pool = this->node_client_pools_->at(node_key);
//                 if (!pool) {
//                     return std::make_pair(node_key, "{\"error\": \"Node pool not found\"}");
//                 }
                
//                 auto [client, status] = async_simple::coro::syncAwait(
//                     pool->get_client(management_timeout)
//                 );
                
//                 if (status != coro_io::client_pool_status::ok || !client) {
//                     return std::make_pair(node_key, "{\"error\": \"Failed to get client\"}");
//                 }
                
//                 // 构建请求
//                 std::string target_url = "http://" + node_key + std::string(req.get_url());
//                 coro_http::coro_http_request fwd_req;
//                 fwd_req.method = req.get_method() == "GET" ? 
//                     http_method::GET : http_method::POST;
//                 fwd_req.url = target_url;
//                 fwd_req.body = req.get_body();
                
//                 // 复制headers
//                 for (const auto& header : req.get_headers()) {
//                     std::string key(header.name);
//                     if (key != "host" && key != "connection" && key != "content-length") {
//                         fwd_req.headers.emplace_back(header.name, header.value);
//                     }
//                 }
//                 fwd_req.headers.emplace_back("Connection", "keep-alive");
//                 fwd_req.headers.emplace_back("x-conductor-broadcast", "true");
                
//                 // 发送请求
//                 auto result = async_simple::coro::syncAwait(
//                     client->async_request(fwd_req)
//                 );
                
//                 return std::make_pair(node_key, result.resp.body);
//             } catch (const std::exception& e) {
//                 return std::make_pair(node_key, "{\"error\": \"" + std::string(e.what()) + "\"}");
//             }
//         }));
//     }
    
//     // 3. 收集结果（带超时）
//     for (size_t i = 0; i < futures.size(); ++i) {
//         auto& future = futures[i];
//         auto status = future.wait_for(std::chrono::milliseconds(100));
        
//         if (status == std::future_status::ready) {
//             results.push_back(future.get());
//         } else {
//             results.push_back(std::make_pair("node_" + std::to_string(i), 
//                 "{\"error\": \"Request timeout\"}"));
//         }
        
//         // 检查全局超时
//         auto elapsed = std::chrono::steady_clock::now() - start_time;
//         if (elapsed > global_timeout) {
//             // 剩余请求标记为超时
//             for (size_t j = i + 1; j < futures.size(); ++j) {
//                 results.push_back(std::make_pair("node_" + std::to_string(j), 
//                     "{\"error\": \"Global timeout\"}"));
//             }
//             break;
//         }
//     }
    
//     return results;
// }


// std::vector<std::pair<std::string, std::string>> ConductorProxy::broadcast_request_to_nodes(
//     coro_http::coro_http_request& req, 
//     const std::vector<std::shared_ptr<NodeInfo>>& nodes) {
    
//     std::vector<std::pair<std::string, std::string>> results;
//     results.reserve(nodes.size());
    
//     // 1. 配置超时
//     auto timeout = std::chrono::milliseconds(config_.connect_timeout_ms);
    
//     for (auto& node : nodes) {
//         auto node_key = node->get_node_key();
//         std::string target_url = "http://" + node_key + std::string(req.get_url());
        
//         try {
//             // 2. 获取client（简化版）
//             std::lock_guard<std::mutex> lock(pools_mutex_);
//             auto pool = node_client_pools_->at(node_key);
//             if (!pool) {
//                 results.emplace_back(node_key, "{\"error\": \"Node pool not found\"}");
//                 continue;
//             }
            
//             auto [client, status] = async_simple::coro::syncAwait(
//                 pool->get_client(timeout)
//             );
            
//             if (status != coro_io::client_pool_status::ok || !client) {
//                 results.emplace_back(node_key, "{\"error\": \"Failed to get client\"}");
//                 continue;
//             }
            
//             // 3. 构建请求（直接复用原始请求）
//             coro_http::coro_http_request fwd_req;
//             fwd_req.method = req.get_method() == "GET" ? 
//                 http_method::GET : http_method::POST;
//             fwd_req.url = target_url;
//             fwd_req.body = req.get_body();
            
//             // 4. 复制headers（只过滤必要字段）
//             for (const auto& header : req.get_headers()) {
//                 std::string key(header.name);
//                 if (key != "host" && key != "connection" && key != "content-length") {
//                     fwd_req.headers.emplace_back(header.name, header.value);
//                 }
//             }
//             fwd_req.headers.emplace_back("Connection", "keep-alive");
//             fwd_req.headers.emplace_back("x-conductor-broadcast", "true");
            
//             // 5. 同步发送请求
//             auto result = async_simple::coro::syncAwait(
//                 client->async_request(fwd_req)
//             );
            
//             results.emplace_back(node_key, result.resp.body);
            
//         } catch (const std::exception& e) {
//             results.emplace_back(node_key, "{\"error\": \"" + std::string(e.what()) + "\"}");
//         }
//     }
    
//     return results;
// }


// std::string ConductorProxy::aggregate_responses(
//     const std::string& path,
//     const std::vector<std::pair<std::string, std::string>>& responses) {
    
//     // 1. 健康检查聚合
//     if (path == "/health" || path == "/status") {
//         boost::json::object result;
//         result["conductor_status"] = "ok";
//         result["total_nodes"] = static_cast<int64_t>(responses.size());
//         result["healthy_nodes"] = 0;
        
//         boost::json::array nodes;
        
//         for (const auto& [node_key, response] : responses) {
//             boost::json::object node_info;
//             node_info["node"] = node_key;
            
//             try {
//                 auto json = boost::json::parse(response);
//                 if (json.is_object()) {
//                     auto& obj = json.as_object();
//                     if (obj.contains("status") && obj["status"].is_string() && 
//                         obj["status"].as_string() == "ok") {
//                         node_info["status"] = "ok";
//                         result["healthy_nodes"] = result["healthy_nodes"].as_int64() + 1;
//                     } else {
//                         node_info["status"] = "unhealthy";
//                     }
//                 } else {
//                     node_info["status"] = "unhealthy";
//                 }
//             } catch (...) {
//                 node_info["status"] = "error";
//                 node_info["error"] = "Failed to parse response";
//             }
            
//             nodes.push_back(node_info);
//         }
        
//         result["nodes"] = nodes;
//         result["cluster_status"] = (result["healthy_nodes"].as_int64() == responses.size()) ? 
//             "ok" : "degraded";
        
//         return boost::json::serialize(result);
//     }
    
//     // 2. 指标聚合
//     if (path == "/metrics") {
//         std::string result = "# Mooncake Conductor Global Metrics\n";
//         result += "conductor_nodes_total " + std::to_string(responses.size()) + "\n";
        
//         for (const auto& [node_key, response] : responses) {
//             result += "# Node: " + node_key + "\n";
//             result += response + "\n";
//         }
        
//         return result;
//     }
    
//     // 3. 默认：返回原始响应数组
//     boost::json::array result;
//     for (const auto& [node_key, response] : responses) {
//         boost::json::object item;
//         item["node"] = node_key;
//         try {
//             item["response"] = boost::json::parse(response);
//         } catch (...) {
//             item["response"] = response;
//         }
//         result.push_back(item);
//     }
    
//     return boost::json::serialize(result);
// }

// async_simple::coro::Lazy<void> ConductorProxy::handle_inference_request(
//     coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
    
//     if (!running_.load(std::memory_order_acquire)) {
//         resp.set_status_and_content(cinatra::status_type::service_unavailable, 
//                                    "Service unavailable");
//         co_return;
//     }
    
//     auto ctx = parse_request_context(req);
//     auto decision = make_scheduling_decision(ctx);
    
//     if (ctx.is_stream) {
//         // ✅ 流式处理
//         try {
//             resp.set_status(cinatra::status_type::ok);
//             resp.add_header("Content-Type", "text/event-stream");
//             resp.add_header("Cache-Control", "no-cache");
//             resp.add_header("Connection", "keep-alive");
//             resp.add_header("Transfer-Encoding", "chunked");
//             resp.add_header("X-Request-Id", ctx.request_id);
//             resp.add_header("X-Accel-Buffering", "no");
//             resp.set_delay(true);
            
//             // 获取流式生成器
//             auto generator = co_await handle_stream_inference(ctx, decision.decode_node);
            
//             // 遍历生成器
//             while (auto chunk = co_await generator.next()) {
//                 if (chunk) {
//                     resp.write_data(std::move(chunk.value()));
//                 }
//             }
            
            
//         } catch (const std::exception& e) {
//             resp.set_status_and_content(cinatra::status_type::internal_server_error,
//                                        "Streaming error: " + std::string(e.what()));
//         }
//     } else {
//         // ✅ 非流式处理（简化版）
//         co_await handle_non_stream_inference(req, resp, decision);
//     }
// }





RequestContext ConductorProxy::parse_request_context(coro_http::coro_http_request& req) {
    RequestContext ctx;
    ctx.request_id = generate_request_id();
    ctx.path = std::string(req.get_url());
    ctx.method = std::string(req.get_method());
    ctx.raw_body = std::string(req.get_body());
    
    for (const auto& header : req.get_headers()) {
        ctx.headers.emplace_back(std::string(header.name), std::string(header.value));
    }

    // TODO: 集成endpoint adapter解析流式标志
    // 临时：简单解析JSON body
    try {
        ctx.is_stream = nlohmann::json::parse(ctx.raw_body).value("stream", false);
    } catch (...) {
        // 忽略解析错误
    }
    
    return ctx;
}

// SchedulingDecision ConductorProxy::make_scheduling_decision(const RequestContext& ctx) {
//     SchedulingDecision decision;
//     decision.is_stream = ctx.is_stream;
    
//     // TODO: 集成MooncakeStoreCommunicationLayer查询KVCache分布
//     // TODO: 实现完整的调度策略
    
//     std::lock_guard<std::mutex> lock(nodes_mutex_);
    
//     // 临时降级逻辑：选择第一个可用节点
//     if (!decoding_capable_nodes_.empty()) {
//         decision.decode_node = decoding_capable_nodes_.front();
//     } else if (config_.enable_mixed_deployment && !mixed_capable_nodes_.empty()) {
//         decision.decode_node = mixed_capable_nodes_.front();
//     }
    
//     return decision;
// }

// async_simple::coro::Lazy<StreamChunkGenerator> ConductorProxy::handle_stream_inference(
//     const RequestContext& ctx, 
//     const std::shared_ptr<NodeInfo>& decode_node) {
    
//     if (!decode_node) {
//         // 错误处理Generator
//         co_return StreamChunkGenerator(
//             []() -> StreamChunkGenerator {
//                 co_yield std::string(R"({"error": "No decode node available"})");
//                 co_return;
//             }()
//         );
//     }
    
//     try {
//         std::string target_url = "http://" + decode_node->get_node_key() + ctx.path;
        
//         // 从decode节点流式获取响应
//         co_return co_await stream_from_decode_node(
//             decode_node, target_url, ctx, ctx.request_id);
        
//     } catch (const std::exception& e) {
//         // 错误处理Generator
//         co_return StreamChunkGenerator(
//             [error_msg = std::string(e.what())]() -> StreamChunkGenerator {
//                 co_yield std::string("{\"error\": \"" + error_msg + "\"}");
//                 co_return;
//             }()
//         );
//     }
// }

// async_simple::coro::Lazy<StreamChunkGenerator> ConductorProxy::stream_from_decode_node(
//     const std::shared_ptr<NodeInfo>& node,
//     const std::string& target_url,
//     const RequestContext& ctx,
//     const std::string& request_id) {
    
//     auto node_key = node->get_node_key();
//     std::lock_guard<std::mutex> lock(pools_mutex_);
    
//     auto pool_it = stream_client_pools_.find(node_key);
//     if (pool_it == stream_client_pools_.end()) {
//         pool_it = node_client_pools_.find(node_key);
//     }
    
//     if (pool_it == node_client_pools_.end()) {
//         throw std::runtime_error("Stream pool not found for node: " + node_key);
//     }
    
//     // 获取client
//     auto [client, status] = co_await pool_it->second->get(
//         node_key, 
//         std::chrono::milliseconds(config_.stream_connect_timeout_ms)
//     );
    
//     if (status != ylt::coro_io::client_pool_status::ok) {
//         throw std::runtime_error("Failed to get stream client from pool");
//     }
    
//     // 构建headers
//     std::vector<std::pair<std::string, std::string>> headers;
//     headers.reserve(ctx.headers.size() + 3);
    
//     for (const auto& [k, v] : ctx.headers) {
//         if (k != "host" && k != "connection" && k != "content-length") {
//             headers.emplace_back(k, v);
//         }
//     }
    
//     headers.emplace_back("X-Request-Id", request_id);
//     headers.emplace_back("Connection", "keep-alive");
//     headers.emplace_back("Accept", "text/event-stream");
    
//     try {
//         // 直接使用async_stream API，不构造coro_http_request
//         auto response = co_await client->async_stream(
//             target_url,
//             cinatra::http_method::POST,
//             ctx.raw_body,
//             std::move(headers)
//         );
        
//         if (response.status != 200) {
//             auto error_body = co_await response.body();
//             pool_it->second->return_client(node_key, std::move(client));
//             throw std::runtime_error("Stream request failed with status: " + 
//                                    std::to_string(response.status) + ", body: " + error_body);
//         }
        
//         // 正确构造Generator协程
//         co_return StreamChunkGenerator(
//             [response = std::move(response), 
//              pool = pool_it->second.get(), 
//              node_key, 
//              client = std::move(client)]() mutable -> StreamChunkGenerator {
                
//                 // 开始标记
//                 co_yield std::string(" {\"status\": \"stream_started\"}\n\n");
                
//                 try {
//                     while (true) {
//                         auto chunk = co_await response.next_chunk();
//                         if (!chunk || chunk->empty()) {
//                             co_yield std::string(" [DONE]\n\n");
//                             break;
//                         }
                        
//                         // 格式化SSE数据
//                         std::string formatted = " " + std::move(chunk.value()) + "\n\n";
//                         co_yield std::move(formatted);
//                     }
//                 } catch (const std::exception& e) {
//                     // 错误处理
//                     co_yield std::string(" {\"error\": \"" + std::string(e.what()) + "\"}\n\n");
//                     co_yield std::string(" [DONE]\n\n");
//                 }
                
//                 // 资源清理
//                 if (pool) {
//                     pool->return_client(node_key, std::move(const_cast<std::unique_ptr<HttpClient>&>(client)));
//                 }
                
//                 co_return;
//             }()
//         );
        
//     } catch (...) {
//         // 异常安全
//         if (client) {
//             pool_it->second->return_client(node_key, std::move(client));
//         }
//         throw;
//     }
// }


async_simple::coro::Lazy<void> ConductorProxy::handle_inference_request(
    coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
    
    if (!running_.load(std::memory_order_acquire)) {
        resp.set_status_and_content(cinatra::status_type::service_unavailable, 
                                   "Service unavailable");
        co_return;
    }
    
    auto ctx = parse_request_context(req);
    // auto decision = make_scheduling_decision(ctx);
    
    if (ctx.is_stream) {
        try {
            // // 设置流式响应头
            // resp.set_status(cinatra::status_type::ok);
            // resp.add_header("Content-Type", "text/event-stream");
            // resp.add_header("Cache-Control", "no-cache");
            // resp.add_header("Connection", "keep-alive");
            // resp.add_header("Transfer-Encoding", "chunked");
            // resp.add_header("X-Request-Id", ctx.request_id);
            // resp.add_header("X-Accel-Buffering", "no");
            // resp.set_delay(true);
            
            // // 获取流式生成器
            // auto generator = co_await handle_stream_inference(ctx, decision.decode_node);
            
            // // 遍历生成器，逐块写入响应
            // while (auto chunk = co_await generator.next()) {
            //     if (chunk) {
            //         resp.write_data(std::move(chunk.value()));
            //     }
            // }
            
            // resp.end();

            resp.set_status_and_content(cinatra::status_type::ok, 
                                   "{\"error\": \"Stream handling not implemented\"}");
        } catch (const std::exception& e) {
            resp.set_status_and_content(cinatra::status_type::internal_server_error,
                                       "Streaming error: " + std::string(e.what()));
        }
    } else {
        // TODO: 非流式处理
        // co_await handle_non_stream_inference(req, resp, decision);
        resp.set_status_and_content(cinatra::status_type::ok, 
                                   "{\"error\": \"Non-stream handling not implemented\"}");
    }
}


// void ConductorProxy::handle_management_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
//     std::lock_guard<std::mutex> lock(nodes_mutex_);
//     if (registered_nodes_.empty()) {
//         resp.set_status_and_content(cinatra::status_type::service_unavailable, 
//                                    "No nodes available");
//         return;
//     }
    
//     static size_t index = 0;
//     auto node = registered_nodes_[index % registered_nodes_.size()];
//     index++;
    
//     // 简单的管理请求转发
//     auto node_key = node->get_node_key();
//     std::lock_guard<std::mutex> pools_lock(pools_mutex_);
    
//     auto pool_it = node_client_pools_.find(node_key);
//     if (pool_it == node_client_pools_.end()) {
//         resp.set_status_and_content(cinatra::status_type::bad_gateway, 
//                                    "Node pool not found");
//         return;
//     }
    
//     auto [client, status] = async_simple::coro::syncAwait(
//         pool_it->second->get_client(
//             node_key, 
//             std::chrono::milliseconds(config_.connect_timeout_ms)
//         )
//     );
    
//     if (status != ylt::coro_io::client_pool_status::ok) {
//         resp.set_status_and_content(cinatra::status_type::service_unavailable, 
//                                    "Failed to get client");
//         return;
//     }
    
//     std::string target_url = "http://" + node_key + std::string(req.get_url());
    
//     // 构建请求
//     std::vector<std::pair<std::string, std::string>> headers;
//     for (const auto& [k, v] : req.get_headers()) {
//         if (k != "host" && k != "connection" && k != "content-length") {
//             headers.emplace_back(std::string(k), std::string(v));
//         }
//     }
//     headers.emplace_back("Connection", "keep-alive");
    
//     auto method = req.get_method() == "GET" ? 
//         cinatra::http_method::GET : cinatra::http_method::POST;
    
//     auto result = async_simple::coro::syncAwait(
//         client->async_request(
//             target_url,
//             method,
//             req.get_body(),
//             std::move(headers)
//         )
//     );
    
//     // 归还client
//     pool_it->second->return_client(node_key, std::move(client));
    
//     resp.set_status(static_cast<cinatra::status_type>(result.status));
//     for (const auto& [k, v] : result.headers) {
//         std::string key = std::string(k);
//         if (key != "connection" && key != "content-length" && key != "transfer-encoding") {
//             resp.add_header(key, std::string(v));
//         }
//     }
//     resp.set_content(result.body);
// }

// void ConductorProxy::handle_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
//     if (!running_.load(std::memory_order_acquire)) {
//         resp.set_status_and_content(cinatra::status_type::service_unavailable, 
//                                    "Service unavailable");
//         return;
//     }
    
//     auto category = classify_request(req);
//     switch (category) {
//         case RequestCategory::MANAGEMENT:
//             handle_management_request(req, resp);
//             break;
//         case RequestCategory::INFERENCE:
//             async_simple::coro::syncAwait(
//                 [this, &req, &resp]() -> async_simple::coro::Lazy<void> {
//                     co_await handle_inference_request(req, resp);
//                 }()
//             );
//             break;
//         case RequestCategory::UNKNOWN:
//             if (req.get_method() == "GET") {
//                 handle_management_request(req, resp);
//             } else {
//                 resp.set_status_and_content(cinatra::status_type::bad_request,
//                                            "Unsupported request type");
//             }
//             break;
//     }
// }

} // namespace mooncake_conductor  mooncake_conductor::uuid_to_str(mooncake::generate_uuid());