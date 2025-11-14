#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <cassert>


namespace mooncake_conductor {

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byteString, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t b : bytes) {
        ss << std::setw(2) << static_cast<int>(b);
    }
    return ss.str();
}

std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256_CTX sha256_ctx;
    SHA256_Init(&sha256_ctx);
    SHA256_Update(&sha256_ctx, data.data(), data.size());
    SHA256_Final(hash.data(), &sha256_ctx);
    return hash;
}

const std::vector<uint8_t> NONE_HASH(32, 0);

struct TestCase {
    std::string description;
    std::string serialized_hex;
    std::string expected_hash;
};

void run_consistency_test() {
    std::vector<TestCase> test_cases = {
        {
            "区块1: tokens [1,2,3,4,5]",
            "80059534000000000000004320000000000000000000000000000000000000000000000000000000000000000094284b014b024b034b044b0574944e87942e",
            "62a05fac03f5470c9e1e66b43447b1cb321ec98e3afb509f531d0781dde12d52"
        },
        {
            "区块2: tokens [6,7,8,9,10]", 
            "8005953400000000000000432062a05fac03f5470c9e1e66b43447b1cb321ec98e3afb509f531d0781dde12d5294284b064b074b084b094b0a74944e87942e",
            "3b3f53cad691850fca841706606c71b1320e0515cca38dec3b48f3e3722052be"
        }
    };
    
    std::cout << "开始哈希一致性测试...\n" << std::endl;
    
    bool all_passed = true;
    
    for (const auto& test_case : test_cases) {
        std::cout << "测试: " << test_case.description << std::endl;
        std::cout << "序列化数据长度: " << test_case.serialized_hex.length() / 2 << " 字节" << std::endl;
        
        auto serialized_data = hex_to_bytes(test_case.serialized_hex);
        
        auto hash_result = sha256(serialized_data);
        auto hash_hex = bytes_to_hex(hash_result);
        
        std::cout << "计算哈希: " << hash_hex << std::endl;
        std::cout << "预期哈希: " << test_case.expected_hash << std::endl;
        
        bool passed = (hash_hex == test_case.expected_hash);
        std::cout << "结果: " << (passed ? "通过" : "失败") << std::endl;
        std::cout << "---" << std::endl;
        
        if (!passed) {
            all_passed = false;
        }
    }
    
    std::cout << "总体结果: " << (all_passed ? "所有测试通过!" : "有测试失败!") << std::endl;

    assert(all_passed);
}

void verify_none_hash() {
    std::cout << "验证NONE_HASH值:" << std::endl;
    std::cout << "NONE_HASH (十六进制): " << bytes_to_hex(NONE_HASH) << std::endl;
    std::cout << "NONE_HASH 长度: " << NONE_HASH.size() << " 字节" << std::endl;
    
    bool is_all_zero = true;
    for (auto byte : NONE_HASH) {
        if (byte != 0) {
            is_all_zero = false;
            break;
        }
    }
    std::cout << "NONE_HASH 全为零: " << (is_all_zero ? "是" : "否") << std::endl;
}

}