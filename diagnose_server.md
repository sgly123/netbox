# WebSocket 服务器诊断报告

## 测试结果分析

根据你的测试结果：
- **成功连接**: 42/100 (42%)
- **失败连接**: 58/100 (58%)
- **失败原因**: `timed out during opening handshake`
- **成功客户端延迟**: 优秀 (平均 0.76ms)

## 问题诊断

### 主要问题：服务器连接队列满或处理能力不足

你的服务器在处理 **42个并发连接** 后开始拒绝新连接，这表明：

1. **监听队列（backlog）太小**
   - 服务器的 `listen()` backlog 参数可能设置过小
   - 建议值：128 或更高

2. **连接处理速度慢**
   - WebSocket 握手处理可能阻塞了新连接的接受
   - 需要异步处理握手过程

3. **文件描述符限制**
   - Windows 默认限制可能较低
   - 需要检查系统资源限制

4. **线程池/工作线程不足**
   - 如果使用线程池处理连接，可能线程数不够

## 解决方案

### 方案1：调整服务器代码

#### 增加 listen backlog
```cpp
// 在 TcpServer::start() 中
int backlog = 128;  // 或更高，如 256, 512
if (listen(m_listenFd, backlog) < 0) {
    // 错误处理
}
```

#### 检查 accept 循环
```cpp
// 确保 accept 在独立线程或异步处理
while (running) {
    int clientFd = accept(m_listenFd, ...);
    if (clientFd > 0) {
        // 立即将连接交给工作线程处理
        threadPool->submit([this, clientFd]() {
            handleNewConnection(clientFd);
        });
    }
}
```

#### 优化 WebSocket 握手
```cpp
// 确保握手处理不阻塞 accept 循环
void handleWebSocketHandshake(int clientFd) {
    // 快速完成握手
    // 避免在握手中进行耗时操作
}
```

### 方案2：调整系统参数

#### Windows
```powershell
# 增加动态端口范围
netsh int ipv4 set dynamicport tcp start=1024 num=64511

# 检查防火墙设置
netsh advfirewall show allprofiles
```

#### Linux
```bash
# 增加文件描述符限制
ulimit -n 65535

# 调整 TCP 参数
sysctl -w net.core.somaxconn=1024
sysctl -w net.ipv4.tcp_max_syn_backlog=2048
```

### 方案3：使用优化的测试参数

使用改进后的压测脚本，降低连接速率：

```bash
# 慢速连接测试（推荐）
python websocket_stress_test.py -c 100 -n 10 -r 5 -t 30 --retries 3

# 参数说明：
# -r 5: 每秒只建立5个连接（给服务器更多时间）
# -t 30: 连接超时30秒（默认10秒可能不够）
# --retries 3: 失败后重试3次
```

## 建议的测试步骤

### 步骤1：找到服务器连接上限
```bash
# 运行渐进式测试
test_find_limit.bat  # Windows
./test_find_limit.sh # Linux
```

### 步骤2：优化服务器配置
根据步骤1的结果，调整服务器代码中的：
- listen backlog
- 线程池大小
- 连接处理逻辑

### 步骤3：重新测试
```bash
# 使用优化参数测试
test_optimized.bat  # Windows
./test_optimized.sh # Linux
```

## 性能优化建议

### 代码层面

1. **异步 accept**
   ```cpp
   // 使用 epoll 监听 listen socket
   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);
   ```

2. **连接池**
   ```cpp
   // 预分配连接对象，避免频繁 new/delete
   std::vector<Connection*> connectionPool;
   ```

3. **减少握手时间**
   ```cpp
   // 优化 WebSocket 握手响应生成
   // 使用预计算的 SHA-1 查找表
   ```

### 架构层面

1. **多进程模型**
   - 使用 SO_REUSEPORT（Linux）
   - 多个进程监听同一端口

2. **负载均衡**
   - 使用 Nginx 作为前端代理
   - 分发连接到多个后端服务器

3. **连接限流**
   - 实现令牌桶算法
   - 限制每秒新建连接数

## 预期性能目标

根据 README.md 中的性能指标：

| 指标 | 当前值 | 目标值 | 状态 |
|------|--------|--------|------|
| 并发连接 | 42 | 1,000+ | ❌ 需优化 |
| 成功率 | 42% | 99%+ | ❌ 需优化 |
| 平均延迟 | 0.76ms | 0.6ms | ✅ 优秀 |
| QPS | 375 | 40,000 | ❌ 需优化 |

## 下一步行动

1. ✅ **立即执行**: 运行 `test_optimized.bat` 使用慢速连接测试
2. 🔍 **代码检查**: 检查 `TcpServer::start()` 中的 `listen()` backlog 参数
3. 🔧 **代码修改**: 增加 backlog 到 512
4. 📊 **重新测试**: 使用 `test_find_limit.bat` 找到新的连接上限
5. 🚀 **持续优化**: 根据测试结果继续优化

## 参考资料

### 查看服务器代码
```bash
# 查找 listen 调用
grep -r "listen(" app/ NetFramework/

# 查找 accept 调用
grep -r "accept(" app/ NetFramework/

# 查找线程池配置
grep -r "ThreadPool" app/ NetFramework/
```

### 监控服务器
```bash
# Windows - 查看连接数
netstat -an | findstr "8001" | findstr "ESTABLISHED"

# Linux - 查看连接数
netstat -an | grep 8001 | grep ESTABLISHED | wc -l

# 查看进程资源使用
# Windows: 任务管理器
# Linux: top -p <pid>
```

## 总结

你的服务器在 **消息处理性能** 方面表现优秀（延迟 0.76ms），但在 **连接接受能力** 方面存在瓶颈。

主要优化方向：
1. 增加 listen backlog
2. 优化连接接受循环
3. 使用慢速连接测试避免压垮服务器
4. 考虑使用多进程/多实例架构

先运行 `test_optimized.bat` 看看慢速连接是否能提高成功率！
