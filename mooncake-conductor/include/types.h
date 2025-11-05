#pragma once

#include <string>
#include <vector>

namespace mooncake {

struct SLORequirement {
    double max_ttft_ms = 100.0;
    double max_tbt_ms = 50.0;
    int priority = 1;
};

struct TokenizationResult {
    std::vector<int> token_ids;
    std::string model_name;
};

struct NodeMetrics {
    double gpu_utilization = 0.0;
    uint32_t queue_depth = 0;
    uint64_t memory_used_bytes = 0;
    uint64_t memory_total_bytes = 0;
    double kv_cache_hit_rate = 0.0;
    int64_t last_update_time = 0;
};

// 缓存匹配请求，承载一次请求的完整上下文信息
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

}