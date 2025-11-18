#include "conductor_proxy.h"
#include "request_handler.h"
#include "conductor_types.h"
#include "cli_parse.h"
#include "hash.h"
#include "block_serializer.h"
#include "mooncake_store_communication_layer.h"
#include "adapter_factory.h"

#include <nlohmann/json.hpp>
#include <glog/logging.h>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_http/coro_http_server.hpp>
#include <ylt/easylog/record.hpp>

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>

namespace mooncake_conductor {

namespace nl = nlohmann;

ProxyServer::ProxyServer(const ProxyServerArgs& config)
    : port_(config.port),
      host_(config.host),
      http_server_(std::make_unique<coro_http::coro_http_server>(4, config.port)),
      request_handler_(std::make_unique<RequestHandler>(config, "12", "34")) {
    init_http_server();
}

ProxyServer::~ProxyServer() {
    http_server_->stop();
}

void ProxyServer::init_http_server() {
//    curl -X POST http://localhost:8000/v1/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "prompt": "The quick brown fox jumps over the lazy dog",
//            "max_tokens": 16
//          }'

//  Or for chat completions:

//    curl -X POST http://localhost:8000/v1/chat/completions \
//      -H "Content-Type: application/json" \
//      -d '{
//            "model": "your-model",
//            "messages": [{"role": "user", "content": "Hello!"}],
//            "max_tokens": 16
//          }'
    using namespace coro_http;

    http_server_->set_http_handler<POST>(
        "/v1/completions", [&](coro_http_request& req, coro_http_response& resp) {
            auto request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");
            LOG(INFO) << "reievce request /v1/completions";
            auto result = request_handler_->handleRequest(request_body);
            // TODO 增加对于result解析等操作
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });

    http_server_->set_http_handler<POST>(
        "/v1/chat/completions",
        [&](coro_http_request& req, coro_http_response& resp) {
            const auto& request_body = req.get_queries();
            resp.add_header("Content-Type", "application/json");
            LOG(INFO) << "reievce request /v1/chat/completions";
            std::string result = request_handler_->handleRequest(request_body);
            if (result.size()) {
                resp.set_status_and_content(status_type::ok, std::move(result));
            } else {
                resp.set_status_and_content(status_type::internal_server_error,
                                            "Failed to handle request.");
            }
        });
}

void ProxyServer::start_server() {
    http_server_->async_start();
}

void ProxyServer::stop_server() {
    http_server_->stop();
}

}  // namespace mooncake_conductor


namespace mooncake_conductor {

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

void test_serializer() {
    try {
        BlockSerializer serializer;
        
        std::vector<int64_t> token_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        size_t block_size = 5;
        
        auto serialized_blocks = serializer.serialize_blocks(token_ids, block_size);
        
        std::cout << "成功序列化 " << serialized_blocks.size() << " 个数据块\n";
        
        for (size_t i = 0; i < serialized_blocks.size(); ++i) {
            const auto& block = serialized_blocks[i];
            std::cout << "数据块 " << i + 1 << ": " << block.size() << " 字节\n";
            
            std::string hex_str = BlockSerializer::to_hex(block);
            std::cout << "十六进制: " << hex_str << "\n";
            
            // 验证哈希
            auto hash_value = sha256(block);
            std::cout << "SHA256哈希: " << BlockSerializer::to_hex(hash_value) << "\n";
            std::cout << "---\n";
        }
        
        std::cout << "✓ 序列化完成n";
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
    }
}

void test_api_endpoint_adapter() {
    // 1. Verify adapter registration
    auto adapter = EndpointAdapterFactory::createAdapter("vllm");
    assert(adapter && "VLLM adapter should be registered");
    std::cout << "[TEST] Adapter created successfully" << std::endl;

    const std::string base_url = "http://localhost:8000";

    // 2. Verify health endpoint
    std::string health_ep = adapter->buildHealthEndpoint(base_url);
    assert(health_ep == "http://localhost:8000/health" && "Health endpoint mismatch");
    
    auto health_req = adapter->createHealthRequest(health_ep);
    assert(health_req.url == "http://localhost:8000/health" && "Health URL mismatch");
    assert(health_req.method == "GET" && "Health method mismatch");
    std::cout << "[TEST] Health endpoint verified" << std::endl;

    // 3. Verify health response parsing
    std::string healthy_resp = R"({"status": "healthy", "version": "0.3.2"})";
    assert(adapter->parseHealthResponse(healthy_resp) && "Should parse as healthy");
    
    std::string unhealthy_resp = R"({"status": "unhealthy"})";
    assert(!adapter->parseHealthResponse(unhealthy_resp) && "Should parse as unhealthy");
    
    std::string healthy_resp_alt = R"({"healthy": true})";
    assert(adapter->parseHealthResponse(healthy_resp_alt) && "Should parse alternative healthy format");
    std::cout << "[TEST] Health response parsing verified" << std::endl;

    // 4. Verify tokenization request
    std::string tokenize_ep = adapter->buildTokenizeEndpoint(base_url);
    assert(tokenize_ep == "http://localhost:8000/v1/tokenize" && "Tokenize endpoint mismatch");
    
    auto tokenize_req = adapter->createTokenizationRequest("Hello, vLLM!", tokenize_ep);
    assert(tokenize_req.url == tokenize_ep && "Tokenize URL mismatch");
    assert(tokenize_req.method == "POST" && "Tokenize method mismatch");
    assert(tokenize_req.headers.at("Content-Type") == "application/json" && "Content-Type mismatch");
    assert(tokenize_req.headers.at("Accept") == "application/json" && "Accept header mismatch");
    
    // Verify request body using nlohmann/json
    auto req_body = nl::json::parse(tokenize_req.body);
    assert(req_body["text"] == "Hello, vLLM!" && "Request text mismatch");
    assert(req_body["add_special_tokens"] == false && "Special tokens flag mismatch");
    std::cout << "[TEST] Tokenization request verified" << std::endl;

    // 5. Verify tokenization response
    std::string tokenize_resp = R"({
        "tokens": [1, 15043, 1917, 2],
        "model": "meta-llama/Llama-2-7b-chat-hf",
        "truncated": false
    })";
    
    auto tokenize_result = adapter->parseTokenizationResponse(tokenize_resp);
    std::vector<int> expected_vec{1, 15043, 1917, 2};
    assert(tokenize_result.token_ids == expected_vec && "Token IDs mismatch");
    assert(tokenize_result.token_count == 4 && "Token count mismatch");
    assert(tokenize_result.model_name == "meta-llama/Llama-2-7b-chat-hf" && "Model name mismatch");
    assert(!tokenize_result.truncated && "Truncated flag mismatch");
    assert(tokenize_result.error_message.empty() && "Should have no error");
    std::cout << "[TEST] Tokenization response verified" << std::endl;

    // 6. Verify config endpoint
    std::string config_ep = adapter->buildConfigEndpoint(base_url);
    assert(config_ep == "http://localhost:8000/v1/models" && "Config endpoint mismatch");
    
    auto config_req = adapter->createConfigRequest(config_ep);
    assert(config_req.url == config_ep && "Config URL mismatch");
    assert(config_req.method == "GET" && "Config method mismatch");
    std::cout << "[TEST] Config request verified" << std::endl;

    // 7. Verify config response (vLLM format)
    std::string config_resp = R"({
        "data": [{
            "id": "meta-llama/Llama-2-7b-chat-hf",
            "max_model_len": 4096,
            "dtype": "float16",
            "block_size": 16
        }]
    })";
    
    auto engine_config = adapter->parseConfigResponse(config_resp);
    assert(engine_config.model_name == "meta-llama/Llama-2-7b-chat-hf" && "Model name mismatch");
    assert(engine_config.max_sequence_length == 4096 && "Max sequence length mismatch");
    assert(engine_config.dtype == "float16" && "DType mismatch");
    assert(engine_config.block_size == 16 && "Block size mismatch");
    std::cout << "[TEST] Config response verified" << std::endl;

    // 8. Verify Prometheus metrics parsing
    std::string prometheus_metrics = R"(
# HELP vllm:gpu_utilization GPU utilization
# TYPE vllm:gpu_utilization gauge
vllm:gpu_utilization{device="0"} 75.5
)";
    
    auto metrics_req = adapter->createMetricsRequest(adapter->buildMetricsEndpoint(base_url));
    auto metrics_result = adapter->parseMetricsResponse(prometheus_metrics);
    assert(std::abs(metrics_result.gpu_utilization - 0.755) < 1e-6 && "GPU utilization parsing failed");
    assert(metrics_result.is_healthy && "Should be healthy with valid metrics");
    std::cout << "[TEST] Prometheus metrics verified" << std::endl;

    std::cout << "\n[SUCCESS] All adapter tests passed!" << std::endl;
    internal::AdapterInitializer::cleanup();
}

void test_main() {
    verify_none_hash();
    std::cout << std::endl;
    run_consistency_test();
    std::cout << std::endl;
    test_serializer();
    std::cout << std::endl;
    test_api_endpoint_adapter();
    std::cout << std::endl;
}


}


void signal_handler(int signal) {
    LOG(INFO) << "\nreceive signal: " << signal << ", start stoping the server...";
    g_stop_flag.store(true);
}

void StartProxyServer(const mooncake_conductor::ProxyServerArgs& config) {
    std::signal(SIGINT, signal_handler);  // Ctrl+C
    std::signal(SIGTERM, signal_handler); // kill
    auto server = std::make_unique<mooncake_conductor::ProxyServer>(config);
    server->start_server();
    LOG(INFO) << "Async Starting mooncake-conductor server on " << config.host << ":" << config.port;
    // wait 1s to let server start
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    LOG(INFO) << "\n  press Ctrl+C to stop server..  ";
    LOG(INFO) << "\n  尝试第一次读取Mooncake Store..  ";
    mooncake_conductor::MooncakeStoreCommunicationLayer mscl{};
    std::string s = "111";
    auto result = mscl.GetReplicaList(s);
    if (result.has_value()) {
        std::cout << "成功获取副本列表！" << std::endl;
        const auto& response = result.value();
        std::cout << "副本数量: " << response.replicas.size() << std::endl;
    } else {
        std::cout << "获取副本列表失败，错误码: " 
                    << mooncake::toString(result.error()) << std::endl;
    }

    mooncake_conductor::test_main();

    while (!g_stop_flag.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG(INFO) << " server STOP finished. ";
}

int main(int argc, char* argv[]) {
    easylog::set_min_severity(easylog::Severity::WARN);
    mooncake_conductor::ProxyServerArgs config;
    try {
        config = parse_args(argc, argv);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error parsing arguments: " << e.what();
        return 1;
    }
    StartProxyServer(config);
    
    

    // if(mscl.IsConnected()) {
    //     LOG(INFO) << "Connductor has connected to Store";
    // }



    return 0;
}
//mooncake_conductor --port=8080 --prefiller_hosts="127.0.0.1,127.0.0.1" --prefiller_ports="8001,8002"
