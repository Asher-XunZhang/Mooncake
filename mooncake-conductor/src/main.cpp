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
        void test_main(const ProxyServerArgs&);
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

    using namespace mooncake_conductor;
    
    // test::test_main(config); // TODO: 测试函数，需更改为UT

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
    
    for(const auto& each : config.both_instances) {
        conductor_proxy->register_node(each.first, each.second, NodeCapability::BOTH);
    }

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
//mooncake_conductor --port=8180 --both_hosts="127.0.0.1" --both_ports="8100" --mooncake_store_port=50098 --mooncake_store_host="10.175.119.75"