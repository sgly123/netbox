#include "WebSocketServer.h"
#include "base/Logger.h"
#include "base/IOMultiplexer.h"
#include <iostream>
#include <thread>
#include <chrono>

/**
 * @brief WebSocket服务器测试程序
 * 
 * 功能：
 * 1. 启动WebSocket服务器
 * 2. 监听指定端口
 * 3. 处理客户端连接和消息
 * 4. 提供回显服务
 * 
 * 使用方法：
 * ./WebSocketTest [port]
 * 默认端口：8080
 */
int main(int argc, char* argv[]) {
    // 日志系统已初始化，无需手动初始化
    Logger::info("WebSocket Test Server Starting...");
    
    // 解析命令行参数
    uint16_t port = 8000;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    try {
        // 创建WebSocket服务器
        WebSocketServer server("0.0.0.0", port, IOMultiplexer::IOType::EPOLL, nullptr);
        
        Logger::info("WebSocket server starting on port " + std::to_string(port));
        
        // 启动服务器
        server.start();
        
        Logger::info("WebSocket server started successfully");
        Logger::info("Waiting for WebSocket connections...");
        Logger::info("You can test with: wscat -c ws://localhost:" + std::to_string(port));
        
        // 保持服务器运行
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    } catch (const std::exception& e) {
        Logger::error("WebSocket server error: " + std::string(e.what()));
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}