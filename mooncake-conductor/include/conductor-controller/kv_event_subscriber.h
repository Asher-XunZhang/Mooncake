// kv_event_subscriber.h
#pragma once

#include <zmq.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>
#include <memory>
#include <cstdint>

namespace mooncake_conductor {

/**
 * @brief ZMQ事件订阅者，用于与Python ZmqEventPublisher通信
 * 
 * 支持实时事件订阅和可选的重放机制，线程安全。
 */
class ZmqEventSubscriber {
public:
    /// 配置参数结构体
    struct Config {
        std::vector<std::string> endpoints;          ///< 订阅地址列表
        std::optional<std::string> replay_endpoint;   ///< 重放请求地址（可选）
        std::string topic = "";                       ///< 订阅主题
        int recv_timeout_ms = 100;                    ///< 接收超时（毫秒）
        size_t max_queue_size = 10000;                ///< 最大队列大小
    };

    /// 接收到的消息结构
    struct EventMessage {
        std::string topic;                    ///< 消息主题
        uint64_t sequence;                   ///< 序列号
        std::vector<uint8_t> payload;        ///< MessagePack编码的有效载荷
        
        EventMessage(std::string t, uint64_t seq, std::vector<uint8_t> p)
            : topic(std::move(t)), sequence(seq), payload(std::move(p)) {}
    };

    explicit ZmqEventSubscriber(const Config& config);
    ~ZmqEventSubscriber();

    // 禁止拷贝和赋值
    ZmqEventSubscriber(const ZmqEventSubscriber&) = delete;
    ZmqEventSubscriber& operator=(const ZmqEventSubscriber&) = delete;

    /**
     * @brief 请求重放从指定序列号开始的事件
     * @param start_seq 起始序列号
     * @return 成功返回true
     * 
     * @note 需要配置replay_endpoint才有效
     */
    bool request_replay(uint64_t start_seq);

    /**
     * @brief 获取下一个事件（阻塞）
     * @param message 输出参数，存储接收到的事件
     * @param timeout_ms 超时时间（毫秒），-1表示无限等待
     * @return 成功获取事件返回true，超时或关闭返回false
     */
    bool get_next_event(EventMessage& message, int timeout_ms = -1);

    /**
     * @brief 非阻塞方式尝试获取下一个事件
     * @param message 输出参数，存储接收到的事件
     * @return 成功获取事件返回true，无事件返回false
     */
    bool try_get_event(EventMessage& message);

    /**
     * @brief 关闭订阅者并释放资源
     */
    void shutdown();

    /**
     * @brief 检查是否正在运行
     */
    bool is_running() const { return running_.load(); }

    /**
     * @brief 获取当前队列中的事件数量
     */
    size_t queue_size() const;

private:
    // 端点端口偏移（与Python端保持一致）
    static std::string offset_endpoint_port(const std::string& endpoint, int data_parallel_rank);
    
    // 接收线程主循环
    void receive_loop();
    
    // 处理重放响应
    bool process_replay_response();

    Config config_;
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> subscriber_;
    std::unique_ptr<zmq::socket_t> replay_socket_;
    
    std::thread recv_thread_;
    std::atomic<bool> running_{false};
    
    // 线程安全的事件队列
    std::queue<EventMessage> event_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable event_cv_;
};

} // end namespace mooncake_conductor