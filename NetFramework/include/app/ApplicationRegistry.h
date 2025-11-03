#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include "ApplicationServer.h"
#include "base/IOMultiplexer.h"
#include "base/IThreadPool.h"

// 前向声明
class EnhancedConfigReader;

/**
 * @brief 应用注册表 - 插件化框架的核心组件
 * 
 * 功能：
 * 1. 管理所有可用的应用类型
 * 2. 支持应用的动态注册
 * 3. 根据配置动态创建应用实例
 * 4. 提供应用列表查询功能
 * 
 * 设计模式：
 * - 单例模式：全局唯一的注册表实例
 * - 工厂模式：根据类型名创建应用实例
 * - 注册模式：应用自动注册到注册表
 */
class ApplicationRegistry {
public:
    /**
     * @brief 应用创建函数类型定义
     * @param ip 监听IP地址
     * @param port 监听端口
     * @param io_type IO多路复用类型
     * @param pool 线程池指针
     * @param config 配置读取器指针（可选）
     * @return 应用服务器实例的智能指针
     */
    using CreateFunc = std::function<std::unique_ptr<ApplicationServer>(
        const std::string& ip, 
        int port, 
        IOMultiplexer::IOType io_type, 
        IThreadPool* pool,
        EnhancedConfigReader* config
    )>;

    /**
     * @brief 获取注册表单例实例
     * @return 注册表引用
     */
    static ApplicationRegistry& getInstance() {
        static ApplicationRegistry instance;
        return instance;
    }

    /**
     * @brief 注册应用类型
     * @param name 应用类型名称（如 "echo", "sanguosha", "mini_redis"）
     * @param creator 应用创建函数
     * @return 是否注册成功
     */
    bool registerApplication(const std::string& name, CreateFunc creator);

    /**
     * @brief 创建应用实例
     * @param name 应用类型名称
     * @param ip 监听IP地址
     * @param port 监听端口
     * @param io_type IO多路复用类型
     * @param pool 线程池指针
     * @param config 配置读取器指针（可选）
     * @return 应用服务器实例，失败时返回nullptr
     */
    std::unique_ptr<ApplicationServer> createApplication(
        const std::string& name,
        const std::string& ip,
        int port,
        IOMultiplexer::IOType io_type,
        IThreadPool* pool,
        EnhancedConfigReader* config = nullptr
    );

    /**
     * @brief 获取所有可用的应用类型列表
     * @return 应用类型名称列表
     */
    std::vector<std::string> getAvailableApplications() const;

    /**
     * @brief 检查应用类型是否已注册
     * @param name 应用类型名称
     * @return 是否已注册
     */
    bool isApplicationRegistered(const std::string& name) const;

    /**
     * @brief 获取已注册应用的数量
     * @return 应用数量
     */
    size_t getApplicationCount() const;

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    ApplicationRegistry() = default;

    /**
     * @brief 禁用拷贝构造和赋值操作（单例模式）
     */
    ApplicationRegistry(const ApplicationRegistry&) = delete;
    ApplicationRegistry& operator=(const ApplicationRegistry&) = delete;

    /**
     * @brief 应用创建函数映射表
     * key: 应用类型名称
     * value: 对应的创建函数
     */
    std::unordered_map<std::string, CreateFunc> m_creators;
};

/**
 * @brief 应用自动注册器宏
 * 
 * 使用方法：
 * REGISTER_APPLICATION("echo", EchoServer);
 * 
 * 这个宏会在程序启动时自动注册应用类型
 */
#define REGISTER_APPLICATION(name, class_name) \
    static bool register_##class_name() { \
        return ApplicationRegistry::getInstance().registerApplication(name, \
            [](const std::string& ip, int port, IOMultiplexer::IOType io_type, IThreadPool* pool, EnhancedConfigReader* config) { \
                return std::make_unique<class_name>(ip, port, io_type, pool, config); \
            }); \
    } \
    static bool g_##class_name##_registered = register_##class_name();
