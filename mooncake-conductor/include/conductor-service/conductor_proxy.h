// conductor_proxy.h
#pragma once

#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_io/client_pool.hpp>
#include <async_simple/coro/Lazy.h>

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <string>
#include <optional>


namespace mooncake_conductor {

constexpr std::string SSE_END_TOKEN = "data: [DONE]";

enum class NodeCapability : uint8_t {
    NONE = 0,
    PREFILL = 1 << 0,
    DECODING = 1 << 1,
    BOTH = PREFILL | DECODING
};

struct NodeInfo {
    std::string host;
    uint16_t port;
    NodeCapability capability;
    
    NodeInfo(std::string h, uint16_t p, NodeCapability cap)
        : host(std::move(h)), 
          port(p),
          capability(cap) {}

    void set_capability(NodeCapability cap) {
        capability = cap;
    }
    
    NodeCapability get_capability() const {
        return capability;
    }
    
    std::string get_node_key() const {
        return host + ":" + std::to_string(port);
    }
};

enum class RequestCategory {
    INFERENCE,
    OTHERS
};

struct RouteRule {
    std::string_view path_prefix;
    std::string_view method;
    RequestCategory category;
};

inline const std::vector<RouteRule> kRouteRules = {
    {"/v1/chat/completions", "POST", RequestCategory::INFERENCE},
    {"/v1/completions", "POST", RequestCategory::INFERENCE}
};

struct SchedulingDecision {
    std::shared_ptr<NodeInfo> prefill_node = nullptr;
    std::shared_ptr<NodeInfo> decode_node = nullptr;
    bool is_stream = false;
    std::string cache_key;
};

class ConductorProxy {
public:

    enum class PDTransferMode {
        SYNC,    // 同步：等待Prefill完成
        ASYNC    // 异步：Prefill/Decode并行执行
    };

    struct Config {
        size_t connect_retry_count = 3;
        uint16_t listen_port = 8080;
        uint32_t max_connections_per_host = 32;
        uint32_t connect_timeout_ms = 1000;
        uint32_t stream_connect_timeout_ms = 3000;
        bool enable_pd_separation = true;
        PDTransferMode pd_transfer_mode = PDTransferMode::SYNC;
    };

    ConductorProxy(const Config& config);
    ~ConductorProxy();

    bool start();
    void stop();
    void register_node(std::string host, uint16_t port, NodeCapability capability);

private:

    using HttpClient = coro_http::coro_http_client;
    using ClientPools = coro_io::client_pools<HttpClient>;
    using coro_http_request = coro_http::coro_http_request;
    using coro_http_response = coro_http::coro_http_response;
    
    /************* functions **************/

    std::string generate_request_id();
    
    RequestCategory classify_request(coro_http_request& req);
    
    async_simple::coro::Lazy<void> handle_request(coro_http_request& req, coro_http_response& resp);
    
    async_simple::coro::Lazy<void> direct_forward(
        coro_http_request& req, 
        coro_http_response& resp, 
        std::shared_ptr<NodeInfo> node);

    bool validate_request_method(coro_http_request& req, coro_http_response& resp);
    
    std::pair<std::string, std::unordered_map<std::string, std::string>> 
    prepare_forward_context(coro_http_request& req, const std::shared_ptr<NodeInfo>& node);
    
    async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>> 
    handle_get_forward(std::string addr,
                       std::string endpoint,
                       std::unordered_map<std::string, std::string> headers);
    
    async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>> 
    handle_non_stream_post_forward(std::string addr,
                                   std::string endpoint,
                                   std::string body,
                                   std::unordered_map<std::string, std::string> headers);
    
    async_simple::coro::Lazy<void> handle_stream_post_forward(coro_http_response& resp,
                                                            std::string addr,
                                                            std::string endpoint,
                                                            std::string body,
                                                            std::unordered_map<std::string, std::string> headers);
    
    std::unordered_map<std::string, std::string> build_forward_headers(const auto& original_headers);
    
    std::unordered_map<std::string, std::string> build_pd_separation_forward_headers(const std::string& request_id);

    void configure_sse_response(coro_http_response& resp);
    
    async_simple::coro::Lazy<void> forward_backend_response(coro_http_response& resp,
                                                            const tl::expected<cinatra::resp_data, std::errc>& result);
    
    void handle_forward_error(coro_http_response& resp, const std::string& error_msg, int error_code);

    SchedulingDecision make_scheduling_decision(coro_http_request& req, coro_http_response& resp);
    
    bool is_stream_req(coro_http_request& req);
    
    void update_client_pools(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap);
    void update_node_categories(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap);

    /************** pd 分离相关 *******************/
    std::shared_ptr<NodeInfo> select_mixed_node();
    std::shared_ptr<NodeInfo> select_prefill_node();
    std::shared_ptr<NodeInfo> select_decode_node();
    
    std::string generate_pd_request_id(const std::string& prefill_addr, 
                                       const std::string& decode_addr);
    
    async_simple::coro::Lazy<void> handle_pd_separation(
                        coro_http_request& req, 
                        coro_http_response& resp);


    async_simple::coro::Lazy<void> handle_sync_pd_transfer(coro_http_request& req,
                                                        coro_http_response& resp,
                                                        std::shared_ptr<NodeInfo> prefill_node,
                                                        std::shared_ptr<NodeInfo> decode_node);

    async_simple::coro::Lazy<tl::expected<cinatra::resp_data, std::errc>> send_prefill_request(
                                                        std::shared_ptr<NodeInfo> prefill_node,
                                                        coro_http_request& original_req,
                                                        const std::unordered_map<std::string, std::string>& headers);

    async_simple::coro::Lazy<void> send_decode_request(coro_http_request& req,
                                                       coro_http_response& resp,
                                                       std::shared_ptr<NodeInfo> decode_node,
                                                       const std::unordered_map<std::string, std::string>& headers,
                                                       const std::optional<nlohmann::json>& kv_params_opt);

    std::unordered_map<std::string, std::string> generate_pd_separation_forward_headers(const std::string& request_id);
    std::string generate_prefill_request_body(coro_http_request& original_req);
    std::string generate_decode_request_body(coro_http_request& original_req,
                                             const std::optional<nlohmann::json>& prefill_kv_params);
    std::optional<nlohmann::json> extract_kv_transfer_params(const std::string_view prefill_response_body);


    /********* fields **********/
    Config config_;

    coro_io::client_pool<HttpClient>::pool_config non_stream_pool_cfg_;
    coro_io::client_pool<HttpClient>::pool_config stream_pool_cfg_;
    
    std::vector<std::shared_ptr<NodeInfo>> registered_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> prefill_capable_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> decoding_capable_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> mixed_capable_nodes_;
    
    std::shared_ptr<ClientPools> non_stream_client_pools_;
    std::shared_ptr<ClientPools> stream_client_pools_;
    
    coro_http::coro_http_server http_server_;
    std::shared_mutex pools_mutex_;
    std::shared_mutex nodes_mutex_;
    std::atomic<bool> running_{false};
    std::string api_key_header_;
};

} // namespace mooncake_conductor