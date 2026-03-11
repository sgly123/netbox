#include "ApplicationServer.h"
#include <sstream>
#include <iomanip>
#include "HttpProtocol.h"
#include "PureRedisProtocol.h"
#include <sstream>
#include <algorithm>
#include <sys/socket.h>
#include "ProtocolRouter.h"
#include "WebSocketProtocol.h"
#include <iostream>
#include <thread>
#include <chrono>

ApplicationServer::ApplicationServer(const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool)
    : TcpServer(ip, port, io_type), m_pool(pool) {}

ApplicationServer::~ApplicationServer() {}

bool ApplicationServer::start() {
    // 初始化协议路由器
    m_router = std::make_unique<ProtocolRouter>();

    // 设置ProtocolRouter的回调（关键修复！）
    m_router->setPacketCallback([this](uint32_t protoId, const std::vector<char>& packet) {
        if (protoId  != 3) {
            Logger::info("ApplicationServer收到协议" + std::to_string(protoId) + "的数据包，长度: " + std::to_string(packet.size()));
            this->onProtocolPacket(protoId, packet);
        }
    });

    initializeProtocolRouter();

    // 设置TcpServer的消息回调
    setOnMessage([this](int clientFd, const std::string& data) {
        Logger::info("ApplicationServer通过回调收到客户端" + std::to_string(clientFd) + "的数据，长度: " + std::to_string(data.length()));
        this->onDataReceived(clientFd, data.c_str(), data.length());
    });

    // 启动TCP服务器
    return TcpServer::start();
}

void ApplicationServer::stop() {
    TcpServer::stop();
}

void ApplicationServer::onDataReceived(int clientFd, const char* data, size_t len) {
    Logger::info("ApplicationServer收到客户端" + std::to_string(clientFd) + "的数据，长度: " + std::to_string(len));

    // 复制数据到堆上，避免栈上数据被释放
    std::vector<char> buffer(data, data + len);

    // 如果有线程池，提交到线程池处理
    if (m_pool) {
        m_pool->enqueue([this, clientFd, buffer]() {
            this->processData(clientFd, buffer.data(), buffer.size());
        });
    } else {
        // 没有线程池，直接在Reactor线程处理
        processData(clientFd, data, len);
    }
}

void ApplicationServer::processData(int clientFd, const char* data, size_t len) {
    m_currentClientFd = clientFd;

    std::ostringstream hexStream;
    hexStream << "原始数据十六进制: ";
    for (size_t i = 0; i < len && i < 50; ++i) {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)data[i] << " ";
    }
    Logger::debug(hexStream.str());

    // ✅ 优先：如果是 RESP 格式（以 '*' 开头），直接走 PureRedisProtocol
    if (len > 0 && data[0] == '*') {
        auto* pureProto = dynamic_cast<PureRedisProtocol*>(m_router->getProtocol(3));
        if (pureProto) {
            size_t processed = pureProto->onClientDataReceived(clientFd, data, len);
            Logger::debug("PureRedisProtocol 直接处理了 " + std::to_string(processed) + " 字节");
            return; // ✅ 处理完就返回，不再走分发器
        }
    }

    // 检查客户端是否已经有WebSocket协议实例
    auto it = m_clientProtocols.find(clientFd);
    if (it != m_clientProtocols.end()) {
        Logger::debug("使用已存在的客户端协议实例处理数据");
        // 客户端已经有协议实例，直接交给该协议处理器处理
        size_t processed = it->second->onDataReceived(data, len);
        Logger::debug("客户端协议处理器处理了 " + std::to_string(processed) + " 字节");
        // 检查连接是否已关闭
        auto wsProto = std::dynamic_pointer_cast<WebSocketProtocol>(it->second);
        if (wsProto && wsProto->getState() == WebSocketProtocol::State::CLOSED) {
            Logger::info("WebSocket连接已关闭，断开客户端" + std::to_string(clientFd));
            closeClientConnection(clientFd);
        }
        return;
    } else {
        Logger::debug("客户端" + std::to_string(clientFd) + "没有已存在的协议实例");
        // 检查客户端是否仍然有效
        if (m_clients.find(clientFd) == m_clients.end()) {
            Logger::warn("客户端" + std::to_string(clientFd) + "已断开连接，忽略收到的数据");
            return;
        } else {
            Logger::debug("客户端" + std::to_string(clientFd) + "仍然处于连接状态");
        }
    }

    // ✅  fallback：非 RESP 再走协议分发器
    if (m_router) {
        size_t processed = m_router->onDataReceived(clientFd, data, len);
        Logger::debug("协议分发器处理了 " + std::to_string(processed) + " 字节");

        // 只有当协议分发器确实没有处理任何数据时，才尝试兜底的Redis协议处理
        if (processed == 0 && len > 0) {
            // 检查是否是WebSocket握手请求
            std::string dataStr(data, len);
            if (dataStr.find("GET ") == 0 && 
                (dataStr.find("Upgrade: websocket") != std::string::npos ||
                 dataStr.find("Upgrade: WebSocket") != std::string::npos ||
                 dataStr.find("upgrade: websocket") != std::string::npos)) {
                Logger::info("检测到WebSocket握手请求，直接使用WebSocket协议处理器");
                
                // 为客户端创建独立的WebSocket协议实例
                std::shared_ptr<WebSocketProtocol> wsProto;
                {
                    std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
                    auto it = m_clientProtocols.find(clientFd);
                if (it == m_clientProtocols.end()) {
                    // 检查客户端是否仍然有效
                    if (m_clients.find(clientFd) == m_clients.end()) {
                        Logger::warn("客户端" + std::to_string(clientFd) + "已断开连接，无法创建协议实例");
                        return;
                    }
                    
                    // 创建新的WebSocket协议实例
                    wsProto = std::make_shared<WebSocketProtocol>();
                        
                        // 设置解码后的消息回调（TEXT/BINARY）
                    wsProto->setPacketCallback([this, clientFd](const std::vector<char>& packet) {
                        this->onProtocolPacketForClient(clientFd, 4, packet);
                    });
                        
                // 设置原始帧回调（握手响应、PONG、CLOSE等控制帧）
                wsProto->setRawFrameCallback([this, clientFd](const std::vector<char>& frame) {
                    // 获取该客户端的发送锁（防止帧交错）
                    std::shared_ptr<std::mutex> sendMutex;
                    {
                        std::lock_guard<std::mutex> lock(this->m_sendMutexesMapMutex);
                        auto it = this->m_clientSendMutexes.find(clientFd);
                        if (it != this->m_clientSendMutexes.end()) {
                            sendMutex = it->second;
                        }
                    }
                    
                    // 发送（如果有锁就加锁，没有就直接发送，因为握手阶段可能还没创建锁）
                    ssize_t sent;
                    if (sendMutex) {
                        std::lock_guard<std::mutex> sendLock(*sendMutex);
                        sent = ::send(clientFd, frame.data(), frame.size(), 0);
                    } else {
                        sent = ::send(clientFd, frame.data(), frame.size(), 0);
                    }
                    
                    if (sent < 0) {
                        Logger::error("❌ 发送原始帧失败: " + std::string(strerror(errno)));
                    } else if (static_cast<size_t>(sent) != frame.size()) {
                        Logger::warn("⚠️ 发送原始帧不完整: " + std::to_string(sent) + "/" + std::to_string(frame.size()));
                    } else {
                        Logger::debug("✅ WebSocket原始帧发送成功 -> 客户端" + std::to_string(clientFd) + 
                                     ", " + std::to_string(sent) + " 字节");
                    }
                });
                        
                    wsProto->setErrorCallback([this, clientFd](const std::string& error) {
                        Logger::error("WebSocket协议错误: " + error);
                        // 出现错误时关闭客户端连接
                        this->closeClientConnection(clientFd);
                    });
                        
                    m_clientProtocols[clientFd] = wsProto;
                    Logger::info("为客户端" + std::to_string(clientFd) + "创建新的WebSocket协议实例");
                } else {
                    wsProto = std::dynamic_pointer_cast<WebSocketProtocol>(it->second);
                    }
                }
                
                Logger::debug("WebSocket协议处理器指针: " + std::to_string(reinterpret_cast<uintptr_t>(wsProto.get())));
                if (wsProto) {
                    size_t wsProcessed = wsProto->onDataReceived(data, len);
                    Logger::debug("WebSocket协议处理器处理了 " + std::to_string(wsProcessed) + " 字节");
                    // 检查连接是否已关闭
                    if (wsProto && wsProto->getState() == WebSocketProtocol::State::CLOSED) {
                        Logger::info("WebSocket连接已关闭，断开客户端" + std::to_string(clientFd));
                        closeClientConnection(clientFd);
                    }
                    return;
                } else {
                    Logger::error("WebSocket协议处理器未注册");
                }
            }
            
            Logger::warn("协议分发器未识别，仍尝试 PureRedisProtocol");
            auto* pureProto = dynamic_cast<PureRedisProtocol*>(m_router->getProtocol(3));
            if (pureProto) {
                size_t fallbackProcessed = pureProto->onClientDataReceived(clientFd, data, len);
                Logger::debug("PureRedisProtocol 兜底处理了 " + std::to_string(fallbackProcessed) + " 字节");
            } else {
                Logger::error("PureRedisProtocol 未注册");
            }
        }
        // 如果协议分发器处理了数据，直接返回
        else if (processed > 0) {
            return;
        }
        // 如果协议分发器没有处理任何数据，且数据长度很小，可能是无效数据，关闭连接
        else if (processed == 0 && len < 20) {
            Logger::warn("收到无法识别的小数据包，长度: " + std::to_string(len) + "，关闭客户端连接");
            closeClientConnection(clientFd);
            return;
        }
    } else {
        Logger::error("协议分发器未初始化");
    }

    {
        std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
    auto it_close = m_clientProtocols.find(clientFd);
    if (it_close != m_clientProtocols.end()) {
        auto wsProto = std::dynamic_pointer_cast<WebSocketProtocol>(it_close->second);
        if (wsProto && wsProto->getState() == WebSocketProtocol::State::CLOSED) {
        // 延迟关闭，确保当前处理流程结束后再清理
            std::thread([this, clientFd]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                closeClientConnection(clientFd);
        }).detach();
    }
        }
}
} 

void ApplicationServer::onClientConnected(int clientFd) {
    Logger::info("🔧 ApplicationServer::onClientConnected - 客户端" + std::to_string(clientFd));
    
    // 为该客户端创建发送锁
    {
        std::lock_guard<std::mutex> lock(m_sendMutexesMapMutex);
        m_clientSendMutexes[clientFd] = std::make_shared<std::mutex>();
        Logger::info("✅ 为客户端 " + std::to_string(clientFd) + " 创建发送锁成功");
    }
}

void ApplicationServer::onClientDisconnected(int clientFd) {
    Logger::info("客户端" + std::to_string(clientFd) + "已断开连接");
    
    // 查找并通知客户端的WebSocket协议实例连接已断开
    {
        std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
    auto it = m_clientProtocols.find(clientFd);
    if (it != m_clientProtocols.end()) {
        // 如果是WebSocket协议实例，发送关闭帧
        auto wsProto = std::dynamic_pointer_cast<WebSocketProtocol>(it->second);
        if (wsProto && wsProto->getState() != WebSocketProtocol::State::CLOSED) {
            // 创建关闭帧并发送
            std::vector<char> closeFrame;
            if (wsProto->packClose(1000, "Connection closed by client", closeFrame)) {
                ::send(clientFd, closeFrame.data(), closeFrame.size(), 0);
                Logger::info("已向客户端" + std::to_string(clientFd) + "发送关闭帧");
            }
            // 不再直接调用setState，因为它是私有的
            // 状态将在WebSocketProtocol内部处理
        }
        
        // 注意：不要在这里删除协议实例，因为closeClientConnection会处理
        Logger::info("客户端" + std::to_string(clientFd) + "的协议实例将在closeClientConnection中清理");
        }
    }
    
    // 移除该客户端的发送锁
    {
        std::lock_guard<std::mutex> lock(m_sendMutexesMapMutex);
        m_clientSendMutexes.erase(clientFd);
    }
    
    // 调用基类实现
    TcpServer::onClientDisconnected(clientFd);
}

void ApplicationServer::onProtocolPacket(uint32_t protoId, const std::vector<char>& packet) {
    if (protoId != 3) {  // 跳过 Redis 数据包
        Logger::info("ApplicationServer::onProtocolPacket 被调用，协议ID: " + std::to_string(protoId) + "，数据包长度: " + std::to_string(packet.size()));
        
        // 如果是WebSocket协议的数据，将其发送回客户端
        if (protoId == 4 && m_currentClientFd > 0 && !packet.empty()) {  // WebSocket协议ID为4
            Logger::info("正在发送WebSocket响应数据，长度: " + std::to_string(packet.size()));
            ssize_t sent = ::send(m_currentClientFd, packet.data(), packet.size(), 0);
            if (sent < 0) {
                Logger::error("发送WebSocket响应数据失败: " + std::to_string(errno));
            } else {
                Logger::info("成功发送WebSocket响应数据: " + std::to_string(sent) + " 字节");
            }
        }
    }
}

void ApplicationServer::onProtocolPacketForClient(int clientFd, uint32_t protoId, const std::vector<char>& packet) {
    // 这个方法是父类的默认实现，子类应该重写来处理各自的业务逻辑
    // WebSocketServer 已经重写了这个方法，所以不应该走到这里
    (void)clientFd;
    (void)protoId;
    (void)packet;
    Logger::debug("ApplicationServer::onProtocolPacketForClient 被调用（应该由子类重写）");
}

void ApplicationServer::closeClientConnection(int clientFd) {
    Logger::info("准备关闭客户端" + std::to_string(clientFd) + "的连接");

    // 1. 发送关闭帧（如果协议还在）
    {
        std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
    auto it = m_clientProtocols.find(clientFd);
    if (it != m_clientProtocols.end()) {
        auto wsProto = std::dynamic_pointer_cast<WebSocketProtocol>(it->second);
        if (wsProto && wsProto->getState() != WebSocketProtocol::State::CLOSED) {
            std::vector<char> closeFrame;
            if (wsProto->packClose(1000, "Server closing", closeFrame)) {
                ::send(clientFd, closeFrame.data(), closeFrame.size(), 0);
                Logger::info("已向客户端" + std::to_string(clientFd) + "发送关闭帧");
            }
            wsProto->setState(WebSocketProtocol::State::CLOSED);
            }
        }
    }

    // 2. 延迟释放协议对象，避免在处理流程中销毁自己
    std::thread([this, clientFd]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 足够让当前帧处理完

        // 再次检查并清理
        {
            std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
        auto it = m_clientProtocols.find(clientFd);
        if (it != m_clientProtocols.end()) {
            m_clientProtocols.erase(it);
            Logger::info("已延迟清理客户端" + std::to_string(clientFd) + "的协议实例");
            }
        }

        // 关闭 socket
        ::close(clientFd);
        Logger::info("客户端" + std::to_string(clientFd) + "连接已关闭");

        // 从客户端列表移除
        auto client_it = m_clients.find(clientFd);
        if (client_it != m_clients.end()) {
            m_clients.erase(client_it);
        }
    }).detach();
}

std::string ApplicationServer::generateJsonResponse(bool success, const std::string& data, const std::string& message) {
    std::string json = "{";
    json += "\"success\":" + std::string(success ? "true" : "false") + ",";
    json += "\"data\":\"" + data + "\",";
    json += "\"message\":\"" + message + "\"";
    json += "}";
    return json;
} 

void ApplicationServer::initializeProtocolRouter() {
    Logger::info("开始初始化协议路由器");

    // 注册WebSocket协议
    auto websocketProto = std::make_shared<WebSocketProtocol>();
    Logger::info("WebSocketProtocol对象创建完成");

    // 设置WebSocket协议的回调
    websocketProto->setPacketCallback([this](const std::vector<char>& packet) {
        Logger::info("WebSocketProtocol回调被调用，响应长度: " + std::to_string(packet.size()));
        this->onProtocolPacket(4, packet); // WebSocket协议ID为4
    });

    // 协议层错误回调
    websocketProto->setErrorCallback([](const std::string& error) {
        Logger::error("WebSocket协议错误: " + error);
    });
    Logger::info("WebSocketProtocol配置完成");

    // 注册WebSocket协议到分发器
    if (m_router) {
        m_router->registerProtocol(WebSocketProtocol::ID, websocketProto);
        Logger::info("注册WebSocketProtocol，ID: " + std::to_string(WebSocketProtocol::ID));
    } else {
        Logger::error("协议路由器未初始化，无法注册WebSocket协议");
    }

    Logger::info("协议路由器初始化完成");
}
