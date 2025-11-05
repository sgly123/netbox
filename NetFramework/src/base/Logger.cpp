#include "base/Logger.h"
#include <iostream>
#include <memory> 
#include <mutex>
#include <chrono>
#include <ctime>    
#include <iomanip>  

// 全局Logger指针和互斥锁，保证线程安全
static std::unique_ptr<Logger> g_logger = nullptr;
static std::mutex g_logger_mutex;

// 获取全局Logger实例，默认使用ConsoleLogger
Logger* Logger::getInstance() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (!g_logger) g_logger = std::make_unique<ConsoleLogger>();
    return g_logger.get();
}

// 设置全局Logger实例（可切换为其他实现）
void Logger::setInstance(Logger* logger) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    if (logger != g_logger.get()) {  
        g_logger.reset(logger);
    }
}

// 便捷静态接口，直接输出不同级别日志
void Logger::debug(const std::string& msg) { getInstance()->log(LogLevel::DEBUG, msg); }
void Logger::info(const std::string& msg)  { getInstance()->log(LogLevel::INFO, msg); }
void Logger::warn(const std::string& msg)  { getInstance()->log(LogLevel::WARN, msg); }
void Logger::error(const std::string& msg) { getInstance()->log(LogLevel::ERROR, msg); }

// 控制台日志实现，输出到终端
void ConsoleLogger::log(LogLevel level, const std::string& msg) {
    // 日志级别字符串
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto value = now_ms.time_since_epoch();
    long long ms = value.count() % 1000;  // 提取毫秒部分
 // 2. 转换为本地时间（带时区）
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_tm = std::localtime(&now_time);  // 本地时区（如北京时间）
 // 注意：localtime线程不安全，若需线程安全可改用localtime_r（POSIX）或std::chrono::current_zone（C++20）

 // 3. 格式化时间字符串（示例：2023-10-01 15:30:45.123）
    char time_buf[32];
 std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", local_tm);
 std::string time_str = std::string(time_buf) + "." + std::to_string(ms);

    static const char* levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::cout << "[" << levelStr[(int)level] << "] " << msg << std::endl;
} 