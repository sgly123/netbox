#include "app/ApplicationRegistry.h"
#include "RedisApplicationServer.h"
#include "base/Logger.h"

/**
 * @brief RedisApplicationServer插件注册文件
 * 
 * 将Redis功能集成到NetBox框架中
 * 复用框架的网络层、协议层、线程池等基础设施
 */

/**
 * @brief RedisApplicationServer注册函数
 * @return 是否注册成功
 */
static bool registerRedisApplicationServer() {
    Logger::info("正在注册RedisApplicationServer插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("redis_app", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) {
            Logger::info("创建RedisApplicationServer实例: " + ip + ":" + std::to_string(port));
            return std::make_unique<RedisApplicationServer>(ip, port, io_type, pool);
        });
    
    if (success) {
        Logger::info("RedisApplicationServer插件注册成功");
        Logger::info("使用方式: 在配置文件中设置 application.type = redis_app");
    } else {
        Logger::error("RedisApplicationServer插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册RedisApplicationServer
 */
static bool g_redisApplicationServerRegistered = registerRedisApplicationServer();

/**
 * @brief 获取RedisApplicationServer插件信息
 * @return 插件描述信息
 */
std::string getRedisApplicationPluginInfo() {
    return R"(
RedisApplicationServer Plugin v1.0
==================================

功能特性:
- 完整的Redis协议支持 (RESP)
- 多种数据类型: String, List, Hash
- 支持18+个Redis命令
- 完美的中文字符支持
- 集成NetBox框架的所有优势

支持的命令:
- String: SET, GET, DEL
- List: LPUSH, LPOP, LRANGE  
- Hash: HSET, HGET, HKEYS
- 通用: PING, KEYS

配置示例:
application:
  type: redis_app
network:
  ip: 127.0.0.1
  port: 6379
  io_type: epoll
threading:
  worker_threads: 4

使用客户端:
- redis-cli -h 127.0.0.1 -p 6379
- telnet 127.0.0.1 6379
- 自定义客户端 (支持SimpleHeaderProtocol)
)";
}
