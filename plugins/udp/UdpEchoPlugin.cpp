#include "app/ApplicationRegistry.h"
#include "UdpEchoServer.h"
#include "base/Logger.h"

// 前向声明
class EnhancedConfigReader;

/**
 * @brief UDP Echo Server插件注册文件
 * 
 * 功能：
 * 1. 将UDP Echo Server注册到应用注册表
 * 2. 支持通过配置文件动态创建UDP Echo Server实例
 * 3. 扩展插件化架构支持UDP协议
 * 
 * 使用方法：
 * 在配置文件中设置 application.type = "udp_echo" 即可使用
 */

/**
 * @brief UDP Echo Server适配器类
 * 由于UDP协议不同于TCP，我们创建一个适配器来集成到现有的ApplicationServer架构
 */
class UdpEchoServerAdapter : public ApplicationServer {
public:
    UdpEchoServerAdapter(const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool)
        : ApplicationServer(ip, port, io_type, pool)
        , m_udpServer(std::make_unique<UdpEchoServer>(ip, port, io_type))
        , m_ip(ip)
        , m_port(port)
        , m_ioType(io_type)
    {
        Logger::info("UDP Echo Server适配器创建成功");
    }

    virtual ~UdpEchoServerAdapter() {
        stop();
        Logger::info("UDP Echo Server适配器销毁");
    }

    bool start() override {
        // 启动UDP服务器而不是TCP服务器
        bool success = m_udpServer->startEchoServer();
        if (success) {
            Logger::info("UDP Echo Server启动成功 " + m_ip + ":" + std::to_string(m_port));
        }
        return success;
    }

    void stop() override {
        if (m_udpServer) {
            m_udpServer->stop();
            Logger::info("UDP Echo Server已停止");
        }
        // 不调用基类的stop()，因为我们没有使用TCP
    }

protected:
    // 实现ApplicationServer要求的纯虚函数
    void initializeProtocolRouter() override {
        // UDP Echo服务器不需要协议路由器
        Logger::debug("UDP Echo Server不使用协议路由器");
    }

    std::string handleHttpRequest(const std::string& request, int clientFd) override {
        // UDP Echo服务器不处理HTTP请求
        (void)request;  // 避免未使用参数警告
        (void)clientFd; // 避免未使用参数警告
        Logger::warn("UDP Echo Server不支持HTTP请求");
        return "";
    }

    std::string handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) override {
        // UDP Echo服务器的业务逻辑在UdpEchoServer中处理
        (void)command;  // 避免未使用参数警告
        (void)args;     // 避免未使用参数警告
        Logger::debug("UDP Echo Server业务逻辑由UdpEchoServer处理");
        return "";
    }

    bool parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) override {
        // UDP Echo服务器不需要解析HTTP路径
        (void)path;     // 避免未使用参数警告
        (void)command;  // 避免未使用参数警告
        (void)args;     // 避免未使用参数警告
        return false;
    }

public:

    // UDP特定功能
    void printStats() const {
        if (m_udpServer) {
            m_udpServer->printStats();
        }
    }

    void cleanupInactiveClients(int timeout_seconds = 300) {
        if (m_udpServer) {
            m_udpServer->cleanupInactiveClients(timeout_seconds);
        }
    }

private:
    std::unique_ptr<UdpEchoServer> m_udpServer;
    std::string m_ip;
    int m_port;
    IOMultiplexer::IOType m_ioType;
};

/**
 * @brief UDP Echo Server注册函数
 * @return 是否注册成功
 */
static bool registerUdpEchoServer() {
    Logger::info("正在注册UDP Echo Server插件...");
    
    bool success = ApplicationRegistry::getInstance().registerApplication("udp_echo", 
        [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) {
            (void)config;  // 避免未使用参数警告
            Logger::info("创建UDP Echo Server实例: " + ip + ":" + std::to_string(port));
            return std::make_unique<UdpEchoServerAdapter>(ip, port, io_type, pool);
        });
    
    if (success) {
        Logger::info("UDP Echo Server插件注册成功");
    } else {
        Logger::error("UDP Echo Server插件注册失败");
    }
    
    return success;
}

/**
 * @brief 全局静态变量，程序启动时自动注册UDP Echo Server
 * 
 * 这个变量的初始化会在main函数执行前完成，
 * 确保UDP Echo Server在应用注册表中可用
 */
static bool g_udpEchoServerRegistered = registerUdpEchoServer();

/**
 * @brief 获取UDP Echo Server插件信息
 * @return 插件描述信息
 */
std::string getUdpEchoServerPluginInfo() {
    return "UDP Echo Server Plugin v1.0 - 提供UDP回显服务功能，支持高性能无连接通信";
}