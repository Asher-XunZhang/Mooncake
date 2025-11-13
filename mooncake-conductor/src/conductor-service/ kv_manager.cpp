#include "kv_manager.h"

#include <array>
#include <vector>
#include <bitset>
#include <memory>
#include <tuple>
#include <string>

#include <stdexcept>
#include <type_traits>
#include <concepts>
#include <openssl/sha.h>

// 直接使用这个类型
struct block_hash {
    std::array<std::byte, 32> hash_;
};

template <std::size_t N>
block_hash sha256(std::tuple<std::array<std::byte, N>, std::vector<int>> input) {
    auto input_bytes = struct_pack::serialize<std::string>(input);
    
    SHA256_CTX context;
    SHA256_Init(&context);
    SHA256_Update(&context, input_bytes.data(), input_bytes.size());
    
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256_Final(hash.data(), &context);
    
    return hash;
}


template <std::size_t N>
std::byte hash_block_tokens(
    const std::array<std::byte, N> parent_block_hash,
    const std::vector<int>& curr_block_token_ids,
) {
    // TODO 将parent的和当前的block hash合并
    const std::array<std::byte, N> parent_hash = parent_block_hash ? *parent_block_hash : g_none_hash;
    
    auto hash_input = std::make_tuple(
        parent_hash,
        curr_block_token_ids
    );
    
    return sha256(hash_input);
}

struct Request {
    std::vector<std::byte> block_hashes;
    int num_tokens;
    std::vector<int> all_token_ids;
};

std::vector<std::byte> get_request_block_hasher(
    const Request& request,
    int block_size
) {
    int start_token_idx = static_cast<int>(request.block_hashes.size()) * block_size;
    int num_tokens = request.num_tokens;
    
    if (start_token_idx + block_size > num_tokens) {
        return {};
    }
    
    const std::byte* prev_block_hash_value = request.block_hashes.empty() ? 
        nullptr : &request.block_hashes.back();
    
    std::vector<std::byte> new_block_hashes;
    int current_start = start_token_idx;
    
    while (true) {
        int end_token_idx = current_start + block_size;
        if (end_token_idx > num_tokens) {
            break;
        }
        
        std::vector<int> block_tokens(
            request.all_token_ids.begin() + current_start,
            request.all_token_ids.begin() + end_token_idx
        );
        
        std::array<std::byte,32> block_hash = hash_block_tokens(
            prev_block_hash_value,
            block_tokens
        );
        
        new_block_hashes.push_back(block_hash);
        current_start += block_size;
        prev_block_hash_value = &new_block_hashes.back();
    }
    
    return new_block_hashes;
}
