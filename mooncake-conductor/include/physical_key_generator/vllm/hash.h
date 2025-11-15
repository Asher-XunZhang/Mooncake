#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <cassert>
#include <stdexcept>
#include <algorithm>

namespace mooncake_conductor {

inline const std::vector<uint8_t> NONE_HASH(32, 0);

inline std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    if (hex.empty()) return {};
    
    if (hex.length() % 2 != 0) {
        throw std::invalid_argument("十六进制字符串长度必须为偶数");
    }
    
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    
    for (size_t i = 0; i < hex.length(); i += 2) {
        char high = hex[i];
        char low = hex[i+1];
        
        uint8_t byte = 0;
        
        if (high >= '0' && high <= '9') byte = (high - '0') << 4;
        else if (high >= 'a' && high <= 'f') byte = (high - 'a' + 10) << 4;
        else if (high >= 'A' && high <= 'F') byte = (high - 'A' + 10) << 4;
        else throw std::invalid_argument("无效的十六进制字符: " + std::string(1, high));
        
        if (low >= '0' && low <= '9') byte |= (low - '0');
        else if (low >= 'a' && low <= 'f') byte |= (low - 'a' + 10);
        else if (low >= 'A' && low <= 'F') byte |= (low - 'A' + 10);
        else throw std::invalid_argument("无效的十六进制字符: " + std::string(1, low));
        
        bytes.push_back(byte);
    }
    
    return bytes;
}

inline std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    
    for (uint8_t byte : bytes) {
        result.push_back(hex_chars[byte >> 4]);
        result.push_back(hex_chars[byte & 0x0F]);
    }
    
    return result;
}

inline std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256_CTX sha256_ctx;
    
    if (!SHA256_Init(&sha256_ctx) ||
        !SHA256_Update(&sha256_ctx, data.data(), data.size()) ||
        !SHA256_Final(hash.data(), &sha256_ctx)) {
        throw std::runtime_error("SHA256计算失败");
    }
    
    return hash;
}

}