# NetBox 测试工具集

> 完整的性能压测和内存检测工具

---

## 📦 工具列表

### 1. 性能压测工具

| 文件 | 说明 | 平台 |
|------|------|------|
| `performance_benchmark.py` | 自动化性能压测脚本 | 全平台 |
| `run_all_tests.sh` | 一键运行所有测试（Linux/macOS） | Linux/macOS |
| `run_performance_test.bat` | 性能测试启动脚本（Windows） | Windows |

### 2. 内存检测工具

| 文件 | 说明 | 平台 |
|------|------|------|
| `memory_leak_detection.sh` | Valgrind内存检测脚本 | Linux/macOS |

---

## 🚀 快速开始

### Linux/macOS

```bash
# 1. 安装依赖
pip3 install -r requirements.txt

# 2. 编译项目
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 3. 运行完整测试
chmod +x tools/run_all_tests.sh
./tools/run_all_tests.sh
```

### Windows

```batch
REM 1. 安装依赖
pip install -r requirements.txt

REM 2. 编译项目
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

REM 3. 运行性能测试
tools\run_performance_test.bat
```

---

## 📊 工具详解

### performance_benchmark.py

**功能**:
- ✅ Redis QPS压测
- ✅ WebSocket QPS压测
- ✅ 延迟统计（平均、P50、P95、P99）
- ✅ 自动生成图表（QPS对比、延迟对比、延迟分布）
- ✅ 生成Markdown格式报告

**使用方法**:

```bash
# 确保服务器已启动
# 终端1: ./build/bin/netbox_server config/config-redis.yaml
# 终端2: ./build/bin/netbox_server config/config-websocket.yaml

# 运行压测
python3 tools/performance_benchmark.py
```

**自定义配置**:

编辑脚本中的参数:
```python
# Redis压测
redis_results = redis_benchmark.run_test(
    num_operations=100000,  # 总操作数
    num_clients=10          # 并发数
)

# WebSocket压测
websocket_results = websocket_benchmark.run_test(
    num_messages=10000,     # 总消息数
    num_clients=10          # 并发数
)
```

**输出**:
- `performance_results/BENCHMARK_REPORT.md` - 完整报告
- `performance_results/charts/*.png` - 性能图表

---

### memory_leak_detection.sh

**功能**:
- ✅ Valgrind Memcheck - 内存泄漏检测
- ✅ Valgrind Massif - Heap使用分析
- ✅ Valgrind Cachegrind - 缓存性能分析
- ✅ Valgrind Helgrind - 线程安全检测

**使用方法**:

```bash
# 编译Debug版本
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 运行检测
chmod +x tools/memory_leak_detection.sh
./tools/memory_leak_detection.sh
```

**输出**:
- `performance_results/memory_check/MEMORY_CHECK_REPORT.md` - 完整报告
- `performance_results/memory_check/*_memcheck_full.log` - 详细日志
- `performance_results/memory_check/*_massif_report.txt` - Heap分析

---

### run_all_tests.sh

**功能**:
- ✅ 一键运行性能压测和内存检测
- ✅ 自动启动/停止服务器
- ✅ 检查依赖
- ✅ 交互式菜单

**使用方法**:

```bash
./tools/run_all_tests.sh

# 选择模式:
#   1) 仅性能压测 (5-10分钟)
#   2) 仅内存检测 (10-15分钟)
#   3) 完整测试 (20-30分钟)
```

---

## 📈 生成的报告示例

### 性能报告结构

```
performance_results/
├── BENCHMARK_REPORT.md          # 完整性能报告
├── charts/
│   ├── qps_comparison.png       # QPS对比图
│   ├── latency_comparison.png   # 延迟对比图
│   ├── redis_latency_distribution.png
│   └── websocket_latency_distribution.png
└── memory_check/
    ├── MEMORY_CHECK_REPORT.md   # 内存检测报告
    ├── redis_memcheck_full.log
    ├── websocket_memcheck_full.log
    ├── redis_massif_report.txt
    └── ...
```

### 报告内容

**性能报告包含**:
- 📊 QPS数据（mini_redis, WebSocket）
- 📊 延迟统计（平均、P50、P95、P99）
- 📊 可视化图表
- 📊 性能评估和优化建议

**内存报告包含**:
- 🔍 内存泄漏检测结果
- 🔍 Heap使用情况
- 🔍 线程安全检测结果
- 🔍 详细日志分析

---

## 🎯 在面试中使用

### 展示策略

1. **简历中**:
   ```
   • QPS: mini_redis 85,000+, WebSocket 40,000+
   • 延迟: 平均<1ms, P99<2ms
   • 质量: 通过Valgrind完整检测，无内存泄漏
   ```

2. **GitHub README**:
   - 添加性能章节
   - 嵌入QPS对比图
   - 链接到完整测试报告

3. **面试时**:
   - 展示图表（手机/平板打开GitHub）
   - 讲解测试方法（压测脚本设计）
   - 强调数据可信度（Valgrind验证）

---

## 🔧 常见问题

### Q: 压测时QPS很低怎么办？

A: 检查以下几点：
1. 是否使用Release编译模式
2. 是否在虚拟机中运行（性能受限）
3. 增加并发客户端数量
4. 调整线程池大小

### Q: Valgrind显示内存泄漏

A: 
1. 检查是否是第三方库的泄漏（可忽略）
2. 关注 `definitely lost` 字段
3. `still reachable` 通常不是问题（全局变量等）

### Q: Windows上如何运行内存检测？

A: 
Windows不支持Valgrind，可以使用：
1. WSL（Windows Subsystem for Linux）
2. Docker容器（Linux环境）
3. Dr. Memory（Windows替代工具）

---

## 📚 相关文档

- [性能测试快速指南](../PERFORMANCE_TESTING.md)
- [详细使用指南](../docs/性能测试与内存检测指南.md)
- [项目README](../README.md)

---

## 🤝 贡献

如果您想改进测试工具，欢迎提交PR：

1. 添加新的压测场景
2. 改进图表展示
3. 支持更多协议
4. 优化报告格式

---

**最后更新**: 2024-11-01


