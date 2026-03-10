# 编译和测试指南

## ✅ 已修复的问题

### 问题1: 回调函数命名不一致
```cpp
// 错误
if (packet_callback_) {
    packet_callback_(frame_data);
}

// 正确
if (packetCallback_) {
    packetCallback_(frame_data);
}
```

### 问题2: 类型转换溢出警告
```cpp
// 错误
out.push_back((FRAME_HEADER >> 8) & 0xFF);

// 正确
out.push_back(static_cast<char>((FRAME_HEADER >> 8) & 0xFF));
```

### 问题3: 插件注册方式错误
```cpp
// 错误
ApplicationRegistry::registerApplication("ecg_device", ...);

// 正确
ApplicationRegistry::getInstance().registerApplication("ecg_device", ...);
```

### 问题4: 缺少头文件
```cpp
// 需要添加
#include "util/EnhancedConfigReader.h"
```

---

## 📦 编译步骤

### Windows环境

#### 方法1: 使用提供的批处理脚本
```cmd
build_ecg_device.bat
```

#### 方法2: 手动编译
```cmd
REM 1. 创建构建目录
mkdir build
cd build

REM 2. 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

REM 3. 编译
cmake --build . --config Release

REM 4. 返回项目根目录
cd ..
```

### Linux环境
```bash
# 1. 创建构建目录
mkdir -p build && cd build

# 2. 配置CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. 编译
make -j$(nproc)

# 4. 返回项目根目录
cd ..
```

---

## 🚀 运行测试

### Step 1: 启动心电设备服务器

#### Windows
```cmd
test_ecg_device.bat
```

或手动运行：
```cmd
build\bin\NetBox.exe config\config-ecg-device.yaml
```

#### Linux
```bash
./build/bin/NetBox config/config-ecg-device.yaml
```

**预期输出**：
```
========================================
  医疗设备数据采集与监控系统
  设备模拟器 v1.0
========================================

配置信息：
  监听地址: 0.0.0.0:8899
  设备ID: 1001
  初始心率: 75 BPM

✅ 设备模拟器已启动
   等待监控客户端连接...
   按 Ctrl+C 退出

提示：
  - 采样率: 500Hz (每2ms发送一个数据点)
  - 协议格式: 帧头(0xAA55) + 设备ID + 数据类型 + 长度 + 数据 + 校验和
  - 数据类型: 0x01=心电数据
```

### Step 2: 运行测试客户端

打开**新的终端窗口**，运行：

```bash
python test_ecg_client.py
```

或指定参数：
```bash
python test_ecg_client.py 127.0.0.1 8899 10
# 参数: IP地址 端口 持续时间(秒)
```

**预期输出**：
```
========================================
  心电设备测试客户端
========================================

✅ 已连接到心电设备服务器 127.0.0.1:8899

开始接收心电数据（持续10秒）...
============================================================
[  100] 心率:  75 BPM | 值:   234 mV | ████████████
[  200] 心率:  75 BPM | 值:  1456 mV | ████████████████████████
[  300] 心率:  75 BPM | 值:  -123 mV | ████████
[  400] 心率:  75 BPM | 值:   567 mV | ██████████████
...
============================================================

统计信息:
  接收采样点数: 5000
  采样率: 500.0 Hz
  最后心率: 75 BPM

✅ 连接已关闭

测试完成！
```

---

## 🔍 验证要点

### 1. 编译成功
- ✅ 无编译错误
- ✅ 只有警告（可忽略）
- ✅ 生成可执行文件 `build/bin/NetBox.exe`

### 2. 服务器启动成功
- ✅ 显示"设备模拟器已启动"
- ✅ 监听端口8899
- ✅ 无错误日志

### 3. 客户端连接成功
- ✅ 显示"已连接到心电设备服务器"
- ✅ 能接收到数据
- ✅ 采样率约500Hz

### 4. 数据正确性
- ✅ 帧头正确（0xAA55）
- ✅ 设备ID正确（1001）
- ✅ 数据类型正确（0x01）
- ✅ 校验和验证通过
- ✅ 心率在合理范围（60-100 BPM）
- ✅ 心电值在合理范围（-2048 ~ 2047）

---

## 🐛 常见问题

### Q1: 编译时找不到头文件
**错误**：`fatal error: MedicalProtocol.h: No such file or directory`

**解决**：确保头文件在正确位置
```
Protocol/include/MedicalProtocol.h  ✅ 正确
MedicalMonitor/protocol/MedicalProtocol.h  ❌ 错误（旧位置）
```

### Q2: 链接错误
**错误**：`undefined reference to MedicalProtocol::...`

**解决**：确保CMakeLists.txt中添加了源文件
```cmake
Protocol/src/MedicalProtocol.cpp
app/src/ECGDeviceServer.cpp
plugins/medical/ECGDevicePlugin.cpp
```

### Q3: 服务器启动失败
**错误**：`bind failed: Address already in use`

**解决**：端口8899被占用，修改配置文件或关闭占用端口的程序
```bash
# Windows查看端口占用
netstat -ano | findstr 8899

# Linux查看端口占用
lsof -i :8899
```

### Q4: 客户端连接失败
**错误**：`Connection refused`

**解决**：
1. 确认服务器已启动
2. 检查防火墙设置
3. 确认IP和端口正确

### Q5: 接收不到数据
**可能原因**：
1. 协议解析错误（检查帧头、校验和）
2. 粘包处理问题
3. 网络延迟

**调试方法**：
```python
# 在test_ecg_client.py中添加调试输出
print(f"接收到原始数据: {data.hex()}")
```

---

## 📊 性能验证

### 预期性能指标
- **采样率**：500Hz ± 5%
- **数据包大小**：15字节/包
- **带宽**：7.5KB/s（每客户端）
- **延迟**：< 10ms
- **丢包率**：< 0.1%

### 性能测试命令
```bash
# 测试60秒，统计采样率
python test_ecg_client.py 127.0.0.1 8899 60
```

---

## ✅ Day 3-4 完成标志

- [x] 医疗协议实现（Protocol层）
- [x] 心电设备服务器实现（app层）
- [x] 插件注册（plugins层）
- [x] 配置文件（config层）
- [x] CMakeLists.txt修改
- [x] 编译成功
- [x] 服务器启动成功
- [x] 客户端测试通过
- [x] 数据正确性验证

---

## 🎯 下一步：Day 5-6 Qt监控客户端

准备工作：
1. 安装Qt5/Qt6
2. 学习QTcpSocket
3. 学习QCustomPlot
4. 设计监控界面

详见：`MedicalMonitor/DAY5-6_PLAN.md`
