#include "MQTTClient.h"
#include "base/Logger.h"
#include <mosquitto.h>
#include <cstring>

MQTTClient::MQTTClient(const std::string& clientId, bool cleanSession)
    : clientId_(clientId), connected_(false), loopRunning_(false) {
    
    // 初始化mosquitto库（全局只需调用一次）
    static bool initialized = false;
    if (!initialized) {
        mosquitto_lib_init();
        initialized = true;
    }
    
    // 创建mosquitto实例
    mosq_ = mosquitto_new(clientId.c_str(), cleanSession, this);
    if (!mosq_) {
        Logger::error("[MQTTClient] 创建mosquitto实例失败");
        return;
    }
    
    // 设置回调函数
    mosquitto_connect_callback_set(mosq_, onConnect);
    mosquitto_disconnect_callback_set(mosq_, onDisconnect);
    mosquitto_message_callback_set(mosq_, onMessage);
    mosquitto_publish_callback_set(mosq_, onPublish);
    mosquitto_subscribe_callback_set(mosq_, onSubscribe);
    mosquitto_log_callback_set(mosq_, onLog);
    
    Logger::info("[MQTTClient] MQTT客户端创建成功: " + clientId);
}

MQTTClient::~MQTTClient() {
    stopLoop();
    disconnect();
    
    if (mosq_) {
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    
    // 清理mosquitto库
    mosquitto_lib_cleanup();
}

bool MQTTClient::connect(const std::string& host, int port,
                        const std::string& username,
                        const std::string& password,
                        int keepalive) {
    if (!mosq_) {
        Logger::error("[MQTTClient] mosquitto实例未初始化");
        return false;
    }
    
    // 设置用户名和密码
    if (!username.empty()) {
        int rc = mosquitto_username_pw_set(mosq_, username.c_str(), 
                                          password.empty() ? nullptr : password.c_str());
        if (rc != MOSQ_ERR_SUCCESS) {
            Logger::error("[MQTTClient] 设置用户名密码失败: " + std::string(mosquitto_strerror(rc)));
            return false;
        }
    }
    
    // 连接到Broker
    int rc = mosquitto_connect(mosq_, host.c_str(), port, keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        Logger::error("[MQTTClient] 连接失败: " + std::string(mosquitto_strerror(rc)));
        return false;
    }
    
    Logger::info("[MQTTClient] 正在连接到 " + host + ":" + std::to_string(port));
    return true;
}

void MQTTClient::disconnect() {
    if (mosq_ && connected_) {
        mosquitto_disconnect(mosq_);
        connected_ = false;
        Logger::info("[MQTTClient] 已断开连接");
    }
}

bool MQTTClient::publish(const std::string& topic, const std::string& payload,
                        QoS qos, bool retain) {
    if (!mosq_ || !connected_) {
        Logger::warn("[MQTTClient] 未连接，无法发布消息");
        return false;
    }
    
    int mid;
    int rc = mosquitto_publish(mosq_, &mid, topic.c_str(),
                              payload.length(), payload.c_str(),
                              qos, retain);
    
    if (rc != MOSQ_ERR_SUCCESS) {
        Logger::error("[MQTTClient] 发布消息失败: " + std::string(mosquitto_strerror(rc)));
        return false;
    }
    
    Logger::debug("[MQTTClient] 发布消息到 " + topic + ", mid=" + std::to_string(mid));
    return true;
}

bool MQTTClient::subscribe(const std::string& topic, QoS qos) {
    if (!mosq_ || !connected_) {
        Logger::warn("[MQTTClient] 未连接，无法订阅");
        return false;
    }
    
    int mid;
    int rc = mosquitto_subscribe(mosq_, &mid, topic.c_str(), qos);
    
    if (rc != MOSQ_ERR_SUCCESS) {
        Logger::error("[MQTTClient] 订阅失败: " + std::string(mosquitto_strerror(rc)));
        return false;
    }
    
    Logger::info("[MQTTClient] 订阅主题: " + topic);
    return true;
}

bool MQTTClient::unsubscribe(const std::string& topic) {
    if (!mosq_ || !connected_) {
        return false;
    }
    
    int mid;
    int rc = mosquitto_unsubscribe(mosq_, &mid, topic.c_str());
    
    if (rc != MOSQ_ERR_SUCCESS) {
        Logger::error("[MQTTClient] 取消订阅失败: " + std::string(mosquitto_strerror(rc)));
        return false;
    }
    
    Logger::info("[MQTTClient] 取消订阅: " + topic);
    return true;
}

void MQTTClient::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    messageCallback_ = callback;
}

void MQTTClient::setConnectCallback(ConnectCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    connectCallback_ = callback;
}

bool MQTTClient::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

void MQTTClient::loop() {
    if (!mosq_) return;
    
    loopRunning_ = true;
    Logger::info("[MQTTClient] 启动消息循环");
    
    int rc = mosquitto_loop_forever(mosq_, -1, 1);
    if (rc != MOSQ_ERR_SUCCESS) {
        Logger::error("[MQTTClient] 消息循环错误: " + std::string(mosquitto_strerror(rc)));
    }
    
    loopRunning_ = false;
}

void MQTTClient::loopOnce(int timeout_ms) {
    if (!mosq_) return;
    mosquitto_loop(mosq_, timeout_ms, 1);
}

void MQTTClient::stopLoop() {
    if (loopRunning_) {
        loopRunning_ = false;
        if (mosq_) {
            mosquitto_loop_stop(mosq_, false);
        }
        Logger::info("[MQTTClient] 停止消息循环");
    }
}

// ==================== 回调函数实现 ====================

void MQTTClient::onConnect(struct mosquitto* mosq, void* obj, int rc) {
    MQTTClient* client = static_cast<MQTTClient*>(obj);
    if (!client) return;
    
    std::lock_guard<std::mutex> lock(client->mutex_);
    
    if (rc == 0) {
        client->connected_ = true;
        Logger::info("[MQTTClient] 连接成功");
        
        if (client->connectCallback_) {
            client->connectCallback_(true);
        }
    } else {
        client->connected_ = false;
        Logger::error("[MQTTClient] 连接失败，错误码: " + std::to_string(rc));
        
        if (client->connectCallback_) {
            client->connectCallback_(false);
        }
    }
}

void MQTTClient::onDisconnect(struct mosquitto* mosq, void* obj, int rc) {
    MQTTClient* client = static_cast<MQTTClient*>(obj);
    if (!client) return;
    
    std::lock_guard<std::mutex> lock(client->mutex_);
    client->connected_ = false;
    
    if (rc == 0) {
        Logger::info("[MQTTClient] 主动断开连接");
    } else {
        Logger::warn("[MQTTClient] 意外断开连接，错误码: " + std::to_string(rc));
    }
    
    if (client->connectCallback_) {
        client->connectCallback_(false);
    }
}

void MQTTClient::onMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* msg) {
    MQTTClient* client = static_cast<MQTTClient*>(obj);
    if (!client || !msg) return;
    
    std::string topic(msg->topic);
    std::string payload(static_cast<const char*>(msg->payload), msg->payloadlen);
    
    Logger::debug("[MQTTClient] 收到消息: " + topic + " = " + payload);
    
    std::lock_guard<std::mutex> lock(client->mutex_);
    if (client->messageCallback_) {
        client->messageCallback_(topic, payload);
    }
}

void MQTTClient::onPublish(struct mosquitto* mosq, void* obj, int mid) {
    Logger::debug("[MQTTClient] 消息发布成功, mid=" + std::to_string(mid));
}

void MQTTClient::onSubscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    Logger::info("[MQTTClient] 订阅成功, mid=" + std::to_string(mid));
}

void MQTTClient::onLog(struct mosquitto* mosq, void* obj, int level, const char* str) {
    // 根据日志级别输出
    switch (level) {
        case MOSQ_LOG_DEBUG:
            Logger::debug("[mosquitto] " + std::string(str));
            break;
        case MOSQ_LOG_INFO:
        case MOSQ_LOG_NOTICE:
            Logger::info("[mosquitto] " + std::string(str));
            break;
        case MOSQ_LOG_WARNING:
            Logger::warn("[mosquitto] " + std::string(str));
            break;
        case MOSQ_LOG_ERR:
            Logger::error("[mosquitto] " + std::string(str));
            break;
        default:
            break;
    }
}

