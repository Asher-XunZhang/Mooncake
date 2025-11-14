#pragma once

#include "python_pickle_serializer.h"
#include "hash.h"

#include <vector>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace mooncake_conductor {

class BlockSerializer {
private:
    BoostPythonPickleSerializer serializer;
    std::vector<uint8_t> none_hash;
    
public:
    BlockSerializer() : none_hash(32, 0) {} // 32字节零作为 NONE_HASH
    
    std::vector<uint8_t> serialize_block(
        const std::vector<uint8_t>* parent_block_hash,
        const std::vector<int64_t>& curr_block_token_ids) {
        
        const auto& parent_hash = parent_block_hash ? 
            *parent_block_hash : none_hash;
            
        return serializer.serialize(parent_hash, curr_block_token_ids, nullptr);
    }
    
    std::vector<std::vector<uint8_t>> serialize_blocks(
        const std::vector<int64_t>& all_token_ids,
        size_t block_size) {
        
        std::vector<std::vector<uint8_t>> serialized_blocks;
        const std::vector<uint8_t>* prev_hash = nullptr;
        std::vector<uint8_t> prev_ha;
        
        for (size_t start = 0; start + block_size <= all_token_ids.size(); start += block_size) {
            std::vector<int64_t> block_tokens(
                all_token_ids.begin() + start,
                all_token_ids.begin() + start + block_size
            );
            
            auto serialized = serialize_block(prev_hash, block_tokens);
            serialized_blocks.push_back(serialized);
            prev_ha = sha256(serialized);
            prev_hash = &prev_ha;
        }
        
        return serialized_blocks;
    }
};

void test_serializer() {
    try {
        BlockSerializer serializer;
        
        std::vector<int64_t> token_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::vector<std::string> expected_serialized_blocks = {
            "80059534000000000000004320000000000000000000000000000000000000000000000000000000000000000094284b014b024b034b044b0574944e87942e",
            "8005953400000000000000432062a05fac03f5470c9e1e66b43447b1cb321ec98e3afb509f531d0781dde12d5294284b064b074b084b094b0a74944e87942e"
        };

        size_t block_size = 5;
        
        auto serialized_blocks = serializer.serialize_blocks(token_ids, block_size);
        
        std::cout << "Serialized " << serialized_blocks.size() << " blocks" << std::endl;
        
        bool all_blocks_match = true;
        
        for (size_t i = 0; i < serialized_blocks.size(); ++i) {
            const auto& block = serialized_blocks[i];
            std::cout << "Block " << i + 1 << ": " << block.size() << " bytes" << std::endl;
            
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (size_t j = 0; j < block.size(); ++j) {
                oss << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(block[j]));
            }
            std::string actual_hex = oss.str();
            std::string expected_hex = expected_serialized_blocks.at(i);
            
            bool match = (actual_hex == expected_hex);
            
            if (match) {
                std::cout << "✓ Block " << i + 1 << " matches expected output" << std::endl;
            } else {
                all_blocks_match = false;
                std::cout << "✗ Block " << i + 1 << " does NOT match expected output!" << std::endl;
                std::cout << "  Expected length: " << expected_hex.length() << " chars" << std::endl;
                std::cout << "  Actual length:   " << actual_hex.length() << " chars" << std::endl;
                
                std::cout << "  Expected (first 64 chars): " << expected_hex.substr(0, 64) << std::endl;
                std::cout << "  Actual (first 64 chars):   " << actual_hex.substr(0, 64) << std::endl;
                
                size_t mismatch_pos = 0;
                while (mismatch_pos < std::min(actual_hex.length(), expected_hex.length()) && 
                       actual_hex[mismatch_pos] == expected_hex[mismatch_pos]) {
                    mismatch_pos++;
                }
                
                std::cout << "  First mismatch at position: " << mismatch_pos << std::endl;
                
                size_t context_start = (mismatch_pos > 10) ? mismatch_pos - 10 : 0;
                size_t context_end = std::min(mismatch_pos + 10, std::min(actual_hex.length(), expected_hex.length()));
                
                std::cout << "  Context around mismatch:" << std::endl;
                std::cout << "  Expected: ..." << expected_hex.substr(context_start, context_end - context_start) << "..." << std::endl;
                std::cout << "  Actual:   ..." << actual_hex.substr(context_start, context_end - context_start) << "..." << std::endl;
            }
            std::cout << "---" << std::endl;
        }
        
        if (all_blocks_match) {
            std::cout << "✓ All blocks match expected output!" << std::endl;
        } else {
            std::cout << "✗ Some blocks do not match expected output!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
    }
}

}