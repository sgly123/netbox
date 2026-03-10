#include "ECGDeviceServer.h"
#include "base/Logger.h"
#include <sys/socket.h>
#include <cstring>
#include <cmath>

ECGDeviceServer::ECGDeviceServer(const std::string& ip, int port, 
                                IOMultiplexer::IOType io_type,
                                uint16_t device_id)
    : ApplicationServer(ip, port, io_type, nullptr),
      device_id_(device_id),
      heart_rate_(160),  // ⚠️ 异常心率160 BPM（严重心动过速）
      running_(false),
      rng_(std::random_device{}()),
      noise_dist_(0.0, 500.0)  // ⚠️ 极高噪声标准差500mV（信号严重失真）
{
    // 禁用TCP层心跳包（医疗设备使用自己的数据流）
    setHeartbeatEnabled(false);
    
    Logger::info("  设备ID: " + std::to_string(device_id_));
    Logger::info("  监听地址: " + ip + ":" + std::to_string(port));
    Logger::info("  采样率: 500Hz (每2ms一个数据点)");
    Logger::info("  ⚠️ 初始心率: " + std::to_string(heart_rate_.load()) + " BPM (严重心动过速 - 危险)");
    Logger::info("  ⚠️ 噪声水平: 极高 (信号严重失真 - 需要检查电极)");
}

ECGDeviceServer::~ECGDeviceServer() {
    stop();
}

bool ECGDeviceServer::start() {
    // 启动ApplicationServer
    if (!ApplicationServer::start()) {
        Logger::error("ApplicationServer启动失败");
        return false;
    }
    
    // 启动数据生成线程
    running_ = true;
    data_thread_ = std::thread(&ECGDeviceServer::dataGenerationThread, this);
    
    Logger::info("✅ 心电设备服务器已启动，等待监控客户端连接...");
    return true;
}

void ECGDeviceServer::stop() {
    // 停止数据生成线程
    running_ = false;
    if (data_thread_.joinable()) {
        data_thread_.join();
    }
    
    // 停止ApplicationServer
    ApplicationServer::stop();
    
    Logger::info("心电设备服务器已停止");
}

void ECGDeviceServer::setHeartRate(uint8_t heart_rate) {
    heart_rate_ = heart_rate;
    Logger::info("心率已设置为: " + std::to_string(heart_rate) + " BPM");
}

void ECGDeviceServer::initializeProtocolRouter() {
    // 医疗设备不需要协议路由器（直接发送原始数据）
    Logger::info("心电设备服务器跳过协议路由器初始化");
}

std::string ECGDeviceServer::handleHttpRequest(const std::string& request, int clientFd) {
    (void)request;
    (void)clientFd;
    return "HTTP/1.1 400 Bad Request\r\n\r\nECG Device does not support HTTP";
}

std::string ECGDeviceServer::handleBusinessLogic(const std::string& command, 
                                                const std::vector<std::string>& args) {
    (void)command;
    (void)args;
    return "";
}

bool ECGDeviceServer::parseRequestPath(const std::string& path, std::string& command, 
                                      std::vector<std::string>& args) {
    (void)path;
    (void)command;
    (void)args;
    return false;
}

void ECGDeviceServer::onClientConnected(int clientFd) {
    ApplicationServer::onClientConnected(clientFd);
    Logger::info("📱 监控客户端连接成功 [FD: " + std::to_string(clientFd) + "]");
    Logger::info("   当前连接数: " + std::to_string(m_clients.size()));
}

void ECGDeviceServer::onClientDisconnected(int clientFd) {
    ApplicationServer::onClientDisconnected(clientFd);
    Logger::info("📱 监控客户端断开连接 [FD: " + std::to_string(clientFd) + "]");
    Logger::info("   当前连接数: " + std::to_string(m_clients.size()));
}

void ECGDeviceServer::dataGenerationThread() {
    Logger::info("🔄 数据生成线程已启动");
    
    auto start_time = std::chrono::steady_clock::now();
    uint64_t sample_count = 0;
    
    while (running_) {
        // 计算当前时间（毫秒）
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
        uint32_t timestamp_ms = static_cast<uint32_t>(elapsed.count());
        
        // 生成心电波形数据
        int16_t ecg_value = generateECGWaveform(timestamp_ms);
        
        // 构造心电数据包
        ECGData ecg_data(timestamp_ms, ecg_value, heart_rate_.load());
        
        // 封装成协议帧
        std::vector<char> frame;
        MedicalProtocol::packECGData(device_id_, ecg_data, frame);
        
        // 广播到所有客户端
        broadcastToAllClients(frame);
        
        // 统计信息（每1000个采样点打印一次）
        sample_count++;
        // 日志已禁用以减少输出
        
        // 等待2ms（500Hz采样率）
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    Logger::info("🔄 数据生成线程已停止，共发送 " + std::to_string(sample_count) + " 个采样点");
}

int16_t ECGDeviceServer::generateECGWaveform(uint32_t time_ms) {
    // 心电周期（毫秒）= 60000 / 心率
    double heart_period_ms = 60000.0 / heart_rate_.load();
    
    // 当前在心电周期中的位置（0.0 ~ 1.0）
    double phase = fmod(time_ms, heart_period_ms) / heart_period_ms;
    
    // 基线值
    double baseline = 0.0;
    
    // P波（心房去极化，0.0 ~ 0.15）
    if (phase >= 0.0 && phase < 0.15) {
        double t = (phase - 0.075) / 0.075;
        baseline += 150.0 * exp(-t * t / 0.1);
    }
    
    // QRS波群（心室去极化，0.15 ~ 0.35）
    if (phase >= 0.15 && phase < 0.35) {
        // Q波（负向）
        if (phase >= 0.15 && phase < 0.20) {
            baseline -= 200.0 * sin((phase - 0.15) / 0.05 * M_PI);
        }
        // R波（正向，主波）
        else if (phase >= 0.20 && phase < 0.28) {
            baseline += 1500.0 * sin((phase - 0.20) / 0.08 * M_PI);
        }
        // S波（负向）
        else if (phase >= 0.28 && phase < 0.35) {
            baseline -= 300.0 * sin((phase - 0.28) / 0.07 * M_PI);
        }
    }
    
    // T波（心室复极化，0.45 ~ 0.70）
    if (phase >= 0.45 && phase < 0.70) {
        double t = (phase - 0.575) / 0.125;
        baseline += 250.0 * exp(-t * t / 0.2);
    }
    
    // 添加基线噪声
    double noise = noise_dist_(rng_);
    
    // 限制在有效范围内
    int16_t result = static_cast<int16_t>(std::max(-2048.0, std::min(2047.0, baseline + noise)));
    
    return result;
}

void ECGDeviceServer::broadcastToAllClients(const std::vector<char>& data) {
    if (data.empty()) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (const auto& [fd, _] : m_clients) {
        ssize_t sent = send(fd, data.data(), data.size(), MSG_DONTWAIT);
        
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            Logger::warn("发送数据失败 [FD: " + std::to_string(fd) + "]");
        }
    }
}
