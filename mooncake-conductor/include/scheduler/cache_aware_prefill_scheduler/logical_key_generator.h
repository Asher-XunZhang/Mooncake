#pragma once
#include <string>
#include <vector>
#include <unordered_set>

class LogicalCacheKey {
private:
    std::string model_name_;
    int world_size_;
    std::string content_hash_;
    std::vector<std::string> chunk_hashes_;
    std::string prompt_fingerprint_; // 用于快速比较的指纹

public:
    LogicalCacheKey(const std::string& model_name, int world_size, 
                   const std::string& content_hash,
                   const std::vector<std::string>& chunk_hashes = {});
    
    // 访问器方法
    const std::string& getModelName() const { return model_name_; }
    int getWorldSize() const { return world_size_; }
    const std::string& getContentHash() const { return content_hash_; }
    const std::vector<std::string>& getChunkHashes() const { return chunk_hashes_; }
    
    // 键比较和哈希支持
    bool operator==(const LogicalCacheKey& other) const;
    bool operator<(const LogicalCacheKey& other) const;
    
    // 生成字符串表示（用于序列化）
    std::string toString() const;
    
    // 从字符串解析（反序列化）
    static LogicalCacheKey fromString(const std::string& key_str);
    
    // 生成指纹（用于快速比较）
    std::string generateFingerprint() const;

    // 哈希支持
    struct Hash {
        size_t operator()(const LogicalCacheKey& key) const;
    };
    
    // 工具方法
    bool isValid() const;
    size_t getEstimatedSize() const;
};

// 工厂函数
namespace LogicalCacheKeyFactory {
    LogicalCacheKey createFromPrompt(const std::string& prompt, 
                                   const std::string& model_name,
                                   int world_size = 1);
    LogicalCacheKey createFromTokens(const std::vector<int>& tokens,
                                   const std::string& model_name,
                                   int world_size = 1);
}