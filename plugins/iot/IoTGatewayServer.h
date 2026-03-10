#pragma once

#include "ApplicationServer.h"
#include "util/EnhancedConfigReader.h"

#ifdef ENABLE_IOT_GATEWAY
#include <mqtt/async_client.h>
#endif
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

struct ModbusDevice {
    std::string id;
    std::string name;
    std::string host;
    int port;
    int slave_id;
    int start_address;
    int register_count;
    int poll_interval;
    std::vector<std::string> register_names;
};

struct DataPoint {
    std::string name;
    double value;
    std::string unit;
    int64_t timestamp;
    std::string quality;
};

class IoTGatewayServer : public ApplicationServer {
public:
    IoTGatewayServer(const std::string& ip, int port, 
                     IOMultiplexer::IOType io_type,
                     EnhancedConfigReader* config);
    
    virtual ~IoTGatewayServer();
    
    bool start() override;
    void stop() override;

protected:
    // 实现 ApplicationServer 的纯虚函数
    void initializeProtocolRouter() override;
    std::string handleHttpRequest(const std::string& request, int clientFd) override;
    std::string handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) override;
    bool parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) override;
    
    //覆盖数据接收处理，直接处理HTTP请求
    void onDataReceived(int clientFd, const char* data, size_t len) override;
    
    void onClientConnected(int clientFd) override;
    void onClientDisconnected(int clientFd) override;

private:
    bool initMQTT();
    bool initModbusDevices();
    void dataCollectionThread();
    std::vector<uint16_t> readModbusRegisters(const ModbusDevice& device);
    double convertRegisterValue(uint16_t rawValue, const std::string& name);
    std::string getUnit(const std::string& name);
    void publishToMQTT(const std::string& deviceId, const std::vector<DataPoint>& data);
    std::vector<uint8_t> buildModbusRequest(int slave_id, int start_addr, int count);
    std::vector<uint16_t> parseModbusResponse(const std::vector<uint8_t>& response);
    int connectToModbusDevice(const std::string& host, int port);
    void closeModbusConnection(int sockfd);

private:
    EnhancedConfigReader* config_;
    std::string mqtt_broker_;
    std::string mqtt_client_id_;
    std::string mqtt_topic_prefix_;
    int mqtt_qos_;
#ifdef ENABLE_IOT_GATEWAY
    std::unique_ptr<mqtt::async_client> mqtt_client_;
#else
    void* mqtt_client_ = nullptr;  // Placeholder when MQTT disabled
#endif
    std::vector<ModbusDevice> devices_;
    std::thread collection_thread_;
    std::atomic<bool> running_;
    std::map<std::string, std::vector<DataPoint>> data_cache_;
    std::mutex cache_mutex_;
    std::atomic<uint16_t> transaction_id_;
};
