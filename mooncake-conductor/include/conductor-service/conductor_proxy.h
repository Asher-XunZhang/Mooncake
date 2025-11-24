// conductor_proxy.h
#pragma once

#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_io/client_pool.hpp>
#include <async_simple/coro/Generator.h>
#include <async_simple/coro/Lazy.h>
#include <boost/asio.hpp>

#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <bitset>
#include <string>
#include <map>


namespace mooncake_conductor {

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

struct SchedulingDecision {
    std::shared_ptr<NodeInfo> prefill_node = nullptr;
    std::shared_ptr<NodeInfo> decode_node = nullptr;
    bool is_stream = false;
    std::string cache_key;
};

struct RequestContext {
    std::string request_id;
    bool is_stream = false;
    std::string path;
    std::string method;
    std::string raw_body;
    std::vector<std::pair<std::string, std::string>> headers;
    bool has_existing_cache = false;
};

using StreamChunk = std::string;
using StreamChunkGenerator = async_simple::coro::Generator<StreamChunk>;

class ConductorProxy {
public:
    struct Config {
        size_t connect_retry_count = 3;
        uint16_t listen_port = 8080;
        uint32_t max_connections_per_host = 32;
        uint32_t connect_timeout_ms = 1000;
        uint32_t stream_connect_timeout_ms = 30000;
        bool enable_mixed_deployment = true;
    };

    ConductorProxy(const Config& config);
    ~ConductorProxy();

    bool start();
    void stop();
    void register_node(std::string host, uint16_t port, NodeCapability capability);

private:

    using HttpClient = coro_http::coro_http_client;
    using ClientPools = coro_io::client_pools<HttpClient>;
    
    std::string generate_request_id();
    
    RequestCategory classify_request(coro_http::coro_http_request& req);
    
    // void handle_http_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp);
    
    async_simple::coro::Lazy<void> handle_inference_request(
        coro_http::coro_http_request& req, coro_http::coro_http_response& resp);
    
    void handle_other_request(coro_http::coro_http_request& req, coro_http::coro_http_response& resp);
    
    async_simple::coro::Lazy<StreamChunkGenerator> handle_stream_inference(
        const RequestContext& ctx, 
        const std::shared_ptr<NodeInfo>& decode_node);
    
    async_simple::coro::Lazy<void> handle_non_stream_inference(
        coro_http::coro_http_request& req, 
        coro_http::coro_http_response& resp);
    
    async_simple::coro::Lazy<StreamChunkGenerator> stream_from_decode_node(
        const std::shared_ptr<NodeInfo>& node,
        const std::string& target_url,
        const RequestContext& ctx,
        const std::string& request_id);
    
    void direct_forward(
        coro_http::coro_http_request& req, 
        coro_http::coro_http_response& resp, 
        std::shared_ptr<NodeInfo> node);
    
    std::vector<std::pair<std::string, std::string>> broadcast_request_to_nodes(
        coro_http::coro_http_request& req, 
        const std::vector<std::shared_ptr<NodeInfo>>& nodes);
    std::string aggregate_responses(
        const std::string& path,
        const std::vector<std::pair<std::string, std::string>>& responses);

    SchedulingDecision make_scheduling_decision(const RequestContext& ctx);
    
    RequestContext parse_request_context(coro_http::coro_http_request& req);
    
    void update_client_pools(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap);
    void update_node_categories(std::shared_ptr<NodeInfo> new_node_ptr, NodeCapability prev_cap);
    
    Config config_;
    
    std::vector<std::shared_ptr<NodeInfo>> registered_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> prefill_capable_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> decoding_capable_nodes_;
    std::vector<std::shared_ptr<NodeInfo>> mixed_capable_nodes_;
    
    std::shared_ptr<ClientPools> node_client_pools_;
    std::shared_ptr<ClientPools> stream_client_pools_;
    
    coro_http::coro_http_server http_server_;
    std::mutex pools_mutex_;
    std::mutex nodes_mutex_;
    std::atomic<bool> running_{false};
};

} // namespace mooncake_conductor