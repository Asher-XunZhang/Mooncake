#pragma once
#include "logical_cache_key.h"
#include <vector>
#include <type_traits>

// 前向声明引擎标签
struct VLLMTag;
struct SGLangTag;
struct TensorRTLLMTag;

// 模板基类
template<typename EngineTag>
class PhysicalKeyGenerator {
public:
    // 生成物理缓存键
    static std::vector<std::string> generatePhysicalKeys(
        const LogicalCacheKey& logical_key,
        int target_rank_id,
        const EngineConfig& config);
    
    // 批量生成（性能优化）
    static std::vector<std::string> generateBatchPhysicalKeys(
        const std::vector<LogicalCacheKey>& logical_keys,
        int target_rank_id,
        const EngineConfig& config);
    
    // 验证生成的键格式是否正确
    static bool validatePhysicalKey(const std::string& physical_key);
    
    // 从物理键解析出组件（用于调试和分析）
    static PhysicalKeyComponents parsePhysicalKey(const std::string& physical_key);
};

// 物理键组件结构（用于解析）
struct PhysicalKeyComponents {
    std::string model_name;
    int world_size;
    int rank_id;
    std::string content_hash;
    int layer_id; // 可选，取决于引擎
    std::string engine_specific_data;
    
    bool isValid() const;
    std::string toDebugString() const;
};