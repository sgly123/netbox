#include "app/ApplicationRegistry.h"
#include "base/Logger.h"
#include <algorithm>

bool ApplicationRegistry::registerApplication(const std::string& name, CreateFunc creator) {
    if (name.empty()) {
        Logger::error("应用注册失败：应用名称不能为空");
        return false;
    }
    
    if (!creator) {
        Logger::error("应用注册失败：创建函数不能为空，应用名称: " + name);
        return false;
    }
    
    // 检查是否已经注册
    if (m_creators.find(name) != m_creators.end()) {
        Logger::warn("应用类型已存在，将覆盖原有注册: " + name);
    }
    
    // 注册应用
    m_creators[name] = creator;
    Logger::info("应用注册成功: " + name);
    
    return true;
}

std::unique_ptr<ApplicationServer> ApplicationRegistry::createApplication(
    const std::string& name,
    const std::string& ip,
    int port,
    IOMultiplexer::IOType io_type,
    IThreadPool* pool,
    EnhancedConfigReader* config) {
    
    // 查找应用创建函数
    auto it = m_creators.find(name);
    if (it == m_creators.end()) {
        Logger::error("未找到应用类型: " + name);
        return nullptr;
    }
    
    try {
        // 调用创建函数，传入配置参数
        auto app = it->second(ip, port, io_type, pool, config);
        if (app) {
            Logger::info("应用创建成功: " + name + " (" + ip + ":" + std::to_string(port) + ")");
        } else {
            Logger::error("应用创建失败: " + name);
        }
        return app;
    } catch (const std::exception& e) {
        Logger::error("应用创建异常: " + name + ", 错误: " + e.what());
        return nullptr;
    } catch (...) {
        Logger::error("应用创建未知异常: " + name);
        return nullptr;
    }
}

std::vector<std::string> ApplicationRegistry::getAvailableApplications() const {
    std::vector<std::string> applications;
    applications.reserve(m_creators.size());
    
    for (const auto& pair : m_creators) {
        applications.push_back(pair.first);
    }
    
    // 按字母顺序排序
    std::sort(applications.begin(), applications.end());
    
    return applications;
}

bool ApplicationRegistry::isApplicationRegistered(const std::string& name) const {
    return m_creators.find(name) != m_creators.end();
}

size_t ApplicationRegistry::getApplicationCount() const {
    return m_creators.size();
}
