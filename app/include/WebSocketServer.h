#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include "ApplicationServer.h"
#include "WebSocketProtocol.h"
#include "util/EnhancedConfigReader.h"
#include <memory>
#include <string>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include "base64.h"
#include <set>
#include <mutex>
/**
 * @brief WebSocket服务器类
 * 
 * 功能特点：
 * 1. 基于TCP的WebSocket协议实现
 * 2. 支持文本和二进制消息传输
 * 3. 自动处理WebSocket握手
 * 4. 支持Ping/Pong心跳机制
 * 5. 提供消息回显功能
 * 
 * 使用示例：
 * WebSocketServer server("0.0.0.0", 8000);
 * server.start();
 */
class WebSocketServer : public ApplicationServer {
public:
    /**
     * @brief 构造函数
     * @param host 监听IP地址
     * @param port 监听端口
     * @param io_type IO多路复用类型
     * @param threadPool 线程池指针
     * @param config 配置读取器指针（可选）
     */
    WebSocketServer(const std::string& host, int port, 
                   IOMultiplexer::IOType io_type, IThreadPool* threadPool, 
                   EnhancedConfigReader* config = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~WebSocketServer() = default;

protected:
    /**
     * @brief 处理客户端数据接收
     * @param clientSocket 客户端socket
     * @param data 接收到的数据
     * @param length 数据长度
     * 
     * 重写父类方法，使用协议路由器处理WebSocket协议
     */
    void handleRead(int clientSocket, const char* data, size_t length);

    /**
     * @brief 客户端连接事件处理
     * @param clientFd 客户端文件描述符
     */
    void onClientConnected(int clientFd) override;

    /**
     * @brief 客户端断开事件处理
     * @param clientFd 客户端文件描述符
     */
    void onClientDisconnected(int clientFd) override;

    /**
     * @brief 协议数据包处理（重写父类方法）
     * @param clientFd 客户端文件描述符
     * @param protoId 协议ID
     * @param packet 数据包
     */
    void onProtocolPacketForClient(int clientFd, uint32_t protoId, const std::vector<char>& packet);

private:
    EnhancedConfigReader* config_;  // 配置读取器
    IThreadPool* threadPool_;       // 线程池指针
    int currentClientFd_ = -1;
    std::set<int> m_clients; // 已连接客户端文件描述符集合
    std::mutex clientsMutex_; // 客户端集合互斥锁
    
    // WebSocket配置参数
    bool enablePing_ = true;           // 是否启用ping/pong
    int pingInterval_ = 30;            // ping间隔（秒）
    size_t maxFrameSize_ = 65536;      // 最大帧大小
    bool enableCompression_ = false;   // 是否启用压缩
    
    /**
     * @brief 处理WebSocket握手
     * @param clientSocket 客户端socket
     * @param requestData 请求数据
     */
    void handleWebSocketHandshake(int clientSocket, const std::string& requestData);
    void sendCloseFrame(int fd, uint16_t code, const std::string& reason);
    
    /**
     * @brief 广播消息给所有已连接客户端
     * @param msg 要广播的消息
     */
    void broadcast(const std::string& msg);
    /**
     * @brief 生成握手响应
     * @param clientKey 客户端密钥
     * @return 握手响应数据
     */
    std::string generateHandshakeResponse(const std::string& clientKey);
    
    /**
     * @brief 发送原始数据到客户端
     * @param clientSocket 客户端socket
     * @param data 要发送的数据
     */
    void sendRawData(int clientSocket, const std::string& data);
    
    /**
     * @brief 将数据转换为字符串（处理UTF-16编码）
     * @param data 原始数据
     * @param length 数据长度
     * @return 转换后的字符串
     */
    std::string convertDataToString(const char* data, size_t length);
    
    /**
     * @brief 从配置文件中加载WebSocket参数
     */
    void loadConfig();
    
    /**
     * @brief 初始化协议路由器
     * 注册WebSocket协议并设置回调函数
     */
    void initializeProtocolRouter() override;
    
    /**
     * @brief 数据包接收回调
     * @param data 数据
     * @param length 数据长度
     * 
     * 实现消息回显功能
     */
    void onPacketReceived(const char* data, size_t length);
    
    /**
     * @brief 错误处理回调
     * @param error 错误信息
     */
    void onError(const std::string& error);
    
    // ApplicationServer纯虚函数实现
    std::string handleHttpRequest(const std::string& request, int clientFd) override;
    std::string handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) override;
    bool parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) override;
};

#endif // WEBSOCKET_SERVER_H