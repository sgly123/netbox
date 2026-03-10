#include "app/include/server.h"
#include "base/Logger.h"
#include "base/AsyncConsoleLogger.h"
#include "base/ThreadPool.h"
#include "base/DoubleLockThreadPool.h"
#include "app/ApplicationRegistry.h"
#include "util/EnhancedConfigReader.h"
#include <thread>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>

/**
 * @brief 全局停止标志，用于优雅退出
 * volatile 确保多线程环境下的可见性
 */
volatile std::sig_atomic_t g_stopFlag = 0;

/**
 * @brief 信号处理函数，处理 SIGINT 和 SIGTERM 信号
 * @param signum 信号编号
 * 
 * 功能：
 * 1. 设置全局停止标志
 * 2. 记录退出日志
 * 3. 触发主循环退出，实现优雅关闭
 */
void signalHandler(int signum) {
    (void)signum; // 避免未使用参数警告
    g_stopFlag = 1;
    Logger::info("收到退出信号，准备优雅退出...");
}

/**
 * @brief 主程序入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码
 * 
 * 程序流程：
 * 1. 解析命令行参数，获取配置文件路径
 * 2. 加载配置文件，获取服务器参数
 * 3. 初始化日志系统
 * 4. 注册信号处理器
 * 5. 创建线程池和服务器实例
 * 6. 启动服务器并进入主循环
 * 7. 优雅退出和资源清理
 */
int main(int argc, char* argv[]) {
    // 配置文件路径，支持命令行参数指定
    std::string config_path = "../config/config.yaml";  // 默认配置文件

    // 解析命令行参数
    if (argc > 1) {
        config_path = argv[1];
        std::cout << "使用指定的配置文件: " << config_path << std::endl;
    } else {
        std::cout << "使用默认配置文件: " << config_path << std::endl;
    }

    // 加载配置文件
    EnhancedConfigReader config;
    if (!config.load(config_path)) {
        std::cerr << "无法读取配置文件: " << config_path << std::endl;
        std::cerr << "请确保配置文件存在且格式正确" << std::endl;
        return 1;
    }

    // 从配置文件读取应用类型和服务器参数
    std::string app_type = config.getString("application.type", "echo");        // 应用类型
    
    // 去除可能的引号
    if (!app_type.empty() && app_type.front() == '"' && app_type.back() == '"') {
        app_type = app_type.substr(1, app_type.length() - 2);
    }
    
    std::string ip = config.getString("network.ip", "127.0.0.1");              // 监听IP
    int port = config.getInt("network.port", 8888);                            // 监听端口
    int thread_num = config.getInt("threading.worker_threads", 10);            // 工作线程数
    std::string io_type_str = config.getString("network.io_type", "epoll");    // IO多路复用类型

    // 解析IO多路复用类型
    IOMultiplexer::IOType io_type = IOMultiplexer::IOType::EPOLL;  // 默认使用EPOLL
    if (io_type_str == "select") {
        io_type = IOMultiplexer::IOType::SELECT;
    } else if (io_type_str == "poll") {
        io_type = IOMultiplexer::IOType::POLL;
    } else if (io_type_str == "epoll") {
        io_type = IOMultiplexer::IOType::EPOLL;
    } else {
        std::cerr << "未知的IO类型: " << io_type_str << ", 使用默认的EPOLL" << std::endl;
    }

    // 初始化异步日志系统
    Logger::setInstance(new AsyncConsoleLogger());

    // 注册信号处理器，支持优雅退出
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 创建线程池实例
    auto pool = new DoubleLockThreadPool(thread_num);

    // 🎯 关键：动态创建应用服务器
    Logger::info("正在创建应用: " + app_type);
    auto& registry = ApplicationRegistry::getInstance();

    // 显示可用的应用类型
    auto availableApps = registry.getAvailableApplications();
    Logger::info("可用的应用类型: ");
    for (const auto& app : availableApps) {
        Logger::info("  - " + app);
    }

    // 创建应用实例，传入配置
    auto server = registry.createApplication(app_type, ip, port, io_type, pool, &config);

    // 检查应用是否创建成功
    if (!server) {
        Logger::error("未知的应用类型: " + app_type);
        Logger::info("请检查配置文件中的 application.type 设置");
        delete pool;
        return -1;
    }

    // 启动服务器
    if (!server->start()) {
        Logger::error(app_type + " 服务器启动失败！");
        delete pool;
        return -1;
    }

    Logger::info(app_type + " 服务器已启动，等待客户端连接...");
    Logger::info("服务器配置: " + ip + ":" + std::to_string(port) + " (IO类型: " + io_type_str + ", 线程数: " + std::to_string(thread_num) + ")");

    // 主循环：保持程序运行，支持优雅退出
    while (!g_stopFlag) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 优雅退出流程
    Logger::info("正在关闭 " + app_type + " 服务器...");
    server->stop();

    Logger::info("服务器已关闭，清理资源...");
    server.reset();  // 释放服务器（智能指针自动管理）
    delete pool;     // 释放线程池

    Logger::info("退出完成。");
    return 0;
}
