#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace mooncake_conductor {

struct SLORequirement {
    double max_ttft_ms = 100.0;
    double max_tbt_ms = 50.0;
    int priority = 1;
};


struct NodeMetrics {
    double gpu_utilization = 0.0;
    uint32_t queue_depth = 0;
    uint64_t memory_used_bytes = 0;
    uint64_t memory_total_bytes = 0;
    double kv_cache_hit_rate = 0.0;
    int64_t last_update_time = 0;
};

struct ProxyServerArgs {
    int port;
    std::string host;
    std::vector<std::string> prefiller_hosts;
    std::vector<int> prefiller_ports;
    std::vector<std::string> decoder_hosts;
    std::vector<int> decoder_ports;
    int max_retries;
    double retry_delay;
    std::vector<std::pair<std::string, int>> prefiller_instances;
    std::vector<std::pair<std::string, int>> decoder_instances;
};


struct CacheMatchRequest {
    std::string request_id;
    std::vector<int> prompt_tokens;
    std::string model_name;
    SLORequirement slo_requirements;

    CacheMatchRequest(std::vector<int> tokens, std::string model, 
                     SLORequirement slo = {}, std::string rid = "")
        : prompt_tokens(std::move(tokens)), model_name(std::move(model)),
          slo_requirements(std::move(slo)), request_id(std::move(rid)) {
        if (request_id.empty()) {
            request_id = generate_uuid();
        }
    }
    
private:
    static std::string generate_uuid();
};

// ============ HTTP请求结构 ============
struct HttpRequest {
    std::string url;
    std::string method{"POST"};  // 默认为POST
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    [[nodiscard]] bool isValid() const { return !url.empty() && !method.empty(); }

    [[nodiscard]] std::string toString() const {
        return "HttpRequest{url: " + url + ", method: " + method +
                ", body_size: " + std::to_string(body.size()) + "}";
    }
};

// ============ Tokenization相关 ============
struct TokenizationResult {
    std::vector<int> token_ids;
    std::string model_name;
    size_t token_count{0};
    bool truncated{false};
    std::string error_message;

    [[nodiscard]] bool isValid() const {
        return error_message.empty() && !token_ids.empty();
    }

    [[nodiscard]] std::string toString() const {
        return "TokenizationResult{tokens: " + std::to_string(token_count) +
                ", model: " + model_name + ", error: " + error_message + "}";
    }
};

// ============ 引擎配置 ============
struct EngineConfig {
    std::string model_name;
    int tensor_parallel_size{1};
    int pipeline_parallel_size{1};
    int max_sequence_length{4096};
    std::string dtype{"float16"};
    int block_size{128};

    [[nodiscard]] bool isValid() const { return !model_name.empty(); }

    [[nodiscard]] std::string toString() const {
        return "EngineConfig{model: " + model_name +
                ", tp_size: " + std::to_string(tensor_parallel_size) + "}";
    }
};

// ============ 负载指标 ============
struct LoadMetrics {
    double gpu_utilization{0.0};
    double cpu_utilization{0.0};
    uint64_t memory_used{0};
    uint64_t memory_total{0};
    uint32_t queue_depth{0};
    uint32_t active_requests{0};
    double tokens_per_second{0.0};
    double kv_cache_utilization{0.0};
    bool is_healthy{false};

    [[nodiscard]] double getLoadFactor() const {
        return (gpu_utilization * 0.6 + cpu_utilization * 0.2 +
                static_cast<double>(queue_depth) / 100.0 * 0.2);
    }

    [[nodiscard]] std::string toString() const {
        return "LoadMetrics{gpu: " + std::to_string(gpu_utilization * 100) +
                "%, queue: " + std::to_string(queue_depth) + "}";
    }
};

}