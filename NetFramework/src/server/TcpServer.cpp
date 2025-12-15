#include "../include/server/TcpServer.h"
#include "base/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <thread>
#include "base/HeartbeatThreadPool.h"
#include <chrono>
const uint32_t HEARTBEAT_MAGIC = 0xFAFBFCFD;


TcpServer::TcpServer(const std::string& ip, int port, IOMultiplexer::IOType io_type)
    : m_port(port), m_ip(ip), m_running(false), m_io(IOFactory::createIO(io_type)) {
    m_socket = -1;
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::setOnConnect(OnConnectCallback cb) {
    m_onConnect = cb;
}

void TcpServer::setOnMessage(OnMessageCallback cb) {
    m_onMessage = cb;
}

void TcpServer::setOnClose(OnCloseCallback cb) {
    m_onClose = cb;
}

IOMultiplexer::IOType TcpServer::type() const {
    return m_io ? m_io->type() : IOMultiplexer::IOType::SELECT;
}

bool TcpServer::start() {
    // 创建socket
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        Logger::error("socket creation failed");
        return false;
    }
    // 设置SO_REUSEADDR
    int opt = 1;
    if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        Logger::error("setsockopt failed");
        close(m_socket);
        return false;
    }
    
    // 增大发送缓冲区（支持高并发和大量广播）
    int sendbuf = 512 * 1024;  // 512KB
    if (setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, sizeof(sendbuf)) < 0) {
        Logger::warn("设置发送缓冲区失败，使用默认值");
    }
    
    // 增大接收缓冲区
    int recvbuf = 512 * 1024;  // 512KB
    if (setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf)) < 0) {
        Logger::warn("设置接收缓冲区失败，使用默认值");
    }
    // 绑定地址
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    if (inet_pton(AF_INET, m_ip.c_str(), &addr.sin_addr) <= 0) {
        Logger::error("invalid address");
        close(m_socket);
        return false;
    }
    if (bind(m_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::error("bind failed");
        close(m_socket);
        return false;
    }
    // 监听 - 设置更大的backlog并设置为非阻塞
    if (listen(m_socket, SOMAXCONN) < 0) {
        Logger::error("listen failed");
        close(m_socket);
        return false;
    }
    
    // 设置监听socket为非阻塞模式，支持批量accept
    int flags = fcntl(m_socket, F_GETFL, 0);
    fcntl(m_socket, F_SETFL, flags | O_NONBLOCK);
    // 初始化IO多路复用器
    if (!m_io->init()) {
        Logger::error("IO多路复用器初始化失败");
        close(m_socket);
        return false;
    }
    // 注册监听socket到IO多路复用器
    m_io->addfd(m_socket, IOMultiplexer::EventType::READ);
    Logger::info("[TcpServer] 服务器启动成功: " + m_ip + ":" + std::to_string(m_port));
    m_running = true;
    // 启动主循环（可用线程实现）
    std::thread([this]() { this->run(); }).detach();
    // 初始化心跳线程池（1线程，10秒检测一次）
    m_heartbeatPool = std::make_unique<HeartbeatThreadPool>(1, 10000);
    m_heartbeatPool->registerTask([this]() { this->checkHeartbeats(); });
    return true;
}

void TcpServer::run() {
    while (m_running) {
        std::vector<std::pair<int, IOMultiplexer::EventType>> activeEvents;
        int n = m_io->wait(activeEvents, 100);  // 100ms超时
        if (n < 0) {
            Logger::error("等待事件失败");
            continue;
        }
        for (auto& [fd, event] : activeEvents) {
            if (fd == m_socket) {
                handleAccept();  // 现在会批量接受连接
            } else if (event & IOMultiplexer::EventType::READ) {
                handleRead(fd);
            } else if (event & IOMultiplexer::EventType::WRITE) {
                // 处理写事件，继续发送缓冲区数据
                std::lock_guard<std::mutex> lock(getSendMutex(fd));
                flushSendBuffer(fd);
            } else if (event & IOMultiplexer::EventType::ERROR) {
                handleClose(fd);
            }
        }
    }
}

void TcpServer::stop() {
    m_running = false;
    close(m_socket);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[fd, _] : m_clients) {
        m_io->removefd(fd);
        close(fd);
    }
    m_clients.clear();
    Logger::info("[TcpServer] 服务器已停止");
}

void TcpServer::handleAccept() {
    // 批量接受连接，避免积压
    for (int i = 0; i < 32; ++i) {  // 每次事件最多接受32个连接
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(m_socket, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有更多待接受的连接
                break;
            }
            if (m_running) Logger::error("accept failed");
            return;
        }
        // 设置非阻塞
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        
        // 为每个客户端连接设置大缓冲区（支持广播）
        int client_sendbuf = 512 * 1024;  // 512KB
        setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &client_sendbuf, sizeof(client_sendbuf));
        int client_recvbuf = 512 * 1024;  // 512KB
        setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &client_recvbuf, sizeof(client_recvbuf));
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_clients[client_fd] = 1;
            m_lastActive[client_fd] = std::chrono::steady_clock::now();
        }
        m_io->addfd(client_fd, IOMultiplexer::EventType::READ);
        
        // 调用虚函数（子类可重写）
        onClientConnected(client_fd);
        
        // 调用回调函数（兼容旧代码）
        if (m_onConnect) m_onConnect(client_fd);
        
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        Logger::info(std::string("[TcpServer] 客户端") + std::to_string(client_fd) + "连接成功（IP:" + ip + "）");
    }
}

// 发送心跳包（使用新的发送逻辑）
void TcpServer::sendHeartbeat(int client_fd) {
    if (!m_heartbeatEnabled) return;

    uint32_t magic = htonl(HEARTBEAT_MAGIC);
    sendData(client_fd, (char*)&magic, sizeof(magic), true);
    Logger::debug("[TcpServer] 客户端" + std::to_string(client_fd) + "心跳包加入发送队列");
}

// 业务数据发送接口（供外部调用）
void TcpServer::sendBusinessData(int client_fd, const std::string& data) {
    sendData(client_fd, data.data(), data.size(), false);
}

void TcpServer::checkHeartbeats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_clients.begin(); it != m_clients.end(); ) {
        int fd = it->first;
        auto last = m_lastActive.find(fd);
        if (last == m_lastActive.end() || std::chrono::duration_cast<std::chrono::seconds>(now - last->second).count() > m_heartbeatTimeout) {
            Logger::info("[Heartbeat] 客户端" + std::to_string(fd) + "心跳超时，关闭连接");
            m_io->removefd(fd);
            close(fd);
            it = m_clients.erase(it);
            m_lastActive.erase(fd);
            if (m_onClose) m_onClose(fd);
        } else {
            sendHeartbeat(fd);
            ++it;
        }
    }
}

void TcpServer::handleRead(int client_fd) {
    std::vector<char> buffer(BUFFER_SIZE);
    int bytes_received = recv(client_fd, buffer.data(), buffer.size(), 0);
    if (bytes_received <= 0) {
        // 处理断开连接或错误，保持原逻辑
        if (bytes_received == 0) {
            handleClose(client_fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            handleClose(client_fd);
        }
        return;
    }

    // 更新活跃时间
    m_lastActive[client_fd] = std::chrono::steady_clock::now();

    size_t processed = 0;
    const char* data = buffer.data();
    size_t total_len = bytes_received;

    // ==================== 优先过滤所有心跳包（处理粘包） ====================
    while (processed + sizeof(uint32_t) <= total_len) {
        uint32_t magic_recv;
        memcpy(&magic_recv, data + processed, sizeof(magic_recv));
        magic_recv = ntohl(magic_recv);

        if (magic_recv == HEARTBEAT_MAGIC) {
            // 识别到心跳包，跳过4字节
            processed += sizeof(uint32_t);
            Logger::debug("[TcpServer] 客户端" + std::to_string(client_fd) + "过滤心跳包，累计处理: " + std::to_string(processed) + "字节");
        } else {
            // 非心跳包，退出循环
            break;
        }
    }

    // ==================== 处理剩余业务数据 ====================
    if (processed < total_len) {
    std::string business_data(data + processed, total_len - processed);
    if (m_onMessage) {
        // 业务层应调用 sendBusinessData 发送响应
        m_onMessage(client_fd, business_data);
    }
}
}

void TcpServer::handleClose(int client_fd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_clients.find(client_fd) == m_clients.end()) return;

    // 清理发送缓冲区和锁
    {
        std::lock_guard<std::mutex> lock(m_mutexForLocks);
        m_sendBuffers.erase(client_fd);
        m_sendMutexes.erase(client_fd);
    }

    m_io->removefd(client_fd);
    close(client_fd);
    m_clients.erase(client_fd);
    m_lastActive.erase(client_fd);
    if (m_onClose) m_onClose(client_fd);
    Logger::info("[TcpServer] 客户端" + std::to_string(client_fd) + "断开连接");
}

void TcpServer::onDataReceived(int, const char*, size_t) {
    // 默认空实现
}

void TcpServer::onClientConnected(int) {
    // 默认空实现
}

void TcpServer::onClientDisconnected(int) {
    // 默认空实现
} 

std::mutex& TcpServer::getSendMutex(int client_fd) {
    std::lock_guard<std::mutex> lock(m_mutexForLocks); // 保护哈希表操作
    auto it = m_sendMutexes.find(client_fd);
    if (it == m_sendMutexes.end()) {
        // 安全插入新锁
       it = m_sendMutexes.emplace(client_fd, std::make_unique<std::mutex>()).first;
    }
    return *it->second;
}

void TcpServer::sendData(int client_fd, const char* data, size_t len, [[maybe_unused]] bool is_heartbeat) {
    if (len == 0) return;

    // 获取客户端专属锁
    std::mutex& sendLock = getSendMutex(client_fd);
    std::lock_guard<std::mutex> lock(sendLock);

    // 将数据加入发送缓冲区
    m_sendBuffers[client_fd].emplace_back(data, data + len);

    // 尝试立即发送缓冲区数据
    flushSendBuffer(client_fd);

    // 若缓冲区仍有数据，注册写事件（通过 IO 多路复用后续发送）
    if (!m_sendBuffers[client_fd].empty()) {
        m_io->modifyFd(client_fd, IOMultiplexer::EventType::READ | IOMultiplexer::EventType::WRITE);
    }
}

// 发送缓冲区刷新函数
void TcpServer::flushSendBuffer(int client_fd) {
    auto it = m_sendBuffers.find(client_fd);
    if (it == m_sendBuffers.end() || it->second.empty()) return;

    while (!it->second.empty()) {
        const auto& buffer = it->second.front();
        ssize_t sent = send(client_fd, buffer.data(), buffer.size(), 0);

        if (sent < 0) {
            // 非阻塞下暂时无法发送，退出等待下次写事件
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                // 发送错误，关闭连接
                Logger::error("[TcpServer] 发送失败，客户端FD: " + std::to_string(client_fd));
                handleClose(client_fd);
                return;
            }
        } else if (sent < (ssize_t)buffer.size()) {
            // 部分发送，保留剩余数据
            std::vector<char> remaining(buffer.begin() + sent, buffer.end());
            it->second.front() = std::move(remaining);
            break;
        } else {
            // 完整发送，移除缓冲区数据
            it->second.pop_front();
        }
    }

    // 缓冲区为空时，取消写事件注册
    if (it->second.empty()) {
        m_io->modifyFd(client_fd, IOMultiplexer::EventType::READ);
    }
}