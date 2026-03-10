# IoT 网关架构说明

## 🎯 核心设计理念

### Modbus 单播（一对一）+ MQTT 广播（一对多）

```
┌──────────────────────────────────────────────────────────────┐
│                      IoT 网关架构                             │
└──────────────────────────────────────────────────────────────┘

输入侧（单播）                网关                输出侧（广播）
─────────────────────────────────────────────────────────────────

设备1 (192.168.1.100)                           订阅者1 (监控系统)
  │                                                  ▲
  │ Modbus TCP                                       │
  │ 点对点连接                                       │
  └──────────▶ ┌─────────────┐                     │
               │             │                     │
设备2 (192.168.1.101)        │   数据采集      │                     │
  │            │   线程池    │                     │
  │ Modbus TCP │             │                     │
  │ 点对点连接  │   ┌─────┐   │    MQTT Publish    │
  └──────────▶ │   │设备1│   │ ──────广播────────┤
               │   │设备2│   │                     │
设备3 (192.168.1.102)        │   │设备3│   │                     │
  │            │   └─────┘   │                     │
  │ Modbus TCP │             │                     ▼
  │ 点对点连接  │   数据转换  │                订阅者2 (数据库)
  └──────────▶ │   协议转换  │                     ▲
               │   数据缓存  │                     │
               │             │    MQTT Publish    │
               └─────────────┘ ──────广播────────┤
                                                   │
                                                   ▼
                                              订阅者3 (分析系统)
```

## 📊 通信模式对比

### Modbus 单播（输入侧）

| 特性 | 说明 |
|------|------|
| **通信模式** | 点对点（一对一） |
| **连接方式** | TCP 短连接 |
| **数据流向** | 设备 → 网关 |
| **并发处理** | 多线程轮询 |
| **协议** | Modbus TCP |
| **端口** | 502（标准） |

**代码实现**：
```cpp
// 每个设备独立的采集线程
void pollingThreadFunc(const DeviceConfig& device) {
    while (running_) {
        // 1. 建立连接
        int sockfd = connectModbusDevice(device.host, device.port);
        
        // 2. 发送请求
        send(sockfd, request.data(), request.size(), 0);
        
        // 3. 接收响应
        recv(sockfd, response.data(), response.size(), 0);
        
        // 4. 关闭连接
        close(sockfd);
        
        // 5. 等待下次轮询
        sleep(device.pollInterval);
    }
}
```

### MQTT 广播（输出侧）

| 特性 | 说明 |
|------|------|
| **通信模式** | 发布/订阅（一对多） |
| **连接方式** | TCP 长连接 |
| **数据流向** | 网关 → 多个订阅者 |
| **并发处理** | 异步发布 |
| **协议** | MQTT 3.1.1/5.0 |
| **端口** | 1883（标准）/ 8883（SSL） |

**代码实现**：
```cpp
// 一次发布，多个订阅者接收
bool publishToMqtt(const std::string& topic, const std::string& payload) {
    auto msg = mqtt::make_message(topic, payload);
    msg->set_qos(1);
    mqttClient_->publish(msg)->wait();
    // 所有订阅该主题的客户端都会收到
}
```

## 🔄 数据流转过程

### 1. 数据采集（Modbus 单播）

```cpp
// 线程1：采集设备1
Device1 (192.168.1.100:502)
  ↓ Modbus Request
  ↓ Read Holding Registers (0-3)
  ↓ Modbus Response
  ↓ [250, 650, 1013, 150]
  ↓
缓存到内存

// 线程2：采集设备2
Device2 (192.168.1.101:502)
  ↓ Modbus Request
  ↓ Read Holding Registers (0-1)
  ↓ Modbus Response
  ↓ [1013, 250]
  ↓
缓存到内存

// 线程3：采集设备3
Device3 (192.168.1.102:502)
  ↓ Modbus Request
  ↓ Read Holding Registers (0-2)
  ↓ Modbus Response
  ↓ [150, 1000, 1]
  ↓
缓存到内存
```

### 2. 数据转换

```cpp
// 原始寄存器值 → 实际物理量
250  → 25.0°C  (scale: 0.1)
650  → 65.0%   (scale: 0.1)
1013 → 101.3kPa (scale: 0.1)
150  → 15.0L/min (scale: 0.1)
```

### 3. 数据发布（MQTT 广播）

```cpp
// 发布到不同主题
Topic: factory/workshop1/device001/data
Payload: [
  {"name":"temperature","value":25.0,"unit":"°C"},
  {"name":"humidity","value":65.0,"unit":"%"},
  {"name":"pressure","value":101.3,"unit":"kPa"},
  {"name":"flow_rate","value":15.0,"unit":"L/min"}
]

Topic: factory/workshop1/device002/data
Payload: [
  {"name":"pressure","value":101.3,"unit":"bar"},
  {"name":"temperature","value":25.0,"unit":"°C"}
]

Topic: factory/workshop1/device003/data
Payload: [
  {"name":"flow_rate","value":15.0,"unit":"m³/h"},
  {"name":"total_flow","value":1000.0,"unit":"m³"},
  {"name":"status","value":1,"unit":""}
]
```

### 4. 多订阅者接收

```
订阅者1（监控系统）
  ↓ Subscribe: factory/workshop1/#
  ↓ 接收所有设备数据
  ↓ 实时显示

订阅者2（数据库）
  ↓ Subscribe: factory/workshop1/+/data
  ↓ 接收所有设备数据
  ↓ 存储到数据库

订阅者3（分析系统）
  ↓ Subscribe: factory/workshop1/device001/data
  ↓ 只接收设备1数据
  ↓ 进行数据分析
```

## 🎯 优势分析

### 为什么使用 Modbus 单播？

1. **设备兼容性** - 大多数工业设备支持 Modbus
2. **简单可靠** - 请求-响应模式，易于调试
3. **点对点控制** - 每个设备独立采集，互不影响
4. **标准协议** - 无需修改设备固件

### 为什么使用 MQTT 广播？

1. **解耦架构** - 网关不需要知道有多少订阅者
2. **灵活订阅** - 订阅者可以随时加入/退出
3. **主题过滤** - 订阅者可以选择性接收数据
4. **QoS 保证** - 支持不同级别的消息可靠性
5. **轻量高效** - 适合物联网场景

## 📈 性能特性

### 并发能力

- **Modbus 采集**：每个设备一个线程，支持 100+ 设备并发
- **MQTT 发布**：异步发布，不阻塞采集线程
- **数据缓存**：内存缓存最新数据，快速查询

### 可靠性

- **Modbus 重试**：连接失败自动重试
- **MQTT 自动重连**：网络断开自动恢复
- **数据缓存**：网络故障时缓存数据，恢复后补发

### 扩展性

- **水平扩展**：多个网关并行工作
- **垂直扩展**：单个网关支持更多设备
- **协议扩展**：易于添加新协议（OPC UA、BACnet等）

## 🔧 配置示例

完整配置请参考：`config/config-iot-complete.yaml`

## 📚 相关文档

- [Modbus 协议说明](MODBUS_PROTOCOL.md)
- [MQTT 集成指南](MQTT_INTEGRATION.md)
- [快速开始](../QUICK_START_IOT.md)
