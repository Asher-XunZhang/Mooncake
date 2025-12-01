// kv_event_subscriber.cpp
#include "kv_event_subscriber.h"

#include <zmq_addon.hpp>

#include <iostream>
#include <chrono>
#include <cstring>
#include <algorithm>

namespace {

// 平台相关的字节序转换头文件
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifndef be64toh
        #define be64toh(x) _byteswap_uint64(x)
    #endif
    #ifndef htobe64
        #define htobe64(x) _byteswap_uint64(x)
    #endif
#else
    #include <arpa/inet.h>
#endif

}

namespace mooncake_conductor {

ZmqEventSubscriber::ZmqEventSubscriber(const Config& config)
    : config_(config)
    , context_(1)  // 单I/O线程
    , running_(true) {
    
    // 参数验证
    if (config_.endpoints.empty()) {
        throw std::invalid_argument("At least one endpoint must be specified");
    }

    // 创建SUB套接字
    subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
    subscriber_->set(zmq::sockopt::linger, 0);  // 立即关闭
    
    // 连接到所有端点
    for (const auto& endpoint : config_.endpoints) {
        try {
            subscriber_->connect(endpoint);
            std::cout << "Connected to endpoint: " << endpoint << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "Failed to connect to " << endpoint << ": " << e.what() << std::endl;
            throw;
        }
    }
    
    // 设置订阅主题
    subscriber_->set(zmq::sockopt::subscribe, 
                    config_.topic.empty() ? "" : config_.topic);
    
    // 创建重放套接字（如果配置了重放端点）
    if (config_.replay_endpoint) {
        try {
            replay_socket_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REQ);
            replay_socket_->set(zmq::sockopt::linger, 0);
            replay_socket_->set(zmq::sockopt::rcvtimeo, 5000);  // 5秒超时
            replay_socket_->connect(*config_.replay_endpoint);
            std::cout << "Connected to replay endpoint: " << *config_.replay_endpoint << std::endl;
        } catch (const zmq::error_t& e) {
            std::cerr << "Failed to connect to replay endpoint: " << e.what() << std::endl;
            replay_socket_.reset();
        }
    }
    
    // 启动接收线程
    recv_thread_ = std::thread(&ZmqEventSubscriber::receive_loop, this);
    std::cout << "ZmqEventSubscriber started with " << config_.endpoints.size() << " endpoints" << std::endl;
}

ZmqEventSubscriber::~ZmqEventSubscriber() {
    shutdown();
}

void ZmqEventSubscriber::shutdown() {
    if (!running_.exchange(false)) {
        return;  // 已经关闭
    }
    
    // 通知接收线程退出
    event_cv_.notify_all();
    
    // 等待接收线程结束
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    
    // 关闭套接字
    if (subscriber_) {
        subscriber_->close();
    }
    if (replay_socket_) {
        replay_socket_->close();
    }
    
    // 清空队列
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<EventMessage> empty;
        std::swap(event_queue_, empty);
    }
    
    context_.close();
    std::cout << "ZmqEventSubscriber shutdown completed" << std::endl;
}

bool ZmqEventSubscriber::request_replay(uint64_t start_seq) {
    if (!replay_socket_) {
        std::cerr << "Replay not configured: no replay endpoint" << std::endl;
        return false;
    }
    
    try {
        // 发送重放请求：[start_seq_bytes]
        zmq::message_t request_msg(sizeof(uint64_t));
        uint64_t network_seq = htobe64(start_seq);
        std::memcpy(request_msg.data(), &network_seq, sizeof(uint64_t));
        
        if (!replay_socket_->send(request_msg, zmq::send_flags::none)) {
            std::cerr << "Failed to send replay request" << std::endl;
            return false;
        }
        
        // 处理重放响应
        return process_replay_response();
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Replay request failed: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqEventSubscriber::get_next_event(EventMessage& message, int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    if (timeout_ms >= 0) {
        // 带超时的等待
        auto timeout = std::chrono::milliseconds(timeout_ms);
        if (!event_cv_.wait_for(lock, timeout, 
                               [this] { return !event_queue_.empty() || !running_; })) {
            return false;  // 超时
        }
    } else {
        // 无限等待
        event_cv_.wait(lock, [this] { return !event_queue_.empty() || !running_; });
    }
    
    if (!running_ || event_queue_.empty()) {
        return false;
    }
    
    message = std::move(event_queue_.front());
    event_queue_.pop();
    return true;
}

bool ZmqEventSubscriber::try_get_event(EventMessage& message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (event_queue_.empty()) {
        return false;
    }
    
    message = std::move(event_queue_.front());
    event_queue_.pop();
    return true;
}

size_t ZmqEventSubscriber::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return event_queue_.size();
}

std::string ZmqEventSubscriber::offset_endpoint_port(const std::string& endpoint, int data_parallel_rank) {
    if (data_parallel_rank == 0 || endpoint.empty()) {
        return endpoint;
    }

    if (endpoint.find("inproc://") != std::string::npos) {
        return endpoint + "_dp" + std::to_string(data_parallel_rank);
    }
    
    if (endpoint.find("tcp://") != std::string::npos) {
        auto colon_pos = endpoint.rfind(':');
        if (colon_pos != std::string::npos) {
            std::string base_addr = endpoint.substr(0, colon_pos);
            int base_port = std::stoi(endpoint.substr(colon_pos + 1));
            int new_port = base_port + data_parallel_rank;
            return base_addr + ":" + std::to_string(new_port);
        }
    }
    
    throw std::invalid_argument("Invalid endpoint: must contain 'inproc://' or 'tcp://'");
}

void ZmqEventSubscriber::receive_loop() {
    zmq::pollitem_t poll_item = {static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0};
    
    while (running_.load()) {
        try {
            // 非阻塞轮询
            int rc = zmq_poll(&poll_item, 1, config_.recv_timeout_ms);
            
            if (rc == 0) {
                continue;  // 超时，继续循环
            }
            
            if (rc < 0) {
                if (errno == ETERM) {
                    break;  // 上下文终止
                }
                std::cerr << "Poll error: " << zmq_strerror(errno) << std::endl;
                continue;
            }
            
            if (poll_item.revents & ZMQ_POLLIN) {
                // 接收多部分消息：[topic, sequence, payload]
                std::vector<zmq::message_t> messages;
                zmq::recv_result_t result = zmq::recv_multipart(*subscriber_, 
                    std::back_inserter(messages), zmq::recv_flags::dontwait);
                
                if (result && *result == 3) {
                    // 解析消息
                    std::string topic(messages[0].data<char>(), messages[0].size());
                    
                    if (messages[1].size() == sizeof(uint64_t)) {
                        uint64_t seq_be;
                        std::memcpy(&seq_be, messages[1].data(), sizeof(uint64_t));
                        uint64_t sequence = be64toh(seq_be);
                        
                        // 复制payload数据
                        const uint8_t* payload_data = static_cast<const uint8_t*>(messages[2].data());
                        std::vector<uint8_t> payload(payload_data, 
                                                   payload_data + messages[2].size());
                        
                        // 创建事件消息
                        EventMessage event(topic, sequence, std::move(payload));
                        
                        // 添加到队列
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            if (event_queue_.size() < config_.max_queue_size) {
                                event_queue_.push(std::move(event));
                            } else {
                                std::cerr << "Event queue full, dropping message" << std::endl;
                            }
                        }
                        event_cv_.notify_one();
                        
                    } else {
                        std::cerr << "Invalid sequence size: " << messages[1].size() << std::endl;
                    }
                } else {
                    std::cerr << "Invalid message format, expected 3 parts" << std::endl;
                }
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                break;  // 正常退出
            }
            std::cerr << "Receive error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 错误时稍作休息
        }
    }
    
    std::cout << "Receive thread exiting" << std::endl;
}

bool ZmqEventSubscriber::process_replay_response() {
    if (!replay_socket_) {
        return false;
    }
    
    try {
        zmq::message_t response;
        auto result = replay_socket_->recv(response, zmq::recv_flags::none);
        
        if (!result) {
            std::cerr << "Failed to receive replay response" << std::endl;
            return false;
        }
        
        // 简化处理：假设重放响应是单个消息包
        // 实际实现需要根据Python端的流式响应进行调整
        std::cout << "Replay response received, size: " << response.size() << " bytes" << std::endl;
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "Replay response error: " << e.what() << std::endl;
        return false;
    }
}

} // end namespace mooncake_conductor