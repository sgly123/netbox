#include "IoTGatewayServerSimple.h"
#include "base/Logger.h"
#include "third_party/nlohmann/json.hpp"
#include <cstring>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
#endif

using json = nlohmann::json;

IoTGatewayServerSimple::IoTGatewayServerSimple(const std::string& ip, int port,
                                               IOMultiplexer::IOType io_type,
                                               EnhancedConfigReader* config)
    : ApplicationServer(ip, port, io_type)
    , config_(config)
    , running_(false)
    , transaction_id_(0) {
    Logger::info("创建IoT网关服务器(简化版)");
}

IoTGatewayServerSimple::~IoTGatewayServerSimple() {
    stop();
}

bool IoTGatewayServerSimple::start() {
    Logger::info("启动IoT网关服务器...");
    
    if (!initModbusDevices()) {
        Logger::error("Modbus设备初始化失败");
        return false;
    }
    
    if (!ApplicationServer::start()) {
        Logger::error("TCP服务器启动失败");
        return false;
    }
    
    running_ = true;
    collection_thread_ = std::thread(&IoTGatewayServerSimple::dataCollectionThread, this);
    
    Logger::info("✅ IoT网关服务器启动成功");
    return true;
}

void IoTGatewayServerSimple::stop() {
    Logger::info("停止IoT网关服务器...");
    running_ = false;
    
    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }
    
    ApplicationServer::stop();
    Logger::info("IoT网关服务器已停止");
}

bool IoTGatewayServerSimple::initModbusDevices() {
    Logger::info("初始化Modbus设备...");
    
    try {
        auto device_list = config_->getArray("devices");
        
        for (const auto& dev_config : device_list) {
            ModbusDeviceConfig device;
            device.id = dev_config["id"].get<std::string>();
            device.name = dev_config["name"].get<std::string>();
            device.host = dev_config["host"].get<std::string>();
            device.port = dev_config["port"].get<int>();
            device.slave_id = dev_config["slave_id"].get<int>();
            device.start_address = dev_config["start_address"].get<int>();
            device.register_count = dev_config["register_count"].get<int>();
            device.poll_interval = dev_config["poll_interval"].get<int>();
            
            if (dev_config.contains("register_names")) {
                for (const auto& name : dev_config["register_names"]) {
                    device.register_names.push_back(name.get<std::string>());
                }
            }
            
            devices_.push_back(device);
            Logger::info("添加设备: " + device.name + " (" + device.host + ":" + std::to_string(device.port) + ")");
        }
        
        Logger::info("✅ 加载了 " + std::to_string(devices_.size()) + " 个设备");
        return !devices_.empty();
        
    } catch (const std::exception& e) {
        Logger::error("读取设备配置失败: " + std::string(e.what()));
        return false;
    }
}

void IoTGatewayServerSimple::dataCollectionThread() {
    Logger::info("数据采集线程启动");
    
    while (running_) {
        for (const auto& device : devices_) {
            try {
                auto registers = readModbusRegisters(device);
                
                if (registers.empty()) {
                    Logger::warn("设备 " + device.name + " 数据读取失败");
                    continue;
                }
                
                std::vector<DeviceDataPoint> data_points;
                int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                for (size_t i = 0; i < registers.size() && i < device.register_names.size(); ++i) {
                    DeviceDataPoint point;
                    point.name = device.register_names[i];
                    point.value = convertRegisterValue(registers[i], point.name);
                    point.unit = getUnit(point.name);
                    point.timestamp = timestamp;
                    point.quality = "good";
                    data_points.push_back(point);
                }
                
                {
                    std::lock_guard<std::mutex> lock(cache_mutex_);
                    data_cache_[device.id] = data_points;
                }
                
                // 广播数据给所有订阅者
                broadcastData(device.id, data_points);
                
                Logger::debug("设备 " + device.name + " 数据采集成功: " + std::to_string(data_points.size()) + " 个数据点");
                
            } catch (const std::exception& e) {
                Logger::error("设备 " + device.name + " 采集异常: " + std::string(e.what()));
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    Logger::info("数据采集线程退出");
}

std::vector<uint16_t> IoTGatewayServerSimple::readModbusRegisters(const ModbusDeviceConfig& device) {
    int sockfd = connectToModbusDevice(device.host, device.port);
    if (sockfd < 0) {
        return {};
    }
    
    auto request = buildModbusRequest(device.slave_id, device.start_address, device.register_count);
    
    ssize_t sent = ::send(sockfd, reinterpret_cast<const char*>(request.data()), request.size(), 0);
    if (sent != static_cast<ssize_t>(request.size())) {
        Logger::error("Modbus请求发送失败");
        closeModbusConnection(sockfd);
        return {};
    }
    
    std::vector<uint8_t> response(1024);
    ssize_t received = ::recv(sockfd, reinterpret_cast<char*>(response.data()), response.size(), 0);
    if (received <= 0) {
        Logger::error("Modbus响应接收失败");
        closeModbusConnection(sockfd);
        return {};
    }
    
    response.resize(received);
    closeModbusConnection(sockfd);
    
    return parseModbusResponse(response);
}

std::vector<uint8_t> IoTGatewayServerSimple::buildModbusRequest(int slave_id, int start_addr, int count) {
    std::vector<uint8_t> request;
    
    uint16_t trans_id = transaction_id_++;
    request.push_back((trans_id >> 8) & 0xFF);
    request.push_back(trans_id & 0xFF);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x06);
    request.push_back(static_cast<uint8_t>(slave_id));
    request.push_back(0x03);
    request.push_back((start_addr >> 8) & 0xFF);
    request.push_back(start_addr & 0xFF);
    request.push_back((count >> 8) & 0xFF);
    request.push_back(count & 0xFF);
    
    return request;
}

std::vector<uint16_t> IoTGatewayServerSimple::parseModbusResponse(const std::vector<uint8_t>& response) {
    std::vector<uint16_t> registers;
    
    if (response.size() < 9) {
        Logger::error("Modbus响应长度不足");
        return registers;
    }
    
    uint8_t function_code = response[7];
    if (function_code != 0x03) {
        Logger::error("Modbus功能码错误: " + std::to_string(function_code));
        return registers;
    }
    
    uint8_t byte_count = response[8];
    
    for (int i = 0; i < byte_count / 2; ++i) {
        uint16_t value = (response[9 + i * 2] << 8) | response[10 + i * 2];
        registers.push_back(value);
    }
    
    return registers;
}

double IoTGatewayServerSimple::convertRegisterValue(uint16_t rawValue, const std::string& name) {
    if (name.find("temperature") != std::string::npos) return rawValue / 10.0;
    if (name.find("humidity") != std::string::npos) return rawValue / 10.0;
    if (name.find("pressure") != std::string::npos) return rawValue / 10.0;
    if (name.find("flow") != std::string::npos) return rawValue / 10.0;
    return static_cast<double>(rawValue);
}

std::string IoTGatewayServerSimple::getUnit(const std::string& name) {
    if (name.find("temperature") != std::string::npos) return "C";
    if (name.find("humidity") != std::string::npos) return "%";
    if (name.find("pressure") != std::string::npos) return "kPa";
    if (name.find("flow") != std::string::npos) return "L/min";
    if (name.find("speed") != std::string::npos) return "RPM";
    return "";
}

int IoTGatewayServerSimple::connectToModbusDevice(const std::string& host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        Logger::error("创建socket失败");
        return -1;
    }
    
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::warn("连接Modbus设备失败: " + host + ":" + std::to_string(port));
        CLOSE_SOCKET(sockfd);
        return -1;
    }
    
    return sockfd;
}

void IoTGatewayServerSimple::closeModbusConnection(int sockfd) {
    if (sockfd >= 0) {
        CLOSE_SOCKET(sockfd);
    }
}

void IoTGatewayServerSimple::broadcastData(const std::string& deviceId, const std::vector<DeviceDataPoint>& data) {
    json j_msg;
    j_msg["type"] = "data";
    j_msg["device_id"] = deviceId;
    j_msg["data"] = json::array();
    
    for (const auto& point : data) {
        json j_point;
        j_point["name"] = point.name;
        j_point["value"] = point.value;
        j_point["unit"] = point.unit;
        j_point["timestamp"] = point.timestamp;
        j_point["quality"] = point.quality;
        j_msg["data"].push_back(j_point);
    }
    
    std::string payload = j_msg.dump() + "\n";
    
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    for (int fd : subscribers_) {
        sendToClient(fd, payload.c_str(), payload.size());
    }
}

void IoTGatewayServerSimple::onPacketReceived(int clientFd, const std::vector<char>& packet) {
    std::string request(packet.begin(), packet.end());
    
    json response;
    response["status"] = "ok";
    response["devices"] = json::array();
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    for (const auto& [device_id, data_points] : data_cache_) {
        json device_data;
        device_data["device_id"] = device_id;
        device_data["data"] = json::array();
        
        for (const auto& point : data_points) {
            json j_point;
            j_point["name"] = point.name;
            j_point["value"] = point.value;
            j_point["unit"] = point.unit;
            j_point["timestamp"] = point.timestamp;
            device_data["data"].push_back(j_point);
        }
        
        response["devices"].push_back(device_data);
    }
    
    std::string response_str = response.dump() + "\n";
    sendToClient(clientFd, response_str.c_str(), response_str.size());
}

void IoTGatewayServerSimple::onClientConnected(int clientFd) {
    Logger::info("客户端连接: " + std::to_string(clientFd));
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    subscribers_.push_back(clientFd);
}

void IoTGatewayServerSimple::onClientDisconnected(int clientFd) {
    Logger::info("客户端断开: " + std::to_string(clientFd));
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    subscribers_.erase(std::remove(subscribers_.begin(), subscribers_.end(), clientFd), subscribers_.end());
}
