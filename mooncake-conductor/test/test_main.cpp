// test_main.cpp
#include "request_handler.h"
#include "conductor_types.h"
#include "hash.h"
#include "block_serializer.h"
#include "mooncake_store_communication_layer.h"
#include "prefill_planner.h"
#include "replica.h"
#include "allocator.h"
#include "adapter_factory.h"

#include <nlohmann/json.hpp>
#include <glog/logging.h>

#include <memory>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cassert>


namespace mooncake_conductor::test {

void test_mooncake_store_communication_layer() {
    LOG(INFO) << "尝试第一次读取Mooncake Store..  ";
    auto mscl = std::make_unique<MooncakeStoreCommunicationLayer>();
    std::string s = "111";
    auto result = mscl->GetReplicaList(s);
    if (result.has_value()) {
        LOG(INFO) << "成功获取副本列表！";
        const auto& response = result.value();
        LOG(INFO) << "副本数量: " << response.replicas.size();
    } else {
        LOG(ERROR) << "获取副本列表失败，错误码: " 
                    << mooncake::toString(result.error());
    }
}


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
    
    LOG(INFO) << "开始哈希一致性测试...\n";
    
    bool all_passed = true;
    
    for (const auto& test_case : test_cases) {
        LOG(INFO) << "测试: " << test_case.description;
        LOG(INFO) << "序列化数据长度: " << test_case.serialized_hex.length() / 2 << " 字节";
        
        auto serialized_data = hex_to_bytes(test_case.serialized_hex);
        
        auto hash_result = sha256(serialized_data);
        auto hash_hex = bytes_to_hex(hash_result);
        
        LOG(INFO) << "计算哈希: " << hash_hex;
        LOG(INFO) << "预期哈希: " << test_case.expected_hash;
        
        bool passed = (hash_hex == test_case.expected_hash);
        LOG(INFO) << "结果: " << (passed ? "通过" : "失败");
        LOG(INFO) << "---";
        
        if (!passed) {
            all_passed = false;
        }
    }
    
    LOG(INFO) << "总体结果: " << (all_passed ? "所有测试通过!" : "有测试失败!");

    assert(all_passed);
}

void verify_none_hash() {
    LOG(INFO) << "验证NONE_HASH值:";
    LOG(INFO) << "NONE_HASH (十六进制): " << bytes_to_hex(NONE_HASH);
    LOG(INFO) << "NONE_HASH 长度: " << NONE_HASH.size() << " 字节";
    
    bool is_all_zero = true;
    for (auto byte : NONE_HASH) {
        if (byte != 0) {
            is_all_zero = false;
            break;
        }
    }
    LOG(INFO) << "NONE_HASH 全为零: " << (is_all_zero ? "是" : "否");
}

void test_serializer() {
    try {
        BlockSerializer serializer;
        
        std::vector<int64_t> token_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        size_t block_size = 5;
        
        auto serialized_blocks = serializer.serialize_blocks(token_ids, block_size);
        
        LOG(INFO) << "成功序列化 " << serialized_blocks.size() << " 个数据块";
        
        for (size_t i = 0; i < serialized_blocks.size(); ++i) {
            const auto& block = serialized_blocks[i];
            LOG(INFO) << "数据块 " << i + 1 << ": " << block.size() << " 字节";
            
            std::string hex_str = BlockSerializer::to_hex(block);
            LOG(INFO) << "十六进制: " << hex_str << "";
            
            // 验证哈希
            auto hash_value = sha256(block);
            LOG(INFO) << "SHA256哈希: " << BlockSerializer::to_hex(hash_value);
            LOG(INFO) << "---";
        }
        
        LOG(INFO) << "✓ 序列化完成n";
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "错误: " << e.what();
    }
}

void test_api_endpoint_adapter() {
    namespace nl = nlohmann;
    // verify adapter registration
    auto adapter = EndpointAdapterFactory::createAdapter("vllm");
    assert(adapter && "VLLM adapter should be registered");
    LOG(INFO) << "[TEST] Adapter created successfully";

    const std::string base_url = "http://localhost:8000";

    // verify health endpoint
    std::string health_ep = adapter->buildHealthEndpoint(base_url);
    assert(health_ep == "http://localhost:8000/health" && "Health endpoint mismatch");
    LOG(INFO) << "[TEST] Health endpoint verified";

    // verify completions endpoint
    std::string completions_ep = adapter->buildCompletionsEndpoint(base_url);
    assert(completions_ep == "http://localhost:8000/v1/completions" && "Completions endpoint mismatch");
    LOG(INFO) << "[TEST] Completions endpoint verified"; 

    // verify chat/completions endpoint
    std::string chat_completions_ep = adapter->buildChatCompletionsEndpoint(base_url);
    assert(chat_completions_ep == "http://localhost:8000/v1/chat/completions" && "Chat Completions endpoint mismatch");
    LOG(INFO) << "[TEST] Chat/Completions endpoint verified"; 

    // verify tokenization endpoint
    std::string tokenize_ep = adapter->buildTokenizeEndpoint(base_url);
    assert(tokenize_ep == "http://localhost:8000/v1/tokenize" && "Tokenize endpoint mismatch");
    LOG(INFO) << "[TEST] Tokenization endpoint verified";
    

    // verify config endpoint
    std::string config_ep = adapter->buildConfigEndpoint(base_url);
    assert(config_ep == "http://localhost:8000/v1/models" && "Config endpoint mismatch");
    LOG(INFO) << "[TEST] Config request verified";

    LOG(INFO) << "[SUCCESS] All adapter tests passed!";
    internal::AdapterInitializer::cleanup();
}

static mooncake::Replica::Descriptor make_memory_replica_desc(
    const std::string& endpoint,
    std::uint64_t size = 1024) {
    mooncake::Replica::Descriptor desc;
    desc.status = mooncake::ReplicaStatus::COMPLETE;

    mooncake::MemoryDescriptor mem;
    mooncake::AllocatedBuffer::Descriptor buf_desc;
    buf_desc.size_ = size;
    buf_desc.buffer_address_ = 0;  // not used in this test
    buf_desc.transport_endpoint_ = endpoint;

    mem.buffer_descriptors.push_back(buf_desc);
    desc.descriptor_variant = mem;
    return desc;
}

void test_prefill_planner() {
    using mooncake_conductor::PrefillPlanner;
    using mooncake_conductor::BestPrefillResult;

    LOG(INFO) << "[TEST] PrefillPlanner longest-prefix / best-node selection";

    std::vector<std::string> keys = {"k1", "k2", "k3"};

    std::vector<tl::expected<GetReplicaListResponse, ErrorCode>> results;
    results.resize(keys.size());

    /**
     * Scenario:
     * - NodeA has replicas for k1, k2
     * - NodeB has replicas for k1, k3
     * - NodeC has replicas for k1, k2, k3
     * So the best node should be NodeC with prefix "k3"
     */
    // k1
        {
        GetReplicaListResponse resp;
        resp.replicas.push_back(make_memory_replica_desc("NodeA:9000"));
        resp.replicas.push_back(make_memory_replica_desc("NodeB:9000"));
        resp.replicas.push_back(make_memory_replica_desc("NodeC:9000"));
        results[0] = resp;
    }
    
    // k2
    {
        GetReplicaListResponse resp;
        resp.replicas.push_back(make_memory_replica_desc("NodeA:9000"));
        resp.replicas.push_back(make_memory_replica_desc("NodeC:9000"));
        results[1] = resp;
    }

    // K3
    {
        GetReplicaListResponse resp;
        resp.replicas.push_back(make_memory_replica_desc("NodeC:9000"));
        results[2] = resp;
    }

    PrefillPlanner planner{};
    BestPrefillResult result = planner.find_best_prefill(keys, results);

    LOG(INFO) << "  hit: " << std::boolalpha << result.hit;
    LOG(INFO) << "  best_index: " << result.best_index;
    LOG(INFO) << "  best_key: " << result.best_key;
    LOG(INFO) << "  node_id: " << result.node_id;

    assert(result.hit && "PrefillPlanner should find a hit");
    assert(result.best_index == 2 && "Best index should be 2 (k3)");
    assert(result.best_key == "k3" && "Best key should be k3");
    assert(result.node_id == "NodeC:9000" && "Best node should be NodeC:9000");

    LOG(INFO) << "[TEST] PrefillPlanner longest-prefix / best-node selection PASSED";
}

void test_main() {
    test_mooncake_store_communication_layer();
    LOG(INFO) << "-----------------------------------------------";
    verify_none_hash();
    LOG(INFO) << "-----------------------------------------------";
    run_consistency_test();
    LOG(INFO) << "-----------------------------------------------";
    test_serializer();
    LOG(INFO) << "-----------------------------------------------";
    test_api_endpoint_adapter();
    LOG(INFO) << "-----------------------------------------------";
    test_prefill_planner();
    LOG(INFO) << "-----------------------------------------------";
}

}