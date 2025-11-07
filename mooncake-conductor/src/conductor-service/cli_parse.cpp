#include "cli_parse.h"
#include "conductor_types.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <vector>
#include <string>


DEFINE_int32(port, 8000, "Port number");
DEFINE_string(host, "localhost", "Host address");
DEFINE_string(prefiller_hosts, "localhost", "Comma-separated list of prefiller hosts");
DEFINE_string(prefiller_ports, "8001", "Comma-separated list of prefiller ports");
DEFINE_string(decoder_hosts, "localhost", "Comma-separated list of decoder hosts");
DEFINE_string(decoder_ports, "8002", "Comma-separated list of decoder ports");
DEFINE_int32(max_retries, 3, "Maximum number of retries for HTTP requests");
DEFINE_double(retry_delay, 0.001, "Base delay (seconds) for exponential backoff retries");


std::vector<std::string> split_str_list(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);
    
    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start));
    return tokens;
}


std::vector<int> parse_int_list(const std::string& str, char delimiter) {
    std::vector<int> result;
    auto tokens = split_str_list(str, delimiter);
    for (const auto& token : tokens) {
        result.push_back(std::stoi(token));
    }
    return result;
}


mooncake_conductor::ProxyServerArgs parse_args(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    
    mooncake_conductor::ProxyServerArgs args;
    args.port = FLAGS_port;
    args.host = FLAGS_host;
    args.max_retries = FLAGS_max_retries;
    args.retry_delay = FLAGS_retry_delay;
    
    args.prefiller_hosts = split_str_list(FLAGS_prefiller_hosts, ',');
    args.prefiller_ports = parse_int_list(FLAGS_prefiller_ports, ',');
    args.decoder_hosts = split_str_list(FLAGS_decoder_hosts, ',');
    args.decoder_ports = parse_int_list(FLAGS_decoder_ports, ',');
    
    // validate host and port number match
    if (args.prefiller_hosts.size() != args.prefiller_ports.size()) {
        throw std::invalid_argument(
            "Number of prefiller hosts must match number of prefiller ports");
    }
    
    if (args.decoder_hosts.size() != args.decoder_ports.size()) {
        throw std::invalid_argument(
            "Number of decoder hosts must match number of decoder ports");
    }
    
    for (size_t i = 0; i < args.prefiller_hosts.size(); ++i) {
        args.prefiller_instances.emplace_back(args.prefiller_hosts[i], args.prefiller_ports[i]);
    }
    
    for (size_t i = 0; i < args.decoder_hosts.size(); ++i) {
        args.decoder_instances.emplace_back(args.decoder_hosts[i], args.decoder_ports[i]);
    }
    LOG(INFO) << "Conductor server port: " << args.port << ", host: " << args.host
              << ", prefiller hosts: " << FLAGS_prefiller_hosts
              << ", prefiller ports: " << FLAGS_prefiller_ports
              << ", decoder hosts: " << FLAGS_decoder_hosts
              << ", decoder ports: " << FLAGS_decoder_ports;
    
    return args;
}
