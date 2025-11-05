#pragma once
#include "tokenization_result.h"
#include "node_metrics.h"
#include <vector>

class APIEndpointAdapter {
public:
    virtual ~APIEndpointAdapter() = default;
    
    // Tokenization接口
    virtual TokenizationResult tokenize(const std::string& prompt) = 0;
    virtual std::vector<TokenizationResult> tokenizeBatch(const std::vector<std::string>& prompts) = 0;
    
    // 指标收集接口
    virtual NodeMetrics getNodeMetrics(const std::string& endpoint_url) = 0;
    virtual std::unordered_map<std::string, NodeMetrics> getClusterMetrics() = 0;
    
    // 健康检查
    virtual bool healthCheck(const std::string& endpoint_url) = 0;
    virtual std::unordered_map<std::string, bool> healthCheckCluster() = 0;
    
    // 引擎特定配置获取
    virtual EngineConfig getEngineConfig(const std::string& endpoint_url) = 0;
    
    // 适配器标识
    virtual std::string getAdapterType() const = 0;
    virtual std::string getSupportedEngine() const = 0;
    
    // 性能统计
    virtual AdapterPerformanceStats getPerformanceStats() const = 0;
    
    // 连接管理
    virtual void setConnectionTimeout(std::chrono::milliseconds timeout) = 0;
    virtual void setRetryPolicy(const RetryPolicy& policy) = 0;
};

// 工厂类
class APIEndpointAdapterFactory {
public:
    static std::unique_ptr<APIEndpointAdapter> createAdapter(
        const std::string& engine_type,
        const AdapterConfig& config);
    
    static std::vector<std::string> getSupportedEngines();
};