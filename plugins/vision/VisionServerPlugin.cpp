#include "app/ApplicationRegistry.h"
#include "VisionServer.h"
#include "base/Logger.h"
#include "util/EnhancedConfigReader.h"

/**
 * @brief Vision视觉检测插件
 * 
 * 功能：
 * 1. 视频流采集和处理
 * 2. OpenCV图像检测算法
 * 3. MQTT事件推送
 * 4. HTTP API接口
 */

#ifdef ENABLE_VISION_SERVER

static bool registerVisionServer() {
    Logger::info("正在注册Vision视觉检测插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("vision_server", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, 
           IThreadPool* pool, EnhancedConfigReader* config) -> std::unique_ptr<ApplicationServer> {
            (void)pool;
            
            Logger::info("创建Vision视觉检测服务器");
            return std::make_unique<VisionServer>(ip, port, io_type, config);
        });
    
    if (success) {
        Logger::info("✅ Vision视觉检测插件注册成功");
    } else {
        Logger::error("❌ Vision视觉检测插件注册失败");
    }
    
    return success;
}

static bool g_visionServerRegistered = registerVisionServer();

#else

// 当未启用VISION_SERVER时，注册一个空实现
static bool registerVisionServerDisabled() {
    Logger::warn("Vision Server 功能未编译（需要ENABLE_VISION_SERVER宏和OpenCV）");
    return true;
}

static bool g_visionServerDisabled = registerVisionServerDisabled();

#endif
