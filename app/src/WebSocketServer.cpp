#include "WebSocketServer.h"
#include "base/Logger.h"
#include "ProtocolFactory.h"
#include "IO/IOFactory.h"
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
// --------------- UTF-8 校验 -----------------
static bool isValidUtf8(const std::string& str) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(str.data());
    size_t len = str.size();
    for (size_t i = 0; i < len; ) {
        unsigned char c = s[i];
        if (c < 0x80) { // 0xxxxxxx
            i++;
        } else if ((c & 0xE0) == 0xC0) { // 110xxxxx 10xxxxxx
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) { // 1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false; // 非法起始字节
        }
    }
    return true;
}
WebSocketServer::WebSocketServer(const std::string& host, int port, 
                               IOMultiplexer::IOType io_type, IThreadPool* threadPool, 
                               EnhancedConfigReader* config)
    : ApplicationServer(host, port, io_type, threadPool), config_(config) {
    
    // ✅ 禁用TCP层心跳包（WebSocket不兼容原始TCP心跳）
    // WebSocket连接中，浏览器会将所有接收到的数据当作WebSocket帧解析
    // 如果发送原始的TCP心跳包（4字节魔数），浏览器可能误判为带掩码的帧而断开连接
    setHeartbeatEnabled(false);
    Logger::info("🚫 已禁用TCP层心跳包（WebSocket连接使用自己的PING/PONG机制）");
    
    // 保存线程池引用供后续使用
    threadPool_ = threadPool;
    
    // 初始化WebSocket配置
    loadConfig();
    
    // 注意：这里不直接调用initializeProtocolRouter()，因为ApplicationServer::start()会调用
    Logger::info("WebSocketServer initialized on " + host + ":" + std::to_string(port));
}

void WebSocketServer::initializeProtocolRouter() {
    // 父类会为每个客户端创建独立的WebSocket协议实例
    // 不需要在这里重复创建共享实例
    Logger::info("WebSocket protocol router initialized (using parent class implementation)");
}

void WebSocketServer::handleRead(int clientSocket, const char* data, size_t length) {
    currentClientFd_ = clientSocket;
    // 记录所有接收到的数据，用于调试
    Logger::debug("Received data from client " + std::to_string(clientSocket) + ", length: " + std::to_string(length));
        std::ostringstream raw;
    for (size_t i = 0; i < length && i < 64; ++i) {
        raw << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)data[i] << ' ';
    }
    Logger::info("原始帧十六进制: " + raw.str());

    // 调用父类的 onDataReceived，它会为每个客户端使用独立的协议实例
    ApplicationServer::onDataReceived(clientSocket, data, length);
}

std::string WebSocketServer::convertDataToString(const char* data, size_t length) {
    // 检查是否是UTF-16编码（每个字符后面都有0）
    if (length >= 2 && data[1] == '\0') {
        // 转换UTF-16到UTF-8
        std::string result;
        for (size_t i = 0; i < length; i += 2) {
            if (data[i] != '\0') {
                result += data[i];
            }
        }
        Logger::debug("Converted UTF-16 data to UTF-8, original length: " + std::to_string(length) + ", converted length: " + std::to_string(result.length()));
        return result;
    }
    // 假定为UTF-8编码
    Logger::debug("Using data as UTF-8, length: " + std::to_string(length));
    return std::string(data, length);
}

void WebSocketServer::handleWebSocketHandshake(int clientSocket, const std::string& requestData) {
    Logger::info("Processing WebSocket handshake request: " + requestData);
    
    // 查找Sec-WebSocket-Key
    std::string keyHeader = "Sec-WebSocket-Key:";
    size_t keyPos = requestData.find(keyHeader);
    if (keyPos == std::string::npos) {
        // 尝试小写形式
        keyHeader = "sec-websocket-key:";
        keyPos = requestData.find(keyHeader);
    }
    
    if (keyPos == std::string::npos) {
        Logger::error("Sec-WebSocket-Key not found in handshake request");
        return;
    }
    
    keyPos += keyHeader.length();
    // 跳过空格
    while (keyPos < requestData.length() && requestData[keyPos] == ' ') {
        keyPos++;
    }
    
    size_t keyEnd = requestData.find("\r\n", keyPos);
    if (keyEnd == std::string::npos) {
        Logger::error("Invalid Sec-WebSocket-Key format");
        return;
    }
    
    std::string clientKey = requestData.substr(keyPos, keyEnd - keyPos);
    std::string response = generateHandshakeResponse(clientKey);
    
    // 发送握手响应
    sendRawData(clientSocket, response);
    Logger::info("WebSocket handshake completed for client: " + std::to_string(clientSocket));
}

std::string WebSocketServer::generateHandshakeResponse(const std::string& clientKey) {
    static const char* WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string concatenated = clientKey + WEBSOCKET_GUID;
    
    unsigned char hash[20]; // SHA1 produces 20 bytes
    SHA1(reinterpret_cast<const unsigned char*>(concatenated.c_str()), 
         concatenated.length(), hash);
    
    std::string acceptKey = base64_encode(hash, 20);
    
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << acceptKey << "\r\n";
    response << "Sec-WebSocket-Extensions: \r\n";
    response << "\r\n";
    
    Logger::debug("Generated handshake response: " + response.str());
    return response.str();
}

void WebSocketServer::sendRawData(int clientSocket, const std::string& data) {
    // 发送原始数据到客户端，支持部分发送重试
    size_t totalSent = 0;
    size_t remaining = data.length();
    const char* ptr = data.c_str();
    
    while (remaining > 0) {
        ssize_t sent = ::send(clientSocket, ptr + totalSent, remaining, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满，短暂等待后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            Logger::error("发送握手响应失败，客户端 " + std::to_string(clientSocket) + 
                         ": " + std::string(strerror(errno)));
            return;
        }
        totalSent += sent;
        remaining -= sent;
    }
    
    Logger::debug("Sent raw data to client " + std::to_string(clientSocket) + 
                 ", length: " + std::to_string(data.length()));
}

void WebSocketServer::broadcast(const std::string& msg) {
    // 1. 只打包一次消息（复用同一个帧）
    std::vector<char> frame;
    {
        std::lock_guard<std::mutex> protoLock(m_clientProtocolsMutex);
        if (m_clientProtocols.empty()) {
            Logger::warn("没有客户端协议实例");
            return;
        }
        auto firstProto = std::dynamic_pointer_cast<WebSocketProtocol>(m_clientProtocols.begin()->second);
        if (!firstProto || !firstProto->packTextMessage(msg, frame)) {
            Logger::error("打包广播消息失败");
            return;
        }
    }
    
    // 2. 快速复制客户端列表（减少锁持有时间）
    std::vector<int> clients;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients.reserve(m_clients.size());
        for (int fd : m_clients) {
            clients.push_back(fd);
        }
    }
    
    // 3. 单线程快速广播（避免线程创建开销和竞争）
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};
    
    for (int clientFd : clients) {
        // 使用 MSG_DONTWAIT 非阻塞发送
        ssize_t sent = ::send(clientFd, frame.data(), frame.size(), MSG_DONTWAIT);
        
        if (sent == (ssize_t)frame.size()) {
            // 完整发送成功
            successCount++;
        } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 发送缓冲区满，使用TCP层的发送队列
            sendBusinessData(clientFd, std::string(frame.begin(), frame.end()));
            successCount++;
        } else if (sent > 0 && sent < (ssize_t)frame.size()) {
            // 部分发送，剩余部分加入队列
            sendBusinessData(clientFd, std::string(frame.begin() + sent, frame.end()));
            successCount++;
        } else {
            // 发送失败
            failCount++;
        }
    }
    
    // 只在有失败时打印
    if (failCount.load() > 0) {
        Logger::warn("广播完成: 成功=" + std::to_string(successCount.load()) + 
                     ", 失败=" + std::to_string(failCount.load()));
    }
}


void WebSocketServer::onProtocolPacketForClient(int clientFd, uint32_t protoId, const std::vector<char>& packet) {
    if (protoId != WebSocketProtocol::ID) {
        Logger::warn("收到非 WebSocket 协议数据包，协议ID: " + std::to_string(protoId));
        return;
    }
    
    // 调用内部处理函数
    currentClientFd_ = clientFd;
    onPacketReceived(packet.data(), packet.size());
}

void WebSocketServer::onPacketReceived(const char* data, size_t length) {
    std::string message(data, length);
    if (!isValidUtf8(message)) {
    Logger::error("收到非法 UTF-8 文本帧，直接关闭连接");
sendCloseFrame(currentClientFd_, 1007, "Invalid UTF-8 in TEXT frame");
        return;
    }

    // 确保该客户端已加入广播列表（首次收到消息时加入）
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        if (m_clients.find(currentClientFd_) == m_clients.end()) {
            m_clients.insert(currentClientFd_);
            Logger::info("✅ 客户端 " + std::to_string(currentClientFd_) + " 握手完成，加入广播列表（共" + 
                        std::to_string(m_clients.size()) + "个客户端）");
        }
    }
    
    Logger::info("收到消息 [客户端" + std::to_string(currentClientFd_) + "]: " + message);

    // 在消息前加上发送者标识
    std::string broadcastMsg = "[客户端" + std::to_string(currentClientFd_) + "]: " + message;
    
    // 广播消息给所有已连接客户端（包括发送者）
    broadcast(broadcastMsg);
}
void WebSocketServer::sendCloseFrame(int fd, uint16_t code, const std::string& reason) {
    std::vector<char> frame;
    frame.push_back(0x88);                           // FIN=1, opcode=8
    uint8_t len = 2 + reason.size();
    frame.push_back(static_cast<char>(len));         // 无掩码
    frame.push_back(static_cast<char>(code >> 8));
    frame.push_back(static_cast<char>(code & 0xFF));
    frame.insert(frame.end(), reason.begin(), reason.end());
    ::send(fd, frame.data(), frame.size(), 0);
}
void WebSocketServer::onError(const std::string& error) {
    Logger::error("WebSocket error: " + error);
}

void WebSocketServer::loadConfig() {
    if (!config_) {
        Logger::info("未提供配置，使用默认WebSocket参数");
        return;
    }
    
    // 从配置文件中加载WebSocket参数
    enablePing_ = config_->getBool("websocket.enable_ping", true);
    pingInterval_ = config_->getInt("websocket.ping_interval", 30);
    maxFrameSize_ = config_->getInt("websocket.max_frame_size", 65536);
    enableCompression_ = config_->getBool("websocket.enable_compression", false);
    
    Logger::info("WebSocket配置已加载:");
    Logger::info("  - 启用ping/pong: " + std::string(enablePing_ ? "是" : "否"));
    Logger::info("  - ping间隔: " + std::to_string(pingInterval_) + "秒");
    Logger::info("  - 最大帧大小: " + std::to_string(maxFrameSize_) + "字节");
    Logger::info("  - 启用压缩: " + std::string(enableCompression_ ? "是" : "否"));
}

void WebSocketServer::onClientConnected(int clientFd) {
    // TCP 连接建立，但 WebSocket 握手还未完成
    Logger::info("🔌 WebSocketServer::onClientConnected - 客户端 " + std::to_string(clientFd));
    
    // 调用父类方法创建发送锁
    ApplicationServer::onClientConnected(clientFd);
    
    Logger::info("✅ 父类 onClientConnected 调用完成");
}

void WebSocketServer::onClientDisconnected(int clientFd) {

    std::lock_guard<std::mutex> lock(clientsMutex_);
    Logger::info("WebSocket客户端" + std::to_string(clientFd) + "已断开");
    // 清理客户端特定资源
    m_clients.erase(clientFd);

    ApplicationServer::onClientDisconnected(clientFd);
}

// ApplicationServer纯虚函数实现
std::string WebSocketServer::handleHttpRequest(const std::string& request, int clientFd) {
    (void)request;
    (void)clientFd;
    // WebSocket协议主要通过WebSocket协议处理器处理，不直接处理HTTP请求
    return "";
}

std::string WebSocketServer::handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) {
    (void)command;
    (void)args;
    // WebSocket消息回显功能
    return "WebSocket echo response";
}

bool WebSocketServer::parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) {
    (void)path;
    (void)command;
    (void)args;
    // WebSocket协议不通过URL路径解析
    return false;
}