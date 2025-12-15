# 🚀 NetBox - 企业级跨平台网络框架

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/netbox/netbox)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-blue.svg)](https://github.com/netbox/netbox)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.1.0-orange.svg)](https://github.com/netbox/netbox/releases)
[![技术栈](https://img.shields.io/badge/技术栈-C%2B%2B17%20%7C%20CMake%20%7C%20Epoll-red.svg)](#技术栈)

## 项目概述

NetBox是基于**C++17**开发的企业级高性能网络框架，采用**分层架构设计**，支持多种协议和应用场景。

- **高性能网络编程**：支持Epoll/IOCP/Kqueue三种IO多路复用模型
- **分层架构设计**：应用层→协议层→网络层，完全解耦，高度可扩展  
- ** 智能协议路由**：支持多协议共存，自动识别并路由到对应处理器
- **插件化扩展**：支持动态协议注册，配置驱动的服务器创建
- **生产级特性**：异步日志、线程池、配置管理、性能监控
- **完整WebSocket实现**：RFC 6455标准协议、帧解析/封装、多客户端广播、线程安全设计

### 📈 性能指标
#### 场景 1：通用连接 / 消息传输压测
测试核心：验证不同并发连接数、消息量下的连接稳定性和消息传输性能，共 5 个测试用例。

| 测试用例 | 总连接数 | 成功连接数 | 成功率 | 发送消息数 | 接收消息数 | QPS | 平均延迟 | P95 延迟 | P99 延迟 | 最大延迟 |
|----------|----------|------------|--------|------------|------------|-----|----------|----------|----------|----------|
| 1kx10（1000 连接 × 10 消息） | 1000 | 994 | 99.40% | 9940 | 9940 | 87.59 | 2.62ms | 18.56ms | 28.80ms | 70.80ms |
| 2000qps x10（2000 连接 × 10 消息） | 2000 | 1992 | 99.60% | 19920 | 19920 | 94.23 | 2.66ms | 18.59ms | 27.64ms | 76.92ms |
| 5kx10（5000 连接 × 10 消息） | 5000 | 4988 | 99.76% | 49880 | 49880 | 97.35 | 2.51ms | 17.87ms | 27.62ms | 132.93ms |
| -c 2000 -n 10 --rate 200（2000 连接 × 10 消息 × 速率 200） | 2000 | 1780 | 89.00% | 17800 | 17623 | 334.24 | 119.60ms | 687.81ms | 4366.70ms | 4759.15ms |
| -c 5000 -n 10 --rate 100（5000 连接 × 10 消息 × 速率 100） | 5000 | 5000 | 100.00% | 50000 | 50000 | 835.06 | 10.99ms | 105.41ms | 176.10ms | 399.88ms |

#### 场景 2：Mini-Redis 数据操作压测
测试核心：验证 Redis 服务在不同并发客户端、操作量下的读写删性能（读操作占比 70%，Key 10 字节，Value 100 字节）。

| 测试用例（并发客户端 × 每客户端操作数） | 总操作数 | 成功率 | SET 操作 | GET 操作 | DEL 操作 | QPS | 平均延迟 | P95 延迟 | P99 延迟 | 性能评估 |
|-----------------------------------------|----------|--------|----------|----------|----------|-----|----------|----------|----------|----------|
| 10×100 | 1000 | 100.00% | 232 | 717 | 51 | 2992.87 | 2.58ms | 5.32ms | 8.23ms | 良好（<5ms） |
| 50×1000 | 50000 | 100.00% | 12444 | 35001 | 2555 | 6626.54 | 7.31ms | 8.95ms | 10.52ms | 良好（<10ms） |
| 高负载场景（总操作数 10 万） | 100000 | 100.00% | 25103 | 69824 | 5073 | 5671.99 | 17.27ms | 19.61ms | 22.49ms | 较差（≥10ms） |
| 高负载场景（总操作数 20 万） | 200000 | 100.00% | 50135 | 139817 | 10048 | 5569.51 | 35.08ms | 41.42ms | 46.92ms | 较差（≥10ms） |

### 四、性能分析
#### 1. 通用连接 / 消息传输场景
- **优势**：低并发（1000-5000 连接）、无速率限制时，连接成功率高达 99.40%-100%，平均延迟仅 2.5-2.7ms，性能稳定。
- **瓶颈**：当设置速率限制（--rate 200）且并发 2000 连接时，连接成功率降至 89%，平均延迟飙升至 119.6ms，最大延迟达 4.7s，存在速率控制下的连接阻塞问题。
- **最优表现**：`-c 5000 -n 10 --rate 100` 用例，100% 连接成功率 + 835 QPS + 10.99ms 平均延迟，平衡了高并发和低延迟。

#### 2. Mini-Redis 数据操作场景
- **优势**：低-中负载（≤5 万操作）时，性能优异，QPS 最高达 6626.54，平均延迟 <8ms，读写删操作稳定无失败。
- **瓶颈**：高负载（≥10 万操作）时，QPS 略有下降（5500+），平均延迟显著升高（17-35ms），达到性能评估 “较差” 阈值，需优化高负载下的资源分配。
- **关键发现**：读操作占比 70% 的场景下，低负载时延迟控制优秀，高负载下延迟增长与操作量正相关，可能受限于内存缓存命中率或网络 IO。
## 30秒快速体验

### 方式1：启动WebSocket聊天服务器 

```bash
# 1. 编译项目
cmake -B build && cmake --build build --config Release

# 2. 启动WebSocket服务器
./build/bin/netbox_server config/config-websocket.yaml

# 3. 使用浏览器或WebSocket客户端连接
# 地址：ws://localhost:8001
```

**测试效果**：
- 打开多个浏览器标签页
- 连接到 `ws://localhost:8001`
- 发送消息，所有客户端都能实时收到广播
- 支持多客户端聊天室功能

### 方式2：docker一键部署

```bash
# 克隆项目
git clone https://github.com/netbox/netbox.git
cd NetBox
docker-compose up --build
```

### 输出效果
```
🏆 NetBox CLI v2.1 - 企业级网络框架
==========================================
创建NetBox项目: MyProject
项目创建成功! 支持Echo/Redis/HTTP/WebSocket四种服务器
构建完成 (Release模式，4线程并行)
服务器启动: 127.0.0.1:8888 (Epoll模式)
等待客户端连接...
```

---

## 🏗️ 项目架构

### 分层架构设计
```
┌──────────────────────────────────────────────┐
│       应用层 (Application Layer)             │
│   EchoServer | RedisServer | HTTP | WebSocket│
├──────────────────────────────────────────────┤
│        协议层 (Protocol Layer)               │
│  ProtocolRouter | RESP | SimpleHdr | WebSocket│ 
├──────────────────────────────────────────────┤
│        网络层 (Network Layer)                │
│   TcpServer | IOMultiplexer                 │
├──────────────────────────────────────────────┤
│         基础层 (Base Layer)                  │
│  ThreadPool | Logger | Config               │
└──────────────────────────────────────────────┘
```

### 核心技术特性

#### IO多路复用支持
```cpp
// 跨平台IO模型抽象
class IOMultiplexer {
    enum IOType { EPOLL, IOCP, KQUEUE, SELECT, POLL };
    virtual int wait(int timeout) = 0;
    virtual bool addSocket(int fd, uint32_t events) = 0;
};

// Linux高性能实现
class EpollMultiplexer : public IOMultiplexer {
    int epoll_wait(epoll_fd, events, max_events, timeout);
};
```

#### 智能协议路由
```cpp
// 协议自动识别和路由
class ProtocolRouter {
    std::map<uint32_t, std::shared_ptr<ProtocolBase>> protocols_;
    
    size_t onDataReceived(int client_fd, const char* data, size_t len) {
        uint32_t protocolId = detectProtocol(data, len);
        auto protocol = protocols_[protocolId];
        return protocol->onDataReceived(data + 4, len - 4);
    }
};
```

####  插件化应用注册
```cpp
// 配置驱动的服务器创建
#define REGISTER_APPLICATION(name, class_type) \
    static ApplicationRegistrar<class_type> registrar_##class_type(name);

REGISTER_APPLICATION("echo", EchoServer);
REGISTER_APPLICATION("redis_app", RedisApplicationServer);
REGISTER_APPLICATION("http", HttpServer);
```

---

## 支持的应用场景

### 1.  Echo回显服务器
- **协议**：自定义SimpleHeader协议（长度前缀）
- **特点**：演示基础网络编程和协议设计
- **用途**：网络编程教学、协议解析演示
- **技术亮点**：心跳保活、长连接维护

### 2. Redis数据库服务器  
- **协议**：完整Redis RESP协议实现
- **特点**：支持标准Redis命令，智能禁用心跳
- **用途**：高性能缓存服务、数据存储
- **技术亮点**：协议冲突解决、4Vx问题修复

### 3.  HTTP Web服务器
- **协议**：HTTP/1.1标准协议
- **特点**：静态文件服务、RESTful API支持
- **用途**：Web应用开发、API网关
- **技术亮点**：Keep-Alive连接复用

### 4.  WebSocket实时通信服务器 
- **协议**：完整WebSocket RFC 6455协议实现
- **特点**：支持多客户端实时广播、双向通信、UTF-8验证
- **用途**：实时聊天室、在线协作、游戏服务器、实时推送
- **技术亮点**：
  - ✅ 完整的WebSocket握手和帧解析
  - ✅ 掩码/解掩码处理（字节序正确性）
  - ✅ 多客户端广播和消息路由
  - ✅ 每客户端协议实例管理（避免状态污染）
  - ✅ 线程安全的并发发送（per-client mutex）
  - ✅ TCP心跳包与应用层协议隔离
  - ✅ UTF-8编码验证和错误处理

---

## 💻 项目技术栈

### 核心技术
- **编程语言**：C++17 (现代C++特性)
- **构建系统**：CMake 3.16+ (跨平台构建)
- **并发模型**：IO多路复用 + 线程池
- **网络协议**：TCP/UDP Socket编程
- **设计模式**：工厂模式、策略模式、观察者模式

### 平台支持
- **Linux**：Epoll高性能IO，支持SO_REUSEPORT
- **Windows**：IOCP完成端口模型  
- **macOS**：Kqueue事件通知机制

### 依赖管理
- **日志系统**：自研异步日志 + spdlog
- **配置管理**：YAML配置文件解析
- **线程库**：POSIX Threads

---

##  快速二次开发

### 扩展新协议
```cpp
class MyProtocol : public ProtocolBase {
public:
    size_t onDataReceived(const char* data, size_t len) override {
        // 实现自定义协议解析逻辑
        return processMyProtocol(data, len);
    }
    
    bool pack(const char* data, size_t len, std::vector<char>& out) override {  
        // 实现协议封装逻辑
        return packMyProtocol(data, len, out);
    }
};

// 注册协议
REGISTER_PROTOCOL(MyProtocol)
```

### 添加新应用
```cpp
class GameServer : public ApplicationServer {
public:
    void onPacketReceived(const std::vector<char>& packet) override {
        // 处理游戏逻辑
        handleGamePacket(packet);
    }
    
    void onClientConnected(int client_fd) override {
        // 处理玩家连接
        handlePlayerJoin(client_fd);
    }
};

// 注册应用
REGISTER_APPLICATION("game", GameServer);
```

---

##  WebSocket技术难题解决全记录

### 问题背景

开发WebSocket实时聊天服务器时，遇到了一系列生产级技术难题，每个问题都涉及深层次的协议理解和系统设计。

### 问题解决时间线

#### 问题：客户端发送一次数据后断开（UTF-8解码错误）

**现象**：
```
WebSocket connection failed: Could not decode a text frame as UTF-8
```

**根因分析**：
- WebSocket协议要求客户端发送的帧必须**带掩码**（Mask bit = 1）
- 服务器需要用4字节掩码键（Masking Key）对payload进行**解掩码**
- 错误代码使用了 `ntohl(maskingKeyNet)`，这会**改变字节顺序**
- 导致解掩码后的数据是乱码，无法通过UTF-8校验

**解决方案**：
```cpp
// ❌ 错误做法：ntohl会改变字节顺序
header.masking_key = ntohl(maskingKeyNet);

// ✅ 正确做法：直接按字节复制
std::memcpy(header.masking_key, data + pos, 4);
```

**技术要点**：
- 理解掩码键不是一个32位整数，而是**4个独立的字节**
- 网络字节序转换（`ntohl`）只适用于数值，不适用于掩码键
- WebSocket帧结构的精确理解（RFC 6455）

---

#### 问题："A server must not mask any frames"（共享实例问题）

**现象**：
```
WebSocket connection failed: A server must not mask any frames that it sends to the client.
```

**根因分析**：
- `WebSocketServer` 创建了一个**共享的** `WebSocketProtocol` 实例
- 所有客户端都使用同一个协议实例，导致**状态污染**
- 客户端A的掩码键可能影响到客户端B的帧封装
- 服务器发送的帧被错误地标记为"已掩码"

**架构缺陷**：
```cpp
//  错误架构：所有客户端共享一个协议实例
class WebSocketServer {
    std::unique_ptr<ProtocolRouter> protocolRouter_; // 共享实例
};
```

**解决方案**：
```cpp
// 正确架构：每个客户端一个独立的协议实例
class ApplicationServer {
    std::unordered_map<int, std::shared_ptr<ProtocolBase>> m_clientProtocols;
    // 在客户端连接时创建独立实例
};
```

**技术要点**：
- 有状态协议必须采用**per-client instance**设计
- 理解共享可变状态在多线程环境中的危险性
- 分层架构中责任的正确划分

---

#### 问题3️⃣：间歇性"Server must not mask"（线程安全问题）

**现象**：
- 单客户端测试正常
- 多客户端高频发送时**间歇性失败**
- 容器环境下更容易复现

**根因分析**：
- `m_clientProtocols` 映射表没有互斥锁保护
- 多线程并发访问导致**数据竞争（data race）**
- 内存损坏导致帧数据被错误解析为"带掩码"

**解决方案**：
```cpp
class ApplicationServer {
    std::unordered_map<int, std::shared_ptr<ProtocolBase>> m_clientProtocols;
    std::mutex m_clientProtocolsMutex; // ✅ 添加互斥锁

    void onDataReceived(...) {
        std::lock_guard<std::mutex> lock(m_clientProtocolsMutex);
        auto proto = m_clientProtocols[clientFd]; // 线程安全访问
    }
};
```

**技术要点**：
- 识别共享数据结构的并发访问风险
- RAII风格的锁管理（`std::lock_guard`）
- 容器环境资源限制下的并发问题放大效应

---

#### 问题：帧交错问题（Frame Interleaving）

**现象**：
- 即使有协议实例锁，仍然出现"masked frame"错误
- 日志显示帧封装正确，但客户端收到乱码

**根因分析**：
- `m_clientProtocolsMutex` 只保护协议实例的**访问**
- 但多个线程可以**同时调用** `::send()` 向同一个socket发送
- TCP是字节流，多个线程的数据会**交错混合**
- 导致WebSocket帧边界错乱

**示例**：
```
线程1发送：[81 05 H e l l o]
线程2发送：[81 05 W o r l d]
实际到达：[81 05 H 81 05 W e l o r l l d o] ❌ 帧边界破坏
```

**解决方案**：
```cpp
class ApplicationServer {
    std::unordered_map<int, std::shared_ptr<std::mutex>> m_clientSendMutexes;
    
    void broadcast(...) {
        auto sendMutex = m_clientSendMutexes[clientFd];
        std::lock_guard<std::mutex> sendLock(*sendMutex); // ✅ 每客户端发送锁
        ::send(clientFd, frame.data(), frame.size(), 0);
    }
};
```

**技术要点**：
- **细粒度锁**：per-client mutex vs global mutex
- 理解TCP的字节流特性与应用层帧的关系
- 原子性操作的边界定义

---

#### 问题：TCP心跳包与应用层协议冲突 🎯 **架构级问题**

**现象**：
```
客户端接收到："�Masked frame from server"
客户端主动关闭连接
```

**根因分析**：
- `TcpServer` 基类每10秒发送**4字节TCP心跳包**（魔数 `0xAABBCCDD`）
- WebSocket客户端（浏览器）将**所有接收数据**都当作WebSocket帧解析
- 心跳包的某个字节恰好满足"掩码位=1"的条件
- 浏览器误判为"服务器发送了带掩码的帧" → 断开连接

**架构问题**：
```
TcpServer（发送原始心跳包）
    ↓
ApplicationServer
    ↓
WebSocketServer ← 浏览器无法识别TCP层心跳包
```

**解决方案**：
```cpp
WebSocketServer::WebSocketServer(...) {
    setHeartbeatEnabled(false); // ✅ 禁用TCP层心跳
    // WebSocket使用自己的PING/PONG帧
}
```
