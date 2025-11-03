#include "app/ApplicationRegistry.h"
#include "DirectRedisServer.h"
#include "base/Logger.h"

/**
 * @brief DirectRedisServer插件注册文件
 * 
 * 直接Redis服务器实现，不依赖复杂的协议栈
 * 专门为Redis协议优化，性能更高
 */

/**
 * @brief DirectRedisServer注册函数
 * @return 是否注册成功
 */
static bool registerDirectRedisServer() {
    Logger::info("正在注册DirectRedisServer插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("direct_redis", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) {
            Logger::info("创建DirectRedisServer实例: " + ip + ":" + std::to_string(port));
            return std::make_unique<DirectRedisServer>(ip, port, io_type, pool);
        });
    
    if (success) {
        Logger::info("DirectRedisServer插件注册成功");
        Logger::info("使用方式: 在配置文件中设置 application.type = direct_redis");
    } else {
        Logger::error("DirectRedisServer插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册DirectRedisServer
 */
static bool g_directRedisServerRegistered = registerDirectRedisServer();

/**
 * @brief 获取DirectRedisServer插件信息
 * @return 插件描述信息
 */
std::string getDirectRedisPluginInfo() {
    return R"(
DirectRedisServer Plugin v1.0
=============================

功能特性:
- 直接处理Redis RESP协议
- 无协议转换开销，性能更高
- 多种数据类型: String, List, Hash
- 支持18+个Redis命令
- 完美的中文字符支持
- 专门为Redis优化的数据流

支持的命令:
- String: SET, GET, DEL
- List: LPUSH, LPOP, LRANGE  
- Hash: HSET, HGET, HKEYS
- 通用: PING, KEYS

配置示例:
application:
  type: direct_redis
network:
  ip: 127.0.0.1
  port: 6379
  io_type: epoll
threading:
  worker_threads: 4

使用客户端:
- redis-cli -h 127.0.0.1 -p 6379
- telnet 127.0.0.1 6379
- nc 127.0.0.1 6379

架构优势:
- 直接继承TcpServer，架构简单
- 原生处理RESP协议，无转换开销
- 专门优化的Redis数据流
- 更高的性能和更低的延迟
)";
}
