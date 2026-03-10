#include "app/ApplicationRegistry.h"
#include "ECGDeviceServer.h"
#include "base/Logger.h"
#include "util/EnhancedConfigReader.h"

/**
 * @brief 心电设备插件
 * 
 * 遵循netbox插件注册机制
 */

/**
 * @brief 心电设备注册函数
 * @return 是否注册成功
 */
static bool registerECGDevice() {
    Logger::info("正在注册心电设备插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("ecg_device", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, 
           IThreadPool* pool, EnhancedConfigReader* config) -> std::unique_ptr<ApplicationServer> {
            (void)pool;  // 心电设备不需要线程池
            
            // 从配置文件读取设备ID
            uint16_t device_id = 1001;
            if (config) {
                device_id = config->getInt("device.id", 1001);
            }
            
            Logger::info("创建心电设备服务器，设备ID: " + std::to_string(device_id));
            return std::make_unique<ECGDeviceServer>(ip, port, io_type, device_id);
        });
    
    if (success) {
        Logger::info("✅ 心电设备插件注册成功");
    } else {
        Logger::error("❌ 心电设备插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册心电设备
 */
static bool g_ecgDeviceRegistered = registerECGDevice();
