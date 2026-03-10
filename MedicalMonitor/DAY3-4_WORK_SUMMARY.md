# Day 3-4 工作总结：设备模拟器开发

## ✅ 已完成的工作

### 1. 协议层（Protocol/）
- ✅ 创建 `Protocol/include/MedicalProtocol.h` - 医疗协议定义
- ✅ 创建 `Protocol/src/MedicalProtocol.cpp` - 医疗协议实现
- ✅ 定义心电数据结构 `ECGData`
- ✅ 实现协议帧封装/解析（帧头+设备ID+数据类型+长度+数据+校验和）
- ✅ 实现粘包处理逻辑

### 2. 应用层（app/）
- ✅ 创建 `app/include/ECGDeviceServer.h` - 心电设备服务器头文件
- ✅ 创建 `app/src/ECGDeviceServer.cpp` - 心电设备服务器实现
- ✅ 继承 `ApplicationServer`，复用netbox网络通信能力
- ✅ 实现心电波形生成算法（P波、QRS波群、T波）
- ✅ 实现500Hz采样率数据发送（每2ms一个数据点）
- ✅ 实现多客户端广播功能

### 3. 插件层（plugins/medical/）
- ✅ 创建 `plugins/medical/ECGDevicePlugin.cpp` - 心电设备插件
- ✅ 遵循netbox插件注册机制
- ✅ 支持通过配置文件创建设备实例

### 4. 配置文件（config/）
- ✅ 创建 `config/config-ecg-device.yaml` - 心电设备配置
- ✅ 定义设备ID、心率、采样率等参数

---

## 📋 下一步工作（Day 5-6）

### 需要修改主CMakeLists.txt

在主 `CMakeLists.txt` 的 `add_executable(NetBox ...)` 部分添加：

```cmake
# 医疗协议
Protocol/src/MedicalProtocol.cpp

# 心电设备应用
app/src/ECGDeviceServer.cpp

# 医疗设备插件
plugins/medical/ECGDevicePlugin.cpp
```

### 编译命令

```bash
# 1. 创建构建目录
mkdir -p build && cd build

# 2. 配置CMake
cmake ..

# 3. 编译
cmake --build . --config Release

# 4. 运行心电设备服务器
./bin/NetBox ../config/config-ecg-device.yaml
```

---

## 🎯 技术要点讲解

### 1. 为什么要遵循netbox项目结构？

**原因**：
- ✅ 复用现有的编译系统（CMakeLists.txt）
- ✅ 复用现有的基础设施（Logger、ThreadPool、IOMultiplexer）
- ✅ 保持代码风格一致
- ✅ 方便后续维护和扩展

**netbox分层架构**：
```
Protocol/     ← 协议层（MedicalProtocol）
    ↓
app/          ← 应用层（ECGDeviceServer）
    ↓
plugins/      ← 插件层（ECGDevicePlugin）
    ↓
config/       ← 配置文件（config-ecg-device.yaml）
```

### 2. 医疗协议设计详解

**协议格式**：
```
+--------+----------+-----------+--------+------+----------+
| 帧头   | 设备ID   | 数据类型  | 长度   | 数据 | 校验和   |
| 2字节  | 2字节    | 1字节     | 2字节  | N字节| 1字节    |
+--------+----------+-----------+--------+------+----------+
```

**为什么这样设计？**
- **帧头（0xAA55）**：快速识别帧边界，处理粘包
- **设备ID**：支持多设备场景（一个服务器管理多个设备）
- **数据类型**：扩展性（0x01=心电，0x02=血压，0x03=体温）
- **长度字段**：支持变长数据
- **校验和**：简单但有效的数据完整性验证

### 3. 心电波形生成算法

**真实心电波形组成**：
```
P波：心房去极化（小波）
  ↓
QRS波群：心室去极化（主波，最高）
  ├─ Q波（负向小波）
  ├─ R波（正向大波）
  └─ S波（负向小波）
  ↓
T波：心室复极化（中等波）
```

**代码实现**：
```cpp
// P波（0.0 ~ 0.15周期）
baseline += 150.0 * exp(-t * t / 0.1);  // 高斯波

// R波（0.20 ~ 0.28周期）
baseline += 1500.0 * sin((phase - 0.20) / 0.08 * M_PI);  // 正弦波

// T波（0.45 ~ 0.70周期）
baseline += 250.0 * exp(-t * t / 0.2);  // 高斯波
```

### 4. 如何复用netbox？

**继承关系**：
```
TcpServer（netbox已有）
    ↓ 继承
ApplicationServer（netbox已有）
    ↓ 继承
ECGDeviceServer（新建）
```

**复用的功能**：
- ✅ TCP网络通信（TcpServer）
- ✅ IO多路复用（Epoll/Select/Poll）
- ✅ 客户端管理（连接/断开）
- ✅ 日志系统（Logger）
- ✅ 配置管理（EnhancedConfigReader）

**新增的功能**：
- ✅ 心电波形生成
- ✅ 500Hz数据发送
- ✅ 医疗协议封装

---

## 🐛 常见问题

### Q1: 为什么禁用TCP层心跳包？
```cpp
setHeartbeatEnabled(false);
```
**答**：医疗设备每2ms就发送数据，本身就是"心跳"，不需要额外的TCP心跳包。

### Q2: 为什么使用MSG_DONTWAIT？
```cpp
send(fd, data.data(), data.size(), MSG_DONTWAIT);
```
**答**：非阻塞发送，避免某个客户端阻塞影响其他客户端。

### Q3: 粘包问题如何处理？
**答**：在 `MedicalProtocol::onDataReceived()` 中：
1. 维护接收缓冲区 `buffer_`
2. 查找帧头 `0xAA55`
3. 检查帧长度是否完整
4. 验证校验和
5. 提取完整帧，移除已处理数据

---

## 📊 性能指标

- **采样率**：500Hz（每2ms一个数据点）
- **数据包大小**：8字节（帧头+设备ID+类型+长度+校验和）+ 7字节（ECGData）= 15字节
- **带宽需求**：15字节 × 500Hz = 7.5KB/s（每个客户端）
- **支持客户端数**：理论上无限制（受限于网络带宽）

---

## 🎓 学到的知识点

1. **医疗设备通信协议设计**
2. **心电波形生成算法**
3. **高频数据采集与发送**（500Hz）
4. **粘包处理**
5. **多客户端广播**
6. **netbox框架的正确使用方式**

---

## 下一步：Qt监控客户端开发（Day 5-6）

准备工作：
1. 安装Qt5/Qt6
2. 学习QTcpSocket（TCP通信）
3. 学习QCustomPlot（波形绘制）
4. 设计监控界面布局
