// cli_parse.cpp
#include "conductor_types.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <charconv>


DEFINE_int32(port, 8000, "Port number");
DEFINE_string(host, "localhost", "Host address");
DEFINE_int32(mooncake_store_port, 50051, "Mooncake Store Port number");
DEFINE_string(mooncake_store_host, "localhost", "Mooncake Store Host address");
DEFINE_string(prefiller_hosts, "", "Comma-separated list of prefiller hosts");
DEFINE_string(prefiller_ports, "", "Comma-separated list of prefiller ports");
DEFINE_string(decoder_hosts, "", "Comma-separated list of decoder hosts");
DEFINE_string(decoder_ports, "", "Comma-separated list of decoder ports");
DEFINE_string(both_hosts, "", "Comma-separated list of prefiller hosts");
DEFINE_string(both_ports, "", "Comma-separated list of prefiller ports");
DEFINE_int32(max_retries, 3, "Maximum number of retries for HTTP requests");
DEFINE_double(retry_delay, 0.001, "Base delay (seconds) for exponential backoff retries");

namespace {

std::vector<std::string> split_str_list(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    
    // 处理空字符串输入
    if (str.empty()) {
        return result; // 返回空向量
    }
    
    std::istringstream ss(str);
    std::string token;
    
    // 使用getline分割字符串，保留空字段
    while (std::getline(ss, token, delimiter)) {
        result.push_back(token);
    }
    
    // 特殊情况：如果字符串以分隔符结尾，需要添加一个空字符串
    if (!str.empty() && str.back() == delimiter) {
        result.push_back("");
    }
    
    return result;
}


std::vector<int> parse_int_list(const std::string& str, char delimiter) {
    std::vector<int> result;
    
    // 处理空字符串输入
    if (str.empty()) {
        return result; // 返回空向量
    }
    
    std::istringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        // 跳过空token（连续分隔符情况）
        if (token.empty()) {
            continue;
        }
        
        // 使用C++17的from_chars进行高效转换（推荐方式）
        int value;
        auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
        
        if (ec == std::errc()) {
            result.push_back(value);
        } else {
            // 转换失败，抛出异常或处理错误
            throw std::invalid_argument("无法将 '" + token + "' 转换为整数");
        }
    }
    
    return result;
}

}

namespace mooncake_conductor {

ProxyServerArgs parse_args(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    ProxyServerArgs args;
    args.port = FLAGS_port;
    args.host = FLAGS_host;
    args.mooncake_store_port = FLAGS_mooncake_store_port;
    args.mooncake_store_host = FLAGS_mooncake_store_host;
    args.max_retries = FLAGS_max_retries;
    args.retry_delay = FLAGS_retry_delay;
    
    args.prefiller_hosts = split_str_list(FLAGS_prefiller_hosts, ',');
    args.prefiller_ports = parse_int_list(FLAGS_prefiller_ports, ',');
    args.decoder_hosts = split_str_list(FLAGS_decoder_hosts, ',');
    args.decoder_ports = parse_int_list(FLAGS_decoder_ports, ',');
    args.both_hosts = split_str_list(FLAGS_both_hosts, ',');
    args.both_ports = parse_int_list(FLAGS_both_ports, ',');

    
    // validate host and port number match
    if (args.prefiller_hosts.size() != args.prefiller_ports.size()) {
        throw std::invalid_argument(
            "Number of prefiller hosts must match number of prefiller ports");
    }
    
    if (args.decoder_hosts.size() != args.decoder_ports.size()) {
        throw std::invalid_argument(
            "Number of decoder hosts must match number of decoder ports");
    }

    if (args.both_hosts.size() != args.both_ports.size()) {
        throw std::invalid_argument(
            "Number of both hosts must match number of both ports");
    }
    
    for (size_t i = 0; i < args.prefiller_hosts.size(); ++i) {
        args.prefiller_instances.emplace_back(args.prefiller_hosts[i], args.prefiller_ports[i]);
    }
    
    for (size_t i = 0; i < args.decoder_hosts.size(); ++i) {
        args.decoder_instances.emplace_back(args.decoder_hosts[i], args.decoder_ports[i]);
    }

    for (size_t i = 0; i < args.both_hosts.size(); ++i) {
        args.both_instances.emplace_back(args.both_hosts[i], args.both_ports[i]);
    }

    LOG(INFO) << "Conductor server port: " << args.port << ", host: " << args.host
              << ", prefiller hosts: " << FLAGS_prefiller_hosts
              << ", prefiller ports: " << FLAGS_prefiller_ports
              << ", decoder hosts: " << FLAGS_decoder_hosts
              << ", decoder ports: " << FLAGS_decoder_ports
              << ", both hosts: " << FLAGS_both_hosts
              << ", both ports: " << FLAGS_both_ports;
    
    return args;
}

}