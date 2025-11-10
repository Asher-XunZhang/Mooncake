#include "request_handler.h"
#include "vllm_endpoint_adapter.h"

#include <unordered_map>
#include <string>
#include <glog/logging.h>


namespace mooncake_conductor {

ProxyState::ProxyState(const std::vector<ServerInstance>& prefiller_instances, 
                 const std::vector<ServerInstance>& decoder_instances) {
    for (const auto& [host, port] : prefiller_instances) {
        prefillers.push_back(std::make_shared<LLMServerState>(host, port));
    }
    
    for (const auto& [host, port] : decoder_instances) {
        decoders.push_back(std::make_shared<LLMServerState>(host, port));
    }
    
    for (size_t i = 0; i < prefillers.size(); ++i) {
        prefiller_heap.emplace_back(0, i, prefillers[i]);
    }
    for (size_t i = 0; i < decoders.size(); ++i) {
        decoder_heap.emplace_back(0, i, decoders[i]);
    }
    
    std::make_heap(prefiller_heap.begin(), prefiller_heap.end(), CompareServer());
    std::make_heap(decoder_heap.begin(), decoder_heap.end(), CompareServer());
}

void ProxyState::abort_prefiller_request(size_t server_idx, const std::string& request_id) {
    std::lock_guard<std::shared_mutex> lock(prefillers[server_idx]->aborted_requests_mutex);
    prefillers[server_idx]->aborted_requests.insert(request_id);
}

std::unordered_set<std::string> ProxyState::acquire_aborted_prefiller_requests(size_t server_idx) {
    std::lock_guard<std::shared_mutex> lock(prefillers[server_idx]->aborted_requests_mutex);
    std::unordered_set<std::string> aborted_requests = prefillers[server_idx]->aborted_requests;
    prefillers[server_idx]->aborted_requests.clear();
    return aborted_requests;
}

size_t ProxyState::select_prefiller(int token_count) {
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

void ProxyState::update_prefiller_priority(size_t server_idx) {
    std::lock_guard<std::mutex> lock(prefiller_heap_mutex);
    auto server = prefillers[server_idx];
    // TODO 替换为kv-center scheduler
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

void ProxyState::update_decoder_priority(size_t server_idx) {
    std::lock_guard<std::mutex> lock(decoder_heap_mutex);
    auto server = decoders[server_idx];
    // TODO 替换为load-balance的调度
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


RequestHandler::RequestHandler(std::string collector, std::string load_collector){
    std::cout << "Finish init Requesthandler!!";
}

std::string RequestHandler::handleRequest(
    const std::unordered_map<std::string_view, std::string_view>& request) {
    return "next support.";
}

std::pair<std::string, int> select_prefill_instance(
    std::vector<std::pair<std::string, int>> prefiller_instances) {
    // 
    return std::pair<std::string, int> {"123", 123};
}

} // namespace mooncake_conductor