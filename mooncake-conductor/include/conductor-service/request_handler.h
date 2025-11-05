// request_handler.h
#pragma once
#include "user_request.h"
#include "cache_hit_distribution_collector.h"

class RequestHandler {
private:
    std::unique_ptr<MessageQueue> message_queue_;
    std::shared_ptr<CacheHitDistributionCollector> cache_collector_;
    std::shared_ptr<LoadMetricsCollector> load_collector_;

public:
    RequestHandler(std::shared_ptr<CacheHitDistributionCollector> collector,
                   std::shared_ptr<LoadMetricsCollector> load_collector);
    
    // 处理传入的用户请求
    void handleRequest(const UserRequest& request);
    
    // 批量处理请求
    void handleBatchRequests(const std::vector<UserRequest>& requests);
    
    // 获取处理状态
    RequestStatus getStatus(const std::string& request_id) const;
    
private:
    void parseAndValidateRequest(const UserRequest& request);
    void triggerLoadMetricsCollection();
};