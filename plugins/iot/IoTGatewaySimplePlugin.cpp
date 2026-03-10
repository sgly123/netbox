#include "app/ApplicationRegistry.h"
#include "IoTGatewayServerSimple.h"
#include "base/Logger.h"

/**
 * @brief IoT网关插件（简化版，不依赖MQTT库）
 */
static bool registerIoTGatewaySimple() {
    Logger::info("正在注册IoT网关插件(简化版)...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("iot_gateway", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, 
           IThreadPool* pool, EnhancedConfigReader* config) -> std::unique_ptr<ApplicationServer> {
            (void)pool;
            Logger::info("创建IoT网关服务器");
            return std::make_unique<IoTGatewayServerSimple>(ip, port, io_type, config);
        });
    
    if (success) {
        Logger::info("✅ IoT网关插件注册成功");
    } else {
        Logger::error("❌ IoT网关插件注册失败");
    }
    
    return success;
}

static bool g_iotGatewayRegistered = registerIoTGatewaySimple();
