#pragma once

// #include "cache_hit_distribution_collector.h"

#include <ylt/coro_http/coro_http_server.hpp>

#include <vector>
#include <unordered_map>

namespace mooncake_conductor {


class RequestHandler {
private:
    std::unique_ptr<int> message_queue_;
    // std::shared_ptr<CacheHitDistributionCollector> cache_collector_;
    // std::shared_ptr<LoadMetricsCollector> load_collector_;

public:
    RequestHandler(std::string collector, std::string load_collector);
    
    std::string handleRequest(const std::unordered_map<std::string_view, std::string_view>& request);

    std::pair<std::string, int> select_prefill_instance(
        std::vector<std::pair<std::string, int>> prefiller_instances);

    // 批量处理请求
    void handleBatchRequests(const std::vector<std::string>& requests);
    
    // 获取处理状态
    int getStatus(const std::string& request_id) const;
    
private:
    // void parseAndValidateRequest(const coro_http_request& request);
    void triggerLoadMetricsCollection();
};

}  // namespace mooncake_conduct