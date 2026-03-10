#include "app/ApplicationRegistry.h"
#include "IoTGatewayServer.h"
#include "base/Logger.h"
#include "util/EnhancedConfigReader.h"

/**
 * @brief IoT网关插件
 * 
 * 功能：
 * 1. 从Modbus设备采集数据
 * 2. 通过MQTT发布到云端
 * 3. 支持多设备管理
 */

static bool registerIoTGateway() {
    Logger::info("正在注册IoT网关插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("iot_gateway", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, 
           IThreadPool* pool, EnhancedConfigReader* config) -> std::unique_ptr<ApplicationServer> {
            (void)pool;
            
            Logger::info("创建IoT网关服务器");
            return std::make_unique<IoTGatewayServer>(ip, port, io_type, config);
        });
    
    if (success) {
        Logger::info("✅ IoT网关插件注册成功");
    } else {
        Logger::error("❌ IoT网关插件注册失败");
    }
    
    return success;
}

static bool g_iotGatewayRegistered = registerIoTGateway();
