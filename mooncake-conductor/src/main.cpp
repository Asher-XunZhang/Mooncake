// main.cpp
#include "conductor_proxy.h"
#include "mooncake_store_communication_layer.h"
#include "conductor_types.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <ylt/easylog/record.hpp>

#include <atomic>

namespace mooncake_conductor {

    ProxyServerArgs parse_args(int, char**);
    namespace test {
        void test_main();
    }
}

namespace {

std::atomic<bool> g_stop_flag{false};

void signal_handler(int signal) {
    LOG(INFO) << "\nreceive signal: " << signal << ", start stoping the server...";
    g_stop_flag.store(true, std::memory_order_relaxed);
}

void StartProxyServer(const mooncake_conductor::ProxyServerArgs& config) {
    std::signal(SIGINT, signal_handler);  // Ctrl+C
    std::signal(SIGTERM, signal_handler); // kill
    // auto server = std::make_unique<mooncake_conductor::ProxyServer>(config);
    // server->start_server();
    // LOG(INFO) << "Async Starting mooncake-conductor server on " << config.host << ":" << config.port;
    // // wait 1s to let server start
    // std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // LOG(INFO) << "\n  press Ctrl+C to stop server..  ";
    // LOG(INFO) << "\n  尝试第一次读取Mooncake Store..  ";
    // mooncake_conductor::MooncakeStoreCommunicationLayer mscl{};
    // std::string s = "111";
    // auto result = mscl.GetReplicaList(s);
    // if (result.has_value()) {
    //     LOG(INFO) << "成功获取副本列表！";
    //     const auto& response = result.value();
    //     LOG(INFO) << "副本数量: " << response.replicas.size();
    // } else {
    //     LOG(ERROR) << "获取副本列表失败，错误码: " 
    //                 << mooncake::toString(result.error());
    // }

    // mooncake_conductor::test::test_main();

    // while (!g_stop_flag.load(std::memory_order_relaxed)) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // }
    using namespace mooncake_conductor;
    
    test::test_main();

    ConductorProxy::Config conductor_proxy_config{};
    // TODO: 其它参数解析
    conductor_proxy_config.listen_port = config.port;

    auto conductor_proxy = std::make_unique<ConductorProxy>(conductor_proxy_config);

    for(const auto& each : config.prefiller_instances) {
        conductor_proxy->register_node(each.first, each.second, NodeCapability::PREFILL);
    }

    for(const auto& each : config.decoder_instances) {
        conductor_proxy->register_node(each.first, each.second, NodeCapability::DECODING);
    }
    
    // for(const auto& each : config.prefiller_instances) {
    //     conductor_proxy->register_node(each.first, each.second, NodeCapability::BOTH);
    // }
    conductor_proxy->start();
    LOG(INFO) << "press Ctrl+C to stop server...";
    while (!g_stop_flag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    conductor_proxy->stop();
    LOG(INFO) << " server STOP finished. ";
}
}

int main(int argc, char* argv[]) {
    // Initialize Google Logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true; // Log to stderr by default
    
    // Initialize Google Flags
    gflags::SetUsageMessage("Mooncake Conductor - Distributed LLM Inference Proxy");
    gflags::SetVersionString("1.0");

    easylog::set_min_severity(easylog::Severity::WARN);
    mooncake_conductor::ProxyServerArgs config;
    try {
        config = mooncake_conductor::parse_args(argc, argv);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error parsing arguments: " << e.what();
        return 1;
    }
    StartProxyServer(config);

    google::ShutdownGoogleLogging();
    gflags::ShutDownCommandLineFlags();

    return 0;
}
//mooncake_conductor --port=8080 --prefiller_hosts="127.0.0.1,127.0.0.1" --prefiller_ports="8001,8002"