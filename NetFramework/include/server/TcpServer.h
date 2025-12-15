#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <deque>
#include "../IO/IOFactory.h"
#include "../base/HeartbeatThreadPool.h"

/**
 * @brief 通用TCP服务端，支持事件回调机制
 *        可作为Echo、聊天室、游戏等多种场景的基础网络服务端
 */
class TcpServer {
public:
    // 连接建立回调：参数为新连接的fd
    using OnConnectCallback = std::function<void(int)>;
    // 消息到达回调：参数为客户端fd和收到的数据
    using OnMessageCallback = std::function<void(int, const std::string&)>;
    // 连接关闭回调：参数为关闭的fd
    using OnCloseCallback = std::function<void(int)>;

    /**
     * @brief 构造函数
     * @param ip      监听IP
     * @param port    监听端口
     * @param io_type IO多路复用类型（SELECT/POLL/EPOLL）
     */
    TcpServer(const std::string& ip, int port, IOMultiplexer::IOType io_type);
    ~TcpServer();

    // 设置连接建立回调
    void setOnConnect(OnConnectCallback cb);
    // 设置消息到达回调
    void setOnMessage(OnMessageCallback cb);
    // 设置连接关闭回调
    void setOnClose(OnCloseCallback cb);

    // 启动服务器
    virtual bool start();
    // 停止服务器
    virtual void stop();
    // 数据接收（应用层可重写）
    virtual void onDataReceived(int clientFd, const char* data, size_t len);
    // 客户端连接（应用层可重写）
    virtual void onClientConnected(int clientFd);
    // 客户端断开（应用层可重写）
    virtual void onClientDisconnected(int clientFd);

    // 获取当前IO类型
    IOMultiplexer::IOType type() const;

protected:
    // 处理新连接
    void handleAccept();
    // 处理客户端读事件
    virtual void handleRead(int client_fd); // 声明为virtual
    // 处理客户端关闭
    void handleClose(int client_fd);

    void run();

    // 成员变量
    int m_socket;  // 监听socket
    int m_port;
    std::string m_ip;
    std::atomic<bool> m_running;
    std::unique_ptr<IOMultiplexer> m_io;
    std::unordered_map<int, int> m_clients;   // 客户端fd映射
    const int BUFFER_SIZE = 4096;
    std::mutex m_mutex;
    IOFactory::PerformanceStats m_stats;
    std::atomic<int> m_current_concurrent{0};

    // 事件回调
    OnConnectCallback m_onConnect;
    OnMessageCallback m_onMessage;
    OnCloseCallback m_onClose;

    std::unique_ptr<HeartbeatThreadPool> m_heartbeatPool; // 心跳线程池
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_lastActive; // 记录每个连接最后活跃时间
    int m_heartbeatTimeout = 60; // 心跳超时时间（秒）
    bool m_heartbeatEnabled = true; // 心跳开关，默认启用
    virtual void sendHeartbeat(int client_fd); // 发送心跳包（可被子类重写）
    void checkHeartbeats(); // 检查心跳超时

public:
    // 智能心跳控制
    void setHeartbeatEnabled(bool enabled) { m_heartbeatEnabled = enabled; }

protected:
    // 子类可用的发送接口
    void sendBusinessData(int client_fd, const std::string& data);

private:
    // 客户端发送缓冲区（确保非阻塞发送完整）
    std::unordered_map<int, std::deque<std::vector<char>>> m_sendBuffers;
    // 发送锁（需确保初始化线程安全）
    std::unordered_map<int, std::unique_ptr<std::mutex>> m_sendMutexes;
    // 保护 m_sendMutexes 自身的锁（关键修复）
    std::mutex m_mutexForLocks;

    std::mutex& getSendMutex(int client_fd);

    void sendData(int client_fd, const char* data, size_t len, bool is_heartbeat);

    void flushSendBuffer(int client_fd);
};
#endif // TCP_SERVER_H
