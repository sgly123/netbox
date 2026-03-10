#include "IoTGatewayServer.h"
#include "base/Logger.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <chrono>
#include <sstream>
// IoT Gateway（物联网网关） = 工业设备监控与控制
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
#endif

using json = nlohmann::json;

IoTGatewayServer::IoTGatewayServer(const std::string& ip, int port,
                                   IOMultiplexer::IOType io_type,
                                   EnhancedConfigReader* config)
    : ApplicationServer(ip, port, io_type, nullptr)
    , config_(config)
    , mqtt_qos_(1)
    , running_(false)
    , transaction_id_(0) {
    Logger::info("创建IoT网关服务器");
}

IoTGatewayServer::~IoTGatewayServer() {
    stop();
}

bool IoTGatewayServer::start() {
    Logger::info("启动IoT网关服务器...");
    
    if (!initMQTT()) {
        Logger::error("MQTT初始化失败");
        return false;
    }
    
    if (!initModbusDevices()) {
        Logger::error("Modbus设备初始化失败");
        return false;
    }
    
    if (!ApplicationServer::start()) {
        Logger::error("TCP服务器启动失败");
        return false;
    }
    
    running_ = true;
    collection_thread_ = std::thread(&IoTGatewayServer::dataCollectionThread, this);
    
    Logger::info("IoT网关服务器启动成功");
    return true;
}

void IoTGatewayServer::stop() {
    Logger::info("停止IoT网关服务器...");
    running_ = false;
    
    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }
    
#ifdef ENABLE_IOT_GATEWAY
    if (mqtt_client_ && mqtt_client_->is_connected()) {
        mqtt_client_->disconnect()->wait();
    }
#endif
    
    ApplicationServer::stop();
}

void IoTGatewayServer::initializeProtocolRouter() {
    // IoT网关通过 handleHttpRequest 方法处理HTTP请求
    // 不需要初始化协议路由器
    Logger::info("IoT网关初始化完成，HTTP命令式API已就绪");
}

void IoTGatewayServer::onDataReceived(int clientFd, const char* data, size_t len) {
    Logger::info("IoT网关收到客户端" + std::to_string(clientFd) + "的数据，长度: " + std::to_string(len));
    
    m_currentClientFd = clientFd;
    
    // 检查是否是HTTP请求（以GET, POST等开头）
    std::string request(data, len);
    if (request.substr(0, 3) == "GET" || request.substr(0, 4) == "POST" || 
        request.substr(0, 3) == "PUT" || request.substr(0, 6) == "DELETE") {
        
        Logger::debug("检测到HTTP请求，调用handleHttpRequest处理");
        std::string response = handleHttpRequest(request, clientFd);
        
        // 发送响应
        ssize_t sent = ::send(clientFd, response.c_str(), response.size(), 0);
        if (sent < 0) {
            Logger::error("发送HTTP响应失败");
        } else {
            Logger::info("成功发送HTTP响应: " + std::to_string(sent) + " 字节");
        }
    } else {
        Logger::warn("收到非HTTP请求，忽略");
    }
}

std::string IoTGatewayServer::handleHttpRequest(const std::string& request, int clientFd) {
    (void)clientFd;
    
    // 解析 HTTP 请求行：GET /path HTTP/1.1
    size_t firstSpace = request.find(' ');
    size_t secondSpace = request.find(' ', firstSpace + 1);
    
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        json error;
        error["success"] = false;
        error["error"] = "Invalid HTTP request";
        std::string response = "HTTP/1.1 400 Bad Request\r\n"
                             "Content-Type: application/json\r\n"
                             "Connection: close\r\n\r\n" + error.dump();
        return response;
    }
    
    std::string method = request.substr(0, firstSpace);
    std::string path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    
    Logger::debug("HTTP请求: " + method + " " + path);
    
    json result;
    std::string jsonBody;
    
    // 路由到不同的命令式API
    if (path == "/devices") {
        jsonBody = handleBusinessLogic("devices", {});
    } else if (path.find("/read/") == 0) {
        // 解析 /read/<device_id> 或 /read/<device_id>/<register>
        std::string subPath = path.substr(6); // 去掉 "/read/"
        size_t slash = subPath.find('/');
        
        if (slash == std::string::npos) {
            // /read/<device_id>
            jsonBody = handleBusinessLogic("read", {subPath});
        } else {
            // /read/<device_id>/<register>
            std::string deviceId = subPath.substr(0, slash);
            std::string registerName = subPath.substr(slash + 1);
            jsonBody = handleBusinessLogic("read", {deviceId, registerName});
        }
    } else if (path.find("/write/") == 0) {
        // 解析 /write/<device_id>/<register>/<value>
        std::string subPath = path.substr(7); // 去掉 "/write/"
        size_t slash1 = subPath.find('/');
        size_t slash2 = subPath.find('/', slash1 + 1);
        
        if (slash1 == std::string::npos || slash2 == std::string::npos) {
            result["success"] = false;
            result["error"] = "Invalid write path format. Expected: /write/<device_id>/<register>/<value>";
            jsonBody = result.dump();
        } else {
            std::string deviceId = subPath.substr(0, slash1);
            std::string registerName = subPath.substr(slash1 + 1, slash2 - slash1 - 1);
            std::string value = subPath.substr(slash2 + 1);
            jsonBody = handleBusinessLogic("write", {deviceId, registerName, value});
        }
    } else if (path == "/status") {
        jsonBody = handleBusinessLogic("status", {});
    } else {
        result["success"] = false;
        result["error"] = "Unknown API endpoint";
        result["path"] = path;
        jsonBody = result.dump();
    }
    
    // 构建 HTTP 响应
    std::string response = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: application/json\r\n"
                         "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n"
                         "Connection: close\r\n"
                         "Access-Control-Allow-Origin: *\r\n\r\n" + jsonBody;
    
    return response;
}

std::string IoTGatewayServer::handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) {
    json response;
    
    // 命令: /devices - 获取所有设备列表
    if (command == "/devices" || command == "devices") {
        response["success"] = true;
        response["devices"] = json::array();
        
        for (const auto& device : devices_) {
            json dev;
            dev["id"] = device.id;
            dev["name"] = device.name;
            dev["host"] = device.host;
            dev["port"] = device.port;
            dev["registers"] = json::array();
            for (const auto& reg : device.register_names) {
                dev["registers"].push_back(reg);
            }
            response["devices"].push_back(dev);
        }
        return response.dump();
    }
    
    // 命令: read - 读取设备数据
    if (command == "read") {
        if (args.empty()) {
            response["success"] = false;
            response["error"] = "Missing device_id";
            return response.dump();
        }
        
        std::string device_id = args[0];
        std::string register_name = args.size() > 1 ? args[1] : "";
        
        // 查找设备
        const ModbusDevice* target_device = nullptr;
        for (const auto& device : devices_) {
            if (device.id == device_id) {
                target_device = &device;
                break;
            }
        }
        
        if (!target_device) {
            response["success"] = false;
            response["error"] = "Device not found: " + device_id;
            return response.dump();
        }
        
        // 实时读取设备数据
        auto registers = readModbusRegisters(*target_device);
        
        if (registers.empty()) {
            response["success"] = false;
            response["error"] = "Failed to read from device";
            return response.dump();
        }
        
        response["success"] = true;
        response["device_id"] = device_id;
        response["device_name"] = target_device->name;
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // 如果指定了寄存器名称，只返回该寄存器
        if (!register_name.empty()) {
            for (size_t i = 0; i < registers.size() && i < target_device->register_names.size(); ++i) {
                if (target_device->register_names[i] == register_name) {
                    response["register"] = register_name;
                    response["value"] = registers[i];
                    response["unit"] = getUnit(register_name);
                    return response.dump();
                }
            }
            response["success"] = false;
            response["error"] = "Register not found: " + register_name;
            return response.dump();
        }
        
        // 返回所有寄存器
        response["data"] = json::array();
        for (size_t i = 0; i < registers.size() && i < target_device->register_names.size(); ++i) {
            json point;
            point["name"] = target_device->register_names[i];
            point["value"] = registers[i];
            point["unit"] = getUnit(target_device->register_names[i]);
            response["data"].push_back(point);
        }
        
        return response.dump();
    }
    
    // 命令: write - 写入设备
    if (command == "write") {
        if (args.size() < 3) {
            response["success"] = false;
            response["error"] = "Usage: /write/<device_id>/<register_name>/<value>";
            return response.dump();
        }
        
        std::string device_id = args[0];
        std::string register_name = args[1];
        std::string value_str = args[2];
        
        // 查找设备
        const ModbusDevice* target_device = nullptr;
        int register_index = -1;
        for (const auto& device : devices_) {
            if (device.id == device_id) {
                target_device = &device;
                // 查找寄存器索引
                for (size_t i = 0; i < device.register_names.size(); ++i) {
                    if (device.register_names[i] == register_name) {
                        register_index = i;
                        break;
                    }
                }
                break;
            }
        }
        
        if (!target_device) {
            response["success"] = false;
            response["error"] = "Device not found: " + device_id;
            return response.dump();
        }
        
        if (register_index < 0) {
            response["success"] = false;
            response["error"] = "Register not found: " + register_name;
            return response.dump();
        }
        
        // 写入值
        uint16_t value = static_cast<uint16_t>(std::stoi(value_str));
        int register_addr = target_device->start_address + register_index;
        
        // 构建 Modbus 写单个寄存器请求（功能码 0x06）
        std::vector<uint8_t> request;
        uint16_t trans_id = transaction_id_++;
        request.push_back((trans_id >> 8) & 0xFF);
        request.push_back(trans_id & 0xFF);
        request.push_back(0x00);  // 协议标识
        request.push_back(0x00);
        request.push_back(0x00);  // 长度高字节
        request.push_back(0x06);  // 长度低字节
        request.push_back(static_cast<uint8_t>(target_device->slave_id));
        request.push_back(0x06);  // 功能码：写单个寄存器
        request.push_back((register_addr >> 8) & 0xFF);
        request.push_back(register_addr & 0xFF);
        request.push_back((value >> 8) & 0xFF);
        request.push_back(value & 0xFF);
        
        // 连接并发送
        int sockfd = connectToModbusDevice(target_device->host, target_device->port);
        if (sockfd < 0) {
            response["success"] = false;
            response["error"] = "Failed to connect to device";
            return response.dump();
        }
        
        ssize_t sent = ::send(sockfd, reinterpret_cast<const char*>(request.data()), request.size(), 0);
        if (sent != static_cast<ssize_t>(request.size())) {
            closeModbusConnection(sockfd);
            response["success"] = false;
            response["error"] = "Failed to send write request";
            return response.dump();
        }
        
        // 接收响应
        std::vector<uint8_t> resp(1024);
        ssize_t received = ::recv(sockfd, reinterpret_cast<char*>(resp.data()), resp.size(), 0);
        closeModbusConnection(sockfd);
        
        if (received <= 0) {
            response["success"] = false;
            response["error"] = "No response from device";
            return response.dump();
        }
        
        response["success"] = true;
        response["device_id"] = device_id;
        response["register"] = register_name;
        response["value"] = value;
        response["message"] = "Write successful";
        
        return response.dump();
    }
    
    // 命令: /status - 获取缓存的数据（向后兼容）
    if (command == "/status" || command == "status") {
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
        return response.dump();
    }
    
    // 未知命令
    response["success"] = false;
    response["error"] = "Unknown command: " + command;
    response["available_commands"] = json::array({
        "/devices - 获取设备列表",
        "/read/<device_id>/<register_name> - 读取指定寄存器",
        "/read/<device_id> - 读取设备所有数据",
        "/write/<device_id>/<register_name>/<value> - 写入寄存器",
        "/status - 获取缓存数据"
    });
    
    return response.dump();
}

bool IoTGatewayServer::parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) {
    command = path;
    args.clear();
    return true;
}

void IoTGatewayServer::onClientConnected(int clientFd) {
    Logger::info("客户端连接: " + std::to_string(clientFd));
}

void IoTGatewayServer::onClientDisconnected(int clientFd) {
    Logger::info("客户端断开: " + std::to_string(clientFd));
}

bool IoTGatewayServer::initMQTT() {
    Logger::info("初始化MQTT客户端...");
    
    mqtt_broker_ = config_->getString("mqtt.broker", "tcp://localhost:1883");
    mqtt_client_id_ = config_->getString("mqtt.client_id", "netbox_iot_gateway");
    mqtt_topic_prefix_ = config_->getString("mqtt.topic_prefix", "factory/production");
    mqtt_qos_ = config_->getInt("mqtt.qos", 1);
    
    Logger::info("MQTT Broker: " + mqtt_broker_);
    Logger::info("Client ID: " + mqtt_client_id_);
    
#ifdef ENABLE_IOT_GATEWAY
    try {
        mqtt_client_ = std::make_unique<mqtt::async_client>(mqtt_broker_, mqtt_client_id_);
        
        mqtt::connect_options conn_opts;
        conn_opts.set_keep_alive_interval(60);
        conn_opts.set_clean_session(true);
        conn_opts.set_automatic_reconnect(true);
        
        Logger::info("连接到MQTT Broker...");
        mqtt_client_->connect(conn_opts)->wait();
        
        Logger::info("MQTT客户端连接成功");
        return true;
        
    } catch (const mqtt::exception& e) {
        Logger::error("MQTT连接失败: " + std::string(e.what()));
        return false;
    }
#else
    Logger::warn("MQTT未启用，跳过MQTT初始化");
    return true;
#endif
}

bool IoTGatewayServer::initModbusDevices() {
    Logger::info("初始化Modbus设备...");
    
    // 调试：打印所有配置键
    auto allKeys = config_->getAllKeys();
    Logger::debug("配置文件中的所有键 (" + std::to_string(allKeys.size()) + " 个):");
    for (const auto& k : allKeys) {
        if (k.find("devices") != std::string::npos) {
            Logger::debug("  " + k + " = " + config_->getString(k, ""));
        }
    }
    
    try {
        // 使用 getKeysWithPrefix 获取设备配置
        // 配置格式: devices.0.id, devices.0.name, devices.0.host, etc.
        
        // 先检查有多少个设备
        // 注意：YAML配置解析器会将所有键放在 mqtt. 前缀下
        int deviceIndex = 0;
        while (true) {
            std::string prefix = "mqtt.devices." + std::to_string(deviceIndex) + ".";
            std::string deviceId = config_->getString(prefix + "id", "");
            
            Logger::debug("尝试读取设备 " + std::to_string(deviceIndex) + ", 键: " + prefix + "id");
            Logger::debug("读取到的device_id: '" + deviceId + "'");
            
            if (deviceId.empty()) {
                break;  // 没有更多设备
            }
            
            ModbusDevice device;
            device.id = deviceId;
            device.name = config_->getString(prefix + "name", "Device" + std::to_string(deviceIndex));
            device.host = config_->getString(prefix + "host", "127.0.0.1");
            device.port = config_->getInt(prefix + "port", 502);
            device.slave_id = config_->getInt(prefix + "slave_id", 1);
            device.start_address = config_->getInt(prefix + "start_address", 0);
            device.register_count = config_->getInt(prefix + "register_count", 4);
            device.poll_interval = config_->getInt(prefix + "poll_interval", 2);
            
            // 读取寄存器名称
            int regIndex = 0;
            while (true) {
                std::string regName = config_->getString(prefix + "register_names." + std::to_string(regIndex), "");
                if (regName.empty()) {
                    break;
                }
                device.register_names.push_back(regName);
                regIndex++;
            }
            
            // 如果没有配置寄存器名称，使用默认名称
            if (device.register_names.empty()) {
                for (int i = 0; i < device.register_count; i++) {
                    device.register_names.push_back("register_" + std::to_string(i));
                }
            }
            
            devices_.push_back(device);
            Logger::info("添加设备: " + device.name + " (" + device.host + ":" + std::to_string(device.port) + ")");
            
            deviceIndex++;
        }
        
        Logger::info("加载了 " + std::to_string(devices_.size()) + " 个设备");
        return !devices_.empty();
        
    } catch (const std::exception& e) {
        Logger::error("读取设备配置失败: " + std::string(e.what()));
        return false;
    }
}

void IoTGatewayServer::dataCollectionThread() {
    Logger::info("数据采集线程启动");
    
    while (running_) {
        for (const auto& device : devices_) {
            try {
                auto registers = readModbusRegisters(device);
                
                if (registers.empty()) {
                    continue;
                }
                
                std::vector<DataPoint> data_points;
                int64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                for (size_t i = 0; i < registers.size() && i < device.register_names.size(); ++i) {
                    DataPoint point;
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
                
                publishToMQTT(device.id, data_points);
                
                Logger::debug("设备 " + device.name + " 采集成功");
                
            } catch (const std::exception& e) {
                Logger::error("设备 " + device.name + " 采集异常: " + std::string(e.what()));
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    Logger::info("数据采集线程退出");
}

std::vector<uint16_t> IoTGatewayServer::readModbusRegisters(const ModbusDevice& device) {
    int sockfd = connectToModbusDevice(device.host, device.port);
    if (sockfd < 0) {
        return {};
    }
    
    auto request = buildModbusRequest(device.slave_id, device.start_address, device.register_count);
    
    ssize_t sent = ::send(sockfd, reinterpret_cast<const char*>(request.data()), request.size(), 0);
    if (sent != static_cast<ssize_t>(request.size())) {
        closeModbusConnection(sockfd);
        return {};
    }
    
    std::vector<uint8_t> response(1024);
    ssize_t received = ::recv(sockfd, reinterpret_cast<char*>(response.data()), response.size(), 0);
    if (received <= 0) {
        closeModbusConnection(sockfd);
        return {};
    }
    
    response.resize(received);
    closeModbusConnection(sockfd);
    
    return parseModbusResponse(response);
}

std::vector<uint8_t> IoTGatewayServer::buildModbusRequest(int slave_id, int start_addr, int count) {
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

std::vector<uint16_t> IoTGatewayServer::parseModbusResponse(const std::vector<uint8_t>& response) {
    std::vector<uint16_t> registers;
    
    if (response.size() < 9) {
        return registers;
    }
    
    uint8_t function_code = response[7];
    if (function_code != 0x03) {
        return registers;
    }
    
    uint8_t byte_count = response[8];
    
    for (int i = 0; i < byte_count / 2; ++i) {
        uint16_t value = (response[9 + i * 2] << 8) | response[10 + i * 2];
        registers.push_back(value);
    }
    
    return registers;
}

double IoTGatewayServer::convertRegisterValue(uint16_t rawValue, const std::string& name) {
    if (name.find("temperature") != std::string::npos) return rawValue / 10.0;
    if (name.find("humidity") != std::string::npos) return rawValue / 10.0;
    if (name.find("pressure") != std::string::npos) return rawValue / 10.0;
    return static_cast<double>(rawValue);
}

std::string IoTGatewayServer::getUnit(const std::string& name) {
    if (name.find("温度") != std::string::npos || name.find("temperature") != std::string::npos) return "℃";
    if (name.find("湿度") != std::string::npos || name.find("humidity") != std::string::npos) return "%";
    if (name.find("压力") != std::string::npos || name.find("pressure") != std::string::npos) return "MPa";
    if (name.find("流量") != std::string::npos || name.find("flow") != std::string::npos) return "L/min";
    if (name.find("转速") != std::string::npos || name.find("speed") != std::string::npos) return "RPM";
    if (name.find("电压") != std::string::npos || name.find("voltage") != std::string::npos) return "V";
    if (name.find("电流") != std::string::npos || name.find("current") != std::string::npos) return "A";
    if (name.find("功率") != std::string::npos || name.find("power") != std::string::npos) return "kW";
    if (name.find("合格率") != std::string::npos || name.find("pass_rate") != std::string::npos) return "%";
    if (name.find("缺陷数") != std::string::npos || name.find("defects") != std::string::npos) return "个";
    return "";
}

void IoTGatewayServer::publishToMQTT(const std::string& deviceId, const std::vector<DataPoint>& data) {
#ifdef ENABLE_IOT_GATEWAY
    if (!mqtt_client_ || !mqtt_client_->is_connected()) {
        return;
    }
    
    try {
        json j_array = json::array();
        
        for (const auto& point : data) {
            json j_point;
            j_point["name"] = point.name;
            j_point["value"] = point.value;
            j_point["unit"] = point.unit;
            j_point["timestamp"] = point.timestamp;
            j_point["quality"] = point.quality;
            j_array.push_back(j_point);
        }
        
        std::string payload = j_array.dump();
        std::string topic = mqtt_topic_prefix_ + "/" + deviceId + "/data";
        
        mqtt_client_->publish(topic, payload, mqtt_qos_, false);
        
        Logger::debug("发布MQTT: " + topic);
        
    } catch (const std::exception& e) {
        Logger::error("MQTT发布失败: " + std::string(e.what()));
    }
#else
    (void)deviceId;
    (void)data;
#endif
}

int IoTGatewayServer::connectToModbusDevice(const std::string& host, int port) {
    Logger::debug("尝试连接Modbus设备: " + host + ":" + std::to_string(port));
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        Logger::error("创建socket失败");
        return -1;
    }
    
#ifdef _WIN32
    DWORD timeout = 3000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // 支持主机名解析和IP地址
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        // 不是有效的IP地址，尝试DNS解析
        struct addrinfo hints, *result;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
        if (ret != 0 || !result) {
            Logger::error("无法解析主机名: " + host);
            CLOSE_SOCKET(sockfd);
            return -1;
        }
        
        addr.sin_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;
        freeaddrinfo(result);
        Logger::debug("成功解析主机名: " + host);
    }
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::error("连接失败: " + host + ":" + std::to_string(port));
        CLOSE_SOCKET(sockfd);
        return -1;
    }
    
    Logger::debug("成功连接到: " + host + ":" + std::to_string(port));
    return sockfd;
}

void IoTGatewayServer::closeModbusConnection(int sockfd) {
    if (sockfd >= 0) {
        CLOSE_SOCKET(sockfd);
    }
}
