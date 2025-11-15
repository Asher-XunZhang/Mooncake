#include "block_serializer.h"
#include "hash.h"

#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <optional>
#include <functional>


namespace mooncake_conductor {

std::string BlockSerializer::to_hex(const std::vector<uint8_t>& data) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(data.size() * 2);
    for (auto byte : data) {
        result.push_back(hex_chars[(byte >> 4) & 0x0F]);
        result.push_back(hex_chars[byte & 0x0F]);
    }
    return result;
}
    
std::vector<uint8_t> BlockSerializer::serialize_block(
    const std::optional<std::vector<uint8_t>>& parent_block_hash,
    const std::vector<int64_t>& curr_block_token_ids) {
    
    const auto& parent_hash = parent_block_hash ? 
        *parent_block_hash : none_hash;
        
    return serializer.serialize(parent_hash, curr_block_token_ids);
}
    
std::vector<std::vector<uint8_t>> BlockSerializer::serialize_blocks(
    const std::vector<int64_t>& all_token_ids,
    size_t block_size) {
    
    std::vector<std::vector<uint8_t>> serialized_blocks;
    std::optional<std::vector<uint8_t>> prev_hash;
    
    for (size_t start = 0; start + block_size <= all_token_ids.size(); start += block_size) {
        std::vector<int64_t> block_tokens(
            all_token_ids.begin() + start,
            all_token_ids.begin() + start + block_size
        );
        
        auto serialized = serialize_block(prev_hash, block_tokens);
        serialized_blocks.push_back(serialized);
        prev_hash = sha256(serialized);
    }
    
    return serialized_blocks;
}


}