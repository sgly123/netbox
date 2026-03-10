#include "server/UdpServer.h"
#include "base/Logger.h"
#include <thread>
#include <cstring>
#include <errno.h>
#include <stdexcept>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
    #define SOCKET_ERROR_CODE WSAGetLastError()
    #define CLOSE_SOCKET closesocket
#else
    #include <unistd.h>
    #include <fcntl.h>
    #define SOCKET_ERROR_CODE errno
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

UdpServer::UdpServer(const std::string& ip, int port, IOMultiplexer::IOType io_type)
    : m_socket(INVALID_SOCKET)
    , m_port(port)
    , m_ip(ip)
    , m_running(false)
    , m_io(IOFactory::createIO(io_type))
{
    if (!m_io) {
        Logger::error("创建IO多路复用器失败");
        throw std::runtime_error("Failed to create IO multiplexer");
    }
    
    if (!m_io->init()) {
        Logger::error("初始化IO多路复用器失败");
        throw std::runtime_error("Failed to initialize IO multiplexer");
    }
    
    Logger::info("UDP服务器创建成功 " + m_ip + ":" + std::to_string(m_port) + 
                 " IO类型: " + IOFactory::getIOTypeName(io_type));
}

UdpServer::~UdpServer() {
    stop();
}

void UdpServer::setOnDatagram(OnDatagramCallback cb) {
    m_onDatagram = cb;
}

void UdpServer::setOnError(OnErrorCallback cb) {
    m_onError = cb;
}

bool UdpServer::start() {
    if (m_running.load()) {
        Logger::warn("UDP服务器已经在运行");
        return true;
    }

    if (!createSocket()) {
        return false;
    }

    if (!bindSocket()) {
        closeSocket();
        return false;
    }

    // 将socket添加到IO多路复用器
    if (!m_io->addfd(m_socket, IOMultiplexer::READ)) {
        Logger::error("添加UDP socket到IO多路复用器失败");
        closeSocket();
        return false;
    }

    m_running.store(true);
    
    // 启动事件循环线程
    std::thread([this]() {
        run();
    }).detach();

    Logger::info("UDP服务器启动成功，监听 " + m_ip + ":" + std::to_string(m_port));
    return true;
}

void UdpServer::stop() {
    if (!m_running.load()) {
        return;
    }

    m_running.store(false);
    
    if (m_socket != INVALID_SOCKET) {
        m_io->removefd(m_socket);
        closeSocket();
    }

    Logger::info("UDP服务器已停止");
}

bool UdpServer::sendTo(const sockaddr_in& addr, const std::string& data) {
    if (!m_running.load()) {
        notifyError(UdpErrorType::UDP_SOCKET_ERROR, "服务器未运行");
        return false;
    }

    if (data.empty()) {
        return true;
    }

    if (data.size() > IOMultiplexer::MAX_UDP_PACKET_SIZE) {
        notifyError(UdpErrorType::PACKET_TOO_LARGE, "数据包过大: " + std::to_string(data.size()));
        return false;
    }

    // 尝试直接发送
    ssize_t sent = ::sendto(m_socket, data.c_str(), data.size(), 0, 
                           reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr));
    
    if (sent == static_cast<ssize_t>(data.size())) {
        // 发送成功
        m_stats.packets_sent++;
        m_stats.bytes_sent += data.size();
        return true;
    } else if (sent == SOCKET_ERROR) {
        int error = SOCKET_ERROR_CODE;
#ifdef _WIN32
        if (error == WSAEWOULDBLOCK) {
#else
        if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
            // 缓冲区满，加入发送队列
            {
                std::lock_guard<std::mutex> lock(m_sendMutex);
                m_sendQueue.emplace(addr, data);
            }
            
            // 监听写事件
            m_io->modifyFd(m_socket, IOMultiplexer::READ | IOMultiplexer::WRITE);
            return true;
        } else {
            // 其他发送错误
            notifyError(UdpErrorType::SEND_FAILED, "发送失败, 错误码: " + std::to_string(error));
            m_stats.send_errors++;
            return false;
        }
    } else {
        // 部分发送（对UDP来说不应该发生）
        Logger::warn("UDP部分发送: " + std::to_string(sent) + "/" + std::to_string(data.size()));
        return false;
    }
}

bool UdpServer::sendTo(const std::string& ip, int port, const std::string& data) {
    sockaddr_in addr = createAddress(ip, port);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        notifyError(UdpErrorType::INVALID_ADDRESS, "无效地址: " + ip + ":" + std::to_string(port));
        return false;
    }
    return sendTo(addr, data);
}

void UdpServer::onDatagramReceived(const sockaddr_in& from, const char* data, size_t len) {
    std::string msg(data, len);
    
    if (m_onDatagram) {
        m_onDatagram(from, msg);
    }
    
    // Logger::debug("收到UDP数据包 从 " + addressToString(from) + " 长度: " + std::to_string(len));
}

void UdpServer::onError(int error_code, const std::string& message) {
    if (m_onError) {
        m_onError(error_code, message);
    }
    
    Logger::error("UDP错误 [" + std::to_string(error_code) + "]: " + message);
}

IOMultiplexer::IOType UdpServer::type() const {
    return m_io->type();
}

void UdpServer::handleRead() {
    if (!processRecv()) {
        notifyError(UdpErrorType::RECV_FAILED, "处理接收事件失败");
    }
}

void UdpServer::handleWrite() {
    if (!processSend()) {
        notifyError(UdpErrorType::SEND_FAILED, "处理发送事件失败");
    }
    
    // 检查发送队列是否为空，如果为空则停止监听写事件
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        if (m_sendQueue.empty()) {
            m_io->modifyFd(m_socket, IOMultiplexer::READ);
        }
    }
}

void UdpServer::run() {
    Logger::info("UDP服务器事件循环开始");
    
    while (m_running.load()) {
        std::vector<std::pair<int, IOMultiplexer::EventType>> activeEvents;
        
        int eventCount = m_io->wait(activeEvents, 1000); // 1秒超时
        
        if (eventCount < 0) {
            if (m_running.load()) {
                notifyError(UdpErrorType::UDP_SOCKET_ERROR, "IO多路复用等待失败");
            }
            break;
        }
        
        for (const auto& event : activeEvents) {
            if (event.first == m_socket) {
                if (event.second & IOMultiplexer::READ) {
                    handleRead();
                }
                if (event.second & IOMultiplexer::WRITE) {
                    handleWrite();
                }
                if (event.second & IOMultiplexer::ERROR) {
                    notifyError(UdpErrorType::UDP_SOCKET_ERROR, "Socket错误事件");
                }
            }
        }
    }
    
    Logger::info("UDP服务器事件循环结束");
}

sockaddr_in UdpServer::createAddress(const std::string& ip, int port) {
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (ip == "0.0.0.0" || ip.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
#ifdef _WIN32
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
#else
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            addr.sin_addr.s_addr = INADDR_NONE;
        }
#endif
    }
    
    return addr;
}

std::string UdpServer::addressToString(const sockaddr_in& addr) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(addr.sin_port));
}

bool UdpServer::createSocket() {
    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket == INVALID_SOCKET) {
        notifyError(UdpErrorType::UDP_SOCKET_ERROR, "创建UDP socket失败, 错误码: " + std::to_string(SOCKET_ERROR_CODE));
        return false;
    }

    // 设置为非阻塞
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(m_socket, FIONBIO, &mode) != 0) {
        notifyError(UdpErrorType::UDP_SOCKET_ERROR, "设置非阻塞模式失败");
        closeSocket();
        return false;
    }
#else
    int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        notifyError(UdpErrorType::UDP_SOCKET_ERROR, "设置非阻塞模式失败");
        closeSocket();
        return false;
    }
#endif

    // 设置地址重用
    int reuse = 1;
    if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse)) == SOCKET_ERROR) {
        Logger::warn("设置地址重用失败: " + std::to_string(SOCKET_ERROR_CODE));
    }

    return true;
}

bool UdpServer::bindSocket() {
    sockaddr_in addr = createAddress(m_ip, m_port);
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        notifyError(UdpErrorType::INVALID_ADDRESS, "无效的绑定地址: " + m_ip);
        return false;
    }

    if (bind(m_socket, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        notifyError(UdpErrorType::BIND_FAILED, "绑定失败 " + m_ip + ":" + std::to_string(m_port) + 
                   ", 错误码: " + std::to_string(SOCKET_ERROR_CODE));
        return false;
    }

    return true;
}

void UdpServer::closeSocket() {
    if (m_socket != INVALID_SOCKET) {
        CLOSE_SOCKET(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

bool UdpServer::processRecv() {
    sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    
    while (true) {
        ssize_t received = recvfrom(m_socket, m_recvBuffer, sizeof(m_recvBuffer) - 1, 0,
                                   reinterpret_cast<struct sockaddr*>(&from), &fromLen);
        
        if (received > 0) {
            m_recvBuffer[received] = '\0'; // 确保字符串结束
            m_stats.packets_received++;
            m_stats.bytes_received += received;
            
            // 调用数据处理回调
            onDatagramReceived(from, m_recvBuffer, static_cast<size_t>(received));
            
        } else if (received == 0) {
            // UDP不会返回0，这里是异常情况
            Logger::warn("UDP recvfrom返回0");
            break;
        } else {
            int error = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
                // 没有更多数据，正常退出
                break;
            } else {
                // 接收错误
                m_stats.recv_errors++;
                return false;
            }
        }
    }
    
    return true;
}

bool UdpServer::processSend() {
    std::lock_guard<std::mutex> lock(m_sendMutex);
    
    while (!m_sendQueue.empty()) {
        const auto& item = m_sendQueue.front();
        
        ssize_t sent = ::sendto(m_socket, item.data.data(), item.data.size(), 0,
                               reinterpret_cast<const struct sockaddr*>(&item.addr), sizeof(item.addr));
        
        if (sent == static_cast<ssize_t>(item.data.size())) {
            // 发送成功
            m_stats.packets_sent++;
            m_stats.bytes_sent += item.data.size();
            m_sendQueue.pop();
        } else if (sent == SOCKET_ERROR) {
            int error = SOCKET_ERROR_CODE;
#ifdef _WIN32
            if (error == WSAEWOULDBLOCK) {
#else
            if (error == EAGAIN || error == EWOULDBLOCK) {
#endif
                // 缓冲区仍然满，稍后再试
                break;
            } else {
                // 发送失败，移除这个包
                m_stats.send_errors++;
                m_sendQueue.pop();
                return false;
            }
        } else {
            // 部分发送（UDP不应该发生）
            Logger::warn("UDP部分发送，丢弃数据包");
            m_sendQueue.pop();
        }
    }
    
    return true;
}

void UdpServer::notifyError(UdpErrorType type, const std::string& detail) {
    std::string message = "UDP错误[" + std::to_string(static_cast<int>(type)) + "]";
    if (!detail.empty()) {
        message += ": " + detail;
    }
    
    onError(static_cast<int>(type), message);
} 