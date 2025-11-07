#pragma once
#include "conductor_types.h"
#include "request_handler.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_set>
#include <glog/logging.h>
#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/coro_rpc/coro_rpc_server.hpp>

// used to notify the server to exit
std::atomic<bool> g_stop_flag{false};

namespace mooncake_conductor {

class LLMServerState {
public:
    LLMServerState(const std::string& host, int port) 
        : host(host), port(port), url("http://" + host + ":" + std::to_string(port) + "/v1") {
        // init coro_http_client
        client = std::make_shared<coro_http::coro_http_client>();
        client->set_req_timeout(std::chrono::seconds(10)); 
        // auto conn_result = client->connect(url);
        // if (conn_result.net_err) {
        //     std::cout << "连接测试失败: " << conn_result.net_err.message() << std::endl;
        // }
    }

    std::string host;
    int port;
    std::string url;
    std::shared_ptr<coro_http::coro_http_client> client;
    
    std::atomic<int> active_tokens{0};
    std::atomic<int> active_kv_cache{0};  // Only for prefiller
    std::atomic<int> active_requests{0};  // Number of active requests
    
    // thread safely aborted_requests set
    mutable std::shared_mutex aborted_requests_mutex;
    std::unordered_set<std::string> aborted_requests;
};

/*
class ProxyState {
public:
    using ServerInstance = std::pair<std::string, int>;
    
    ProxyState(const std::vector<ServerInstance>& prefiller_instances, 
               const std::vector<ServerInstance>& decoder_instances) {
        for (const auto& [host, port] : prefiller_instances) {
            prefillers.push_back(std::make_shared<LLMServerState>(host, port));
        }
        
        for (const auto& [host, port] : decoder_instances) {
            decoders.push_back(std::make_shared<LLMServerState>(host, port));
        }
        
        for (size_t i = 0; i < prefillers.size(); ++i) {
            prefiller_heap.emplace(0, i, prefillers[i]);
        }
        for (size_t i = 0; i < decoders.size(); ++i) {
            decoder_heap.emplace(0, i, decoders[i]);
        }
        
        std::make_heap(prefiller_heap.begin(), prefiller_heap.end(), CompareServer());
        std::make_heap(decoder_heap.begin(), decoder_heap.end(), CompareServer());
    }

    std::vector<std::shared_ptr<LLMServerState>> prefillers;
    std::vector<std::shared_ptr<LLMServerState>> decoders;
    
    std::unordered_map<std::string, size_t> req_to_prefiller;
    std::mutex req_id_lock;
    
    std::vector<ServerEntry> prefiller_heap;
    std::vector<ServerEntry> decoder_heap;
    mutable std::mutex prefiller_heap_mutex;
    mutable std::mutex decoder_heap_mutex;

    void abort_prefiller_request(size_t server_idx, const std::string& request_id) {
        std::lock_guard<std::shared_mutex> lock(prefillers[server_idx]->aborted_requests_mutex);
        prefillers[server_idx]->aborted_requests.insert(request_id);
    }

    std::unordered_set<std::string> acquire_aborted_prefiller_requests(size_t server_idx) {
        std::lock_guard<std::shared_mutex> lock(prefillers[server_idx]->aborted_requests_mutex);
        std::unordered_set<std::string> aborted_requests = prefillers[server_idx]->aborted_requests;
        prefillers[server_idx]->aborted_requests.clear();
        return aborted_requests;
    }

    // 异步生成请求ID
    async::task<std::string> next_req_id() {
        std::lock_guard<std::mutex> lock(req_id_lock);
        co_return generate_uuid(); 
    }

    size_t select_prefiller(int token_count) {
        std::lock_guard<std::mutex> lock(prefiller_heap_mutex);
        
        if (prefiller_heap.empty()) {
            throw std::runtime_error("No prefiller servers available");
        }
        
        // 弹出优先级最高的服务器
        std::pop_heap(prefiller_heap.begin(), prefiller_heap.end(), CompareServer());
        auto chosen_entry = prefiller_heap.back();
        prefiller_heap.pop_back();
        
        size_t chosen_idx = chosen_entry.index;
        auto chosen_server = prefillers[chosen_idx];
        
        // 原子更新服务器状态
        chosen_server->active_tokens.fetch_add(token_count);
        chosen_server->active_kv_cache.fetch_add(token_count);
        
        update_prefiller_priority(chosen_idx);
        
        return chosen_idx;
    }

    void release_prefiller(size_t idx, int token_count) {
        auto server = prefillers[idx];
        server->active_tokens.fetch_sub(token_count);
        update_prefiller_priority(idx);
    }

private:
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

    std::string generate_uuid() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);
        
        std::stringstream ss;
        ss << std::hex;
        for (int i = 0; i < 8; i++) ss << dis(gen);
        ss << "-";
        for (int i = 0; i < 4; i++) ss << dis(gen);
        ss << "-4";
        for (int i = 0; i < 3; i++) ss << dis(gen);
        ss << "-" << dis2(gen);
        for (int i = 0; i < 3; i++) ss << dis(gen);
        ss << "-";
        for (int i = 0; i < 12; i++) ss << dis(gen);
        return ss.str();
    }

    void update_prefiller_priority(size_t server_idx) {
        std::lock_guard<std::mutex> lock(prefiller_heap_mutex);
        auto server = prefillers[server_idx];
        
        double priority = server->active_tokens.load() + server->active_kv_cache.load() * 0.3;
        
        prefiller_heap.erase(
            std::remove_if(prefiller_heap.begin(), prefiller_heap.end(),
                [server_idx](const ServerEntry& entry) {
                    return entry.index == server_idx;
                }),
            prefiller_heap.end()
        );
        
        prefiller_heap.emplace_back(priority, server_idx, server);
        std::push_heap(prefiller_heap.begin(), prefiller_heap.end(), CompareServer());
    }

    void update_decoder_priority(size_t server_idx) {
        std::lock_guard<std::mutex> lock(decoder_heap_mutex);
        auto server = decoders[server_idx];
        double priority = server->active_tokens.load();
        
        decoder_heap.erase(
            std::remove_if(decoder_heap.begin(), decoder_heap.end(),
                [server_idx](const ServerEntry& entry) {
                    return entry.index == server_idx;
                }),
            decoder_heap.end()
        );
        
        decoder_heap.emplace_back(priority, server_idx, server);
        std::push_heap(decoder_heap.begin(), decoder_heap.end(), CompareServer());
    }

};
*/

class ProxyServer {
   public:
    ProxyServer(const ProxyServerArgs& config);
    ~ProxyServer();

    void init_http_server();

    void start_server();

    void stop_server();

    ProxyServer(const ProxyServer&) = delete;
    ProxyServer& operator=(const ProxyServer&) = delete;

   private:
    uint16_t port_;
    std::string host_;
    std::unique_ptr<coro_http::coro_http_server> http_server_;
    std::unique_ptr<RequestHandler> request_handler_;
};

} // namespace mooncake-conductor