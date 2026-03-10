#pragma once

#include "ApplicationServer.h"
#include "MedicalProtocol.h"
#include <thread>
#include <atomic>
#include <random>

/**
 * @brief 心电监护仪设备服务器
 * 
 * 功能：
 * 1. 模拟心电监护仪，生成ECG波形数据
 * 2. 通过TCP发送数据到监控客户端
 * 3. 支持多客户端同时连接
 * 4. 500Hz采样率（每2ms发送一个数据点）
 * 
 * 继承关系：
 * TcpServer → ApplicationServer → ECGDeviceServer
 */
class ECGDeviceServer : public ApplicationServer {
public:
    /**
     * @brief 构造函数
     * @param ip 监听IP地址
     * @param port 监听端口
     * @param io_type IO多路复用类型
     * @param device_id 设备ID（唯一标识）
     */
    ECGDeviceServer(const std::string& ip, int port, 
                   IOMultiplexer::IOType io_type,
                   uint16_t device_id = 1001);
    
    /**
     * @brief 析构函数
     */
    ~ECGDeviceServer() override;
    
    /**
     * @brief 启动设备服务器
     */
    bool start() override;
    
    /**
     * @brief 停止设备服务器
     */
    void stop() override;
    
    /**
     * @brief 设置心率（用于模拟不同状态）
     * @param heart_rate 心率值（BPM）
     */
    void setHeartRate(uint8_t heart_rate);
    
    /**
     * @brief 获取当前心率
     */
    uint8_t getHeartRate() const { return heart_rate_.load(); }
    
    /**
     * @brief 获取设备ID
     */
    uint16_t getDeviceId() const { return device_id_; }

protected:
    /**
     * @brief 初始化协议路由器
     */
    void initializeProtocolRouter() override;
    
    /**
     * @brief HTTP请求处理（医疗设备不支持HTTP）
     */
    std::string handleHttpRequest(const std::string& request, int clientFd) override;
    
    /**
     * @brief 业务逻辑处理
     */
    std::string handleBusinessLogic(const std::string& command, 
                                   const std::vector<std::string>& args) override;
    
    /**
     * @brief 解析请求路径
     */
    bool parseRequestPath(const std::string& path, std::string& command, 
                         std::vector<std::string>& args) override;
    
    /**
     * @brief 客户端连接回调
     */
    void onClientConnected(int clientFd) override;
    
    /**
     * @brief 客户端断开回调
     */
    void onClientDisconnected(int clientFd) override;

private:
    /**
     * @brief 数据生成线程函数
     */
    void dataGenerationThread();
    
    /**
     * @brief 生成模拟心电波形数据
     * @param time_ms 当前时间（毫秒）
     * @return 心电值（-2048 ~ 2047）
     */
    int16_t generateECGWaveform(uint32_t time_ms);
    
    /**
     * @brief 广播数据到所有连接的客户端
     * @param data 数据内容
     */
    void broadcastToAllClients(const std::vector<char>& data);

private:
    uint16_t device_id_;              // 设备ID
    std::atomic<uint8_t> heart_rate_; // 心率（BPM）
    std::atomic<bool> running_;       // 运行标志
    std::thread data_thread_;         // 数据生成线程
    
    // 随机数生成器（用于模拟噪声）
    std::mt19937 rng_;
    std::normal_distribution<double> noise_dist_;
};
