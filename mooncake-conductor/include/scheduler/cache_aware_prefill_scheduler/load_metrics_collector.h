#pragma once
#include "node_metrics.h"
#include "api_endpoint_adapter.h"
#include <unordered_map>

class LoadMetricsCollector {
private:
    std::shared_ptr<APIEndpointAdapter> endpoint_adapter_;
    std::unordered_map<std::string, NodeMetrics> node_metrics_cache_;
    mutable std::shared_mutex cache_mutex_;
    std::chrono::seconds metrics_ttl_;
    
    // 负载计算算法
    std::unique_ptr<LoadCalculationStrategy> load_strategy_;

public:
    LoadMetricsCollector(std::shared_ptr<APIEndpointAdapter> adapter);
    
    // 收集所有可用节点的当前负载指标
    std::unordered_map<std::string, NodeMetrics> collectCurrentMetrics();
    
    // 获取特定节点的负载指标
    NodeMetrics getNodeMetrics(const std::string& node_id);
    
    // 获取节点负载评分（0-1，越高表示负载越重）
    double getNodeLoadScore(const std::string& node_id);
    
    // 触发异步指标收集
    void triggerAsyncCollection();
    
    // 注册指标更新回调
    void registerMetricsUpdateCallback(std::function<void(const std::string&, const NodeMetrics&)> callback);

private:
    NodeMetrics fetchNodeMetricsFromEndpoint(const std::string& node_endpoint);
    void updateMetricsCache(const std::string& node_id, const NodeMetrics& metrics);
    bool isMetricsExpired(const std::string& node_id) const;
};