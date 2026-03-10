# IoT网关使用指南

## 概述

NetBox IoT网关是一个工业设备数据采集网关，支持从Modbus设备采集数据并通过MQTT发布到云端。

## 功能特性

- ✅ Modbus TCP协议支持
- ✅ MQTT消息发布
- ✅ 多设备并发采集
- ✅ 自动重连机制
- ✅ 数据缓存
- ✅ 配置驱动

## 架构设计

```
┌─────────────┐      ┌──────────────┐      ┌─────────────┐
│ Modbus设备  │─────▶│  IoT网关     │─────▶│ MQTT Broker │
│ (温度/压力) │      │  (NetBox)    │      │  (云端)     │
└─────────────┘      └──────────────┘      └─────────────┘
```

## 快速开始

### 1. 安装依赖

```bash
# 安装MQTT C++库 (Paho MQTT)
sudo apt-get install libpaho-mqtt-dev libpaho-mqttpp-dev

# 安装MQTT Broker (Mosquitto)
sudo apt-get install mosquitto mosquitto-clients

# 启动Mosquitto
sudo systemctl start mosquitto
sudo systemctl enable mosquitto
```

### 2. 配置网关

编辑 `config/config-iot-gateway.yaml`:

```yaml
mqtt:
  broker: "tcp://localhost:1883"
  client_id: "netbox_iot_gateway"
  topic_prefix: "iot/devices"

devices:
  device001:
    name: "温度传感器1"
    host: "192.168.1.100"
    port: 502
    slave_id: 1
    start_address: 0
    register_count: 4
    poll_interval: 5
```

### 3. 编译项目

```bash
# 更新CMakeLists.txt添加IoT网关
cd build
cmake ..
make
```

### 4. 运行网关

```bash
./NetBox config/config-iot-gateway.yaml
```

### 5. 测试

```bash
# 终端1: 运行Modbus模拟器
python3 examples/iot_gateway_demo.py

# 终端2: 订阅MQTT主题
mosquitto_sub -t "iot/devices/#" -v

# 终端3: 启动网关
./NetBox config/config-iot-gateway.yaml
```

## 数据格式

### MQTT消息格式

```json
[
  {
    "name": "temperature",
    "value": 25.3,
    "unit": "°C",
    "timestamp": 1704182400000,
    "quality": "good"
  },
  {
    "name": "humidity",
    "value": 65.2,
    "unit": "%",
    "timestamp": 1704182400000,
    "quality": "good"
  }
]
```

### MQTT主题结构

```
iot/devices/{device_id}/data
```

示例：
- `iot/devices/device001/data` - 设备001的数据
- `iot/devices/device002/data` - 设备002的数据

## 配置说明

### MQTT配置

| 参数 | 说明 | 默认值 |
|------|------|--------|
| broker | MQTT Broker地址 | tcp://localhost:1883 |
| client_id | 客户端ID | netbox_iot_gateway |
| username | 用户名 | "" |
| password | 密码 | "" |
| topic_prefix | 主题前缀 | iot/devices |
| qos | 消息质量 | 1 |

### 设备配置

| 参数 | 说明 | 示例 |
|------|------|------|
| name | 设备名称 | 温度传感器1 |
| host | Modbus设备IP | 192.168.1.100 |
| port | Modbus端口 | 502 |
| slave_id | 从站ID | 1 |
| start_address | 起始地址 | 0 |
| register_count | 寄存器数量 | 4 |
| poll_interval | 轮询间隔(秒) | 5 |

## 应用场景

### 1. 工厂设备监控

```yaml
devices:
  machine001:
    name: "生产线1-温度"
    host: "192.168.10.100"
    poll_interval: 2
  
  machine002:
    name: "生产线1-压力"
    host: "192.168.10.101"
    poll_interval: 2
```

### 2. 环境监测

```yaml
devices:
  env001:
    name: "车间环境监测"
    registers:
      - name: "temperature"
      - name: "humidity"
      - name: "co2"
      - name: "pm25"
```

### 3. 能源管理

```yaml
devices:
  power001:
    name: "电力监测"
    registers:
      - name: "voltage"
      - name: "current"
      - name: "power"
      - name: "energy"
```

## 故障排查

### 1. 无法连接Modbus设备

```bash
# 检查网络连通性
ping 192.168.1.100

# 检查端口
telnet 192.168.1.100 502
```

### 2. MQTT连接失败

```bash
# 检查Mosquitto状态
sudo systemctl status mosquitto

# 测试MQTT连接
mosquitto_pub -t "test" -m "hello"
```

### 3. 查看日志

```bash
tail -f logs/iot_gateway.log
```

## 性能优化

1. **调整轮询间隔**：根据实际需求设置合理的采集频率
2. **批量发布**：多个数据点合并为一条MQTT消息
3. **本地缓存**：网络故障时缓存数据，恢复后补发
4. **QoS选择**：根据重要性选择合适的QoS等级

## 扩展开发

### 添加新的协议

1. 继承 `ProtocolBase` 实现协议解析
2. 在 `IoTGatewayServer` 中集成新协议
3. 更新配置文件支持新协议

### 添加数据处理

```cpp
// 在 convertRegisterValue 中添加自定义转换
double IoTGatewayServer::convertRegisterValue(uint16_t rawValue) {
    // 自定义数据转换逻辑
    return static_cast<double>(rawValue) / 10.0;
}
```

## 参考资料

- [Modbus协议规范](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)
- [MQTT协议规范](https://mqtt.org/mqtt-specification/)
- [Paho MQTT C++文档](https://www.eclipse.org/paho/files/mqttdoc/MQTTClient/html/index.html)
