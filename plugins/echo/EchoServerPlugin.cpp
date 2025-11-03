#include "app/ApplicationRegistry.h"
#include "server.h"
#include "base/Logger.h"

/**
 * @brief EchoServer插件注册文件
 * 
 * 功能：
 * 1. 将EchoServer注册到应用注册表
 * 2. 支持通过配置文件动态创建EchoServer实例
 * 3. 实现插件化架构的核心机制
 * 
 * 使用方法：
 * 在配置文件中设置 application.type = "echo" 即可使用
 */

/**
 * @brief EchoServer注册函数
 * @return 是否注册成功
 */
static bool registerEchoServer() {
    Logger::info("正在注册EchoServer插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("echo", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) {
            Logger::info("创建EchoServer实例: " + ip + ":" + std::to_string(port));
            return std::make_unique<EchoServer>(ip, port, io_type, pool);
        });
    
    if (success) {
        Logger::info("EchoServer插件注册成功");
    } else {
        Logger::error("EchoServer插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册EchoServer
 * 
 * 这个变量的初始化会在main函数执行前完成，
 * 确保EchoServer在应用注册表中可用
 */
static bool g_echoServerRegistered = registerEchoServer();

/**
 * @brief 获取EchoServer插件信息
 * @return 插件描述信息
 */
std::string getEchoServerPluginInfo() {
    return "EchoServer Plugin v1.0 - 提供TCP回显服务功能";
}
