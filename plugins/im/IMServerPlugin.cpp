#include "app/ApplicationRegistry.h"
#include "IMServer.h"
#include "base/Logger.h"
#include "util/EnhancedConfigReader.h"

/**
 * @brief IM服务器插件
 * 
 * 注册即时通讯服务器到应用注册表
 */

/**
 * @brief IM服务器注册函数
 * @return 是否注册成功
 */
static bool registerIMServer() {
    Logger::info("正在注册IM服务器插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("im_server", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, 
           IThreadPool* pool, EnhancedConfigReader* config) -> std::unique_ptr<ApplicationServer> {
            (void)pool;    // IM服务器暂不使用线程池
            
            Logger::info("创建IM服务器: " + ip + ":" + std::to_string(port));
            auto server = std::make_unique<IMServer>(ip, port, io_type, config);
            
            // 从配置文件读取心跳超时设置
            if (config) {
                int heartbeatTimeout = config->getInt("features.heartbeat.timeout", 90);
                server->setHeartbeatTimeout(heartbeatTimeout);
                Logger::info("IM服务器心跳超时设置为: " + std::to_string(heartbeatTimeout) + "秒");
            }
            
            return server;
        });
    
    if (success) {
        Logger::info("✅ IM服务器插件注册成功");
    } else {
        Logger::error("❌ IM服务器插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册IM服务器
 */
static bool g_imServerRegistered = registerIMServer();
