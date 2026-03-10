# MQTT 集成指南

## 📦 使用的第三方库

项目使用 **Eclipse Paho MQTT C++** 库，这是业界标准的 MQTT 客户端实现。

### 库信息
- **名称**: Eclipse Paho MQTT C++
- **版本**: 1.2+
- **许可证**: EPL 2.0 / EDL 1.0
- **官网**: https://www.eclipse.org/paho/
- **GitHub**: https://github.com/eclipse/paho.mqtt.cpp

### 特性
- ✅ 完整的 MQTT 3.1.1 和 5.0 支持
- ✅ 同步和异步 API
- ✅ 自动重连
- ✅ QoS 0, 1, 2 支持
- ✅ SSL/TLS 加密
- ✅ 遗嘱消息
- ✅ 持久化会话

## 🔧 安装方法

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y libpaho-mqtt-dev libpaho-mqttpp-dev
```

### CentOS/RHEL

```bash
sudo yum install -y paho-c-devel paho-cpp-devel
```

### macOS

```bash
brew install paho-mqtt-cpp
```

### Windows

使用 vcpkg:
```powershell
vcpkg install paho-mqtt paho-mqttpp3
```

### 从源码编译

```bash
# 1. 安装 Paho C 库
git clone https://github.com/eclipse/paho.mqtt.c.git
cd paho.mqtt.c
mkdir build && cd build
cmake .. -DPAHO_WITH_SSL=ON
make -j$(nproc)
sudo make install

# 2. 安装 Paho C++ 库
git clone https://github.com/eclipse/paho.mqtt.cpp.git
cd paho.mqtt.cpp
mkdir build && cd build
cmake .. -DPAHO_WITH_SSL=ON
make -j$(nproc)
sudo make install
```

## 📝 代码集成

### 1. CMakeLists.txt 配置

已在 `CMakeLists.txt` 中添加：

```cmake
# 查找MQTT库（可选）
find_package(PahoMqttCpp QUIET)
if(PahoMqttCpp_FOUND)
    message(STATUS "Found Paho MQTT C++, IoT Gateway will be enabled")
    set(MQTT_ENABLED ON)
    add_definitions(-DMQTT_ENABLED)
else()
    message(WARNING "Paho MQTT C++ not found, IoT Gateway will be disabled")
    set(MQTT_ENABLED OFF)
endif()

# 链接MQTT库
if(MQTT_ENABLED)
    target_link_libraries(NetBox PRIVATE
        paho-mqttpp3
        paho-mqtt3as
    )
endif()
```

### 2. 代码中使用

在 `app/src/IoTGatewayServer.cpp` 中：

```cpp
#ifdef MQTT_ENABLED
#include <mqtt/async_client.h>

bool IoTGatewayServer::initMqttClient() {
    try {
        mqttClient_ = std::make_shared<mqtt::async_client>(
            mqttBroker_, 
            mqttClientId_
        );
        return true;
    } catch (const mqtt::exception& e) {
        Logger::error("MQTT init failed: " + std::string(e.what()));
        return false;
    }
}

bool IoTGatewayServer::connectMqtt() {
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);
    connOpts.set_automatic_reconnect(true);
    
    mqttClient_->connect(connOpts)->wait();
    return true;
}

bool IoTGatewayServer::publishToMqtt(
    const std::string& topic, 
    const std::string& payload) {
    
    auto msg = mqtt::make_message(topic, payload);
    msg->set_qos(1);
    mqttClient_->publish(msg)->wait();
    return true;
}
#endif
```

## 🐳 Docker 集成

### Dockerfile 更新

已在 Dockerfile 中添加 MQTT 库：

```dockerfile
# 构建阶段
RUN apt-get install -y \
    libpaho-mqtt-dev \
    libpaho-mqttpp-dev

# 运行阶段
RUN apt-get install -y \
    libpaho-mqtt1.3 \
    libpaho-mqttpp3
```

### 重新构建镜像

```bash
docker-compose build netbox-iot-gateway
```

## 📊 API 使用示例

### 基本连接

```cpp
#include <mqtt/async_client.h>

// 创建客户端
mqtt::async_client client("tcp://localhost:1883", "client_id");

// 连接选项
mqtt::connect_options connOpts;
connOpts.set_keep_alive_interval(20);
connOpts.set_clean_session(true);

// 连接
client.connect(connOpts)->wait();
```

### 发布消息

```cpp
// 创建消息
auto msg = mqtt::make_message("topic/test", "Hello MQTT");
msg->set_qos(1);
msg->set_retained(false);

// 发布
client.publish(msg)->wait();
```

### 订阅主题

```cpp
// 订阅
client.subscribe("topic/test", 1)->wait();

// 设置回调
client.set_message_callback([](mqtt::const_message_ptr msg) {
    std::cout << "Topic: " << msg->get_topic() << std::endl;
    std::cout << "Payload: " << msg->to_string() << std::endl;
});
```

### 自动重连

```cpp
mqtt::connect_options connOpts;
connOpts.set_automatic_reconnect(true);
connOpts.set_min_retry_interval(1);  // 1秒
connOpts.set_max_retry_interval(60); // 60秒

// 设置连接丢失回调
client.set_connection_lost_handler([](const std::string& cause) {
    std::cout << "Connection lost: " << cause << std::endl;
});

// 设置重连成功回调
client.set_connected_handler([](const std::string& cause) {
    std::cout << "Reconnected!" << std::endl;
});
```

### SSL/TLS 加密

```cpp
mqtt::ssl_options sslopts;
sslopts.set_trust_store("/path/to/ca.crt");
sslopts.set_key_store("/path/to/client.crt");
sslopts.set_private_key("/path/to/client.key");

mqtt::connect_options connOpts;
connOpts.set_ssl(sslopts);

client.connect(connOpts)->wait();
```

## 🔍 调试和测试

### 检查库是否安装

```bash
# 检查头文件
ls /usr/include/mqtt/

# 检查库文件
ls /usr/lib/x86_64-linux-gnu/libpaho-mqtt*

# 使用 pkg-config
pkg-config --modversion paho-mqttpp3
```

### 编译测试程序

```bash
g++ test.cpp -o test \
    -lpaho-mqttpp3 \
    -lpaho-mqtt3as \
    -std=c++17
```

### 运行时检查

```bash
# 查看链接的库
ldd ./NetBox | grep mqtt

# 应该看到：
# libpaho-mqttpp3.so.1 => /usr/lib/x86_64-linux-gnu/libpaho-mqttpp3.so.1
# libpaho-mqtt3as.so.1 => /usr/lib/x86_64-linux-gnu/libpaho-mqtt3as.so.1
```

## 📚 参考资料

- [Paho MQTT C++ 文档](https://www.eclipse.org/paho/files/mqttdoc/MQTTClient/html/index.html)
- [MQTT 协议规范](https://mqtt.org/mqtt-specification/)
- [示例代码](https://github.com/eclipse/paho.mqtt.cpp/tree/master/src/samples)

## ⚠️ 常见问题

### Q: 编译时找不到 mqtt 头文件

**A**: 确保安装了开发包：
```bash
sudo apt-get install libpaho-mqttpp-dev
```

### Q: 运行时找不到 .so 文件

**A**: 更新动态链接库缓存：
```bash
sudo ldconfig
```

### Q: Docker 中 MQTT 连接失败

**A**: 检查网络配置：
```yaml
# docker-compose.yml
networks:
  netbox-network:
    driver: bridge
```

确保所有服务在同一网络中。

### Q: 如何禁用 MQTT 功能

**A**: 不安装 MQTT 库，代码会自动通过条件编译禁用 MQTT 功能。

## 🎯 性能优化

### 1. 连接池

对于高并发场景，可以使用连接池：

```cpp
class MQTTConnectionPool {
    std::vector<std::shared_ptr<mqtt::async_client>> clients_;
    std::mutex mutex_;
    
public:
    std::shared_ptr<mqtt::async_client> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 返回可用连接
    }
};
```

### 2. 批量发布

```cpp
// 批量发送消息
std::vector<mqtt::delivery_token_ptr> tokens;
for (const auto& msg : messages) {
    tokens.push_back(client.publish(msg));
}

// 等待所有消息发送完成
for (auto& token : tokens) {
    token->wait();
}
```

### 3. QoS 选择

- **QoS 0**: 最快，但可能丢失消息
- **QoS 1**: 至少一次，推荐用于数据采集
- **QoS 2**: 恰好一次，最慢但最可靠

## 📈 监控和日志

### 启用详细日志

```cpp
// 在代码中
Logger::setLevel(LogLevel::DEBUG);

// 或在配置文件中
logging:
  level: "debug"
```

### MQTT 统计信息

```cpp
// 获取连接状态
bool connected = client.is_connected();

// 获取待发送消息数
int pending = client.get_pending_delivery_tokens().size();
```
