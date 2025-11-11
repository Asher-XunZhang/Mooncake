#pragma once

// #include "cache_hit_distribution_collector.h"
#include "conductor_types.h"

#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_http/coro_http_server.hpp>

#include <unordered_set>
#include <vector>
#include <unordered_map>

namespace mooncake_conductor {

class LLMServerState {
public:
    LLMServerState(const std::string& host, int port, std::chrono::seconds timeout) 
        : host(host), port(port), url("http://" + host + ":" + std::to_string(port) + "/v1") {
        client = std::make_unique<coro_http::coro_http_client>();
        client->set_req_timeout(timeout);
        client->add_header("Content-Type", "application/json");
        // url = "http://" + host + ":" + std::to_string(port) + "/v1";
        // auto conn_result = client->connect(url);
        // if (conn_result.net_err) {
        //     std::cout << "连接测试失败: " << conn_result.net_err.message() << std::endl;
        // }
    }

    std::string host;
    int port;
    // TODO url support sglang endpoint type
    std::string url;
    std::unique_ptr<coro_http::coro_http_client> client;
    
    std::atomic<int> active_tokens{0};
    std::atomic<int> active_kv_cache{0};  // Only for prefiller
    std::atomic<int> active_requests{0};  // Number of active requests
    
    // thread safely aborted_requests set
    mutable std::shared_mutex aborted_requests_mutex;
    std::unordered_set<std::string> aborted_requests;
};


class ProxyState {
public:
    using ServerInstance = std::pair<std::string, int>;

    struct ServerEntry {
        double priority;
        size_t index;
        std::shared_ptr<LLMServerState> server;
        
        ServerEntry(double p, size_t i, std::shared_ptr<LLMServerState> s) 
            : priority(p), index(i), server(s) {}
    };

    struct CompareServer {
        bool operator()(const ServerEntry& a, const ServerEntry& b) const {
            return a.priority > b.priority; // min heap
        }
    };
    
    ProxyState(const std::vector<ServerInstance>& prefiller_instances, 
               const std::vector<ServerInstance>& decoder_instances);

    std::vector<std::shared_ptr<LLMServerState>> prefillers;
    std::vector<std::shared_ptr<LLMServerState>> decoders;
    
    std::unordered_map<std::string, size_t> req_to_prefiller;
    std::mutex req_id_lock;
    
    std::vector<ServerEntry> prefiller_heap;
    std::vector<ServerEntry> decoder_heap;
    mutable std::mutex prefiller_heap_mutex;
    mutable std::mutex decoder_heap_mutex;
    std::chrono::seconds default_timeout{5};

    void abort_prefiller_request(size_t server_idx, const std::string& request_id);

    std::unordered_set<std::string> acquire_aborted_prefiller_requests(size_t server_idx);

    size_t select_prefiller(int token_count);

    // void release_prefiller(size_t idx, int token_count) {
    //     auto server = prefillers[idx];
    //     server->active_tokens.fetch_sub(token_count);
    //     update_prefiller_priority(idx);
    // }
private:
    void update_prefiller_priority(size_t server_idx);
    void update_decoder_priority(size_t server_idx);

};


class RequestHandler {
public:
    RequestHandler(const ProxyServerArgs cofig, std::string collector, std::string load_collector);
    
    std::string handleRequest(const std::unordered_map<std::string_view, std::string_view>& request);

    void ping_llm_server(std::chrono::seconds timeout);

    std::pair<std::string, int> select_prefill_instance(
        std::vector<std::pair<std::string, int>> prefiller_instances);

    // batch processing requests
    void handleBatchRequests(const std::vector<std::string>& requests);
    
    int getStatus(const std::string& request_id) const;
    
private:
    // TODO use message_queue to decouple request_handler and proxy_server 
    std::unique_ptr<int> message_queue_;
    std::unique_ptr<ProxyState> proxy_state_;
    // std::shared_ptr<CacheHitDistributionCollector> cache_collector_;
    // std::shared_ptr<LoadMetricsCollector> load_collector_;
    // void parseAndValidateRequest(const coro_http_request& request);
    void triggerLoadMetricsCollection();
};

}  // namespace mooncake_conduct