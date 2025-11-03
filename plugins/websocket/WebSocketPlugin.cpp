#include "app/ApplicationRegistry.h"
#include "WebSocketServer.h"
#include "base/Logger.h"

// 前向声明
class EnhancedConfigReader;

/**
 * @brief WebSocket插件注册文件
 * 
 * 功能：
 * 1. 将WebSocketServer注册到应用注册表
 * 2. 支持通过配置文件动态创建WebSocketServer实例
 * 3. 实现插件化架构的核心机制
 * 
 * 使用方法：
 * 在配置文件中设置 application.type = "websocket" 即可使用
 */

/**
 * @brief WebSocketServer注册函数
 * @return 是否注册成功
 */
static bool registerWebSocketServer() {
    Logger::info("正在注册WebSocketServer插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("websocket", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) {
            Logger::info("创建WebSocketServer实例: " + ip + ":" + std::to_string(port));
            return std::make_unique<WebSocketServer>(ip, port, io_type, pool, config);
        });
    
    if (success) {
        Logger::info("WebSocketServer插件注册成功");
    } else {
        Logger::error("WebSocketServer插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册WebSocketServer
 * 
 * 这个变量的初始化会在main函数执行前完成，
 * 确保WebSocketServer在应用注册表中可用
 */
static bool g_webSocketServerRegistered = registerWebSocketServer();

/**
 * @brief 获取WebSocketServer插件信息
 * @return 插件描述信息
 */
std::string getWebSocketServerPluginInfo() {
    return "WebSocketServer Plugin v1.0 - 提供WebSocket实时通信服务";
}