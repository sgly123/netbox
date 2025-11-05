#!/bin/bash

# NetBox 一键完整测试脚本
# 包含性能压测和内存检测

set -e

echo "╔══════════════════════════════════════════════════════════╗"
echo "║                                                          ║"
echo "║         NetBox 完整测试套件 v1.0                        ║"
echo "║                                                          ║"
echo "║  测试内容:                                               ║"
echo "║    1. 性能压测 (QPS, 延迟, 并发)                        ║"
echo "║    2. 内存检测 (泄漏, 越界, 线程安全)                   ║"
echo "║    3. 生成图表和报告                                     ║"
echo "║                                                          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

cd "$(dirname "$0")/.."

# 检查依赖
echo "检查依赖..."
MISSING_DEPS=()

if ! command -v python3 &> /dev/null; then
    MISSING_DEPS+=("python3")
fi

if ! python3 -c "import redis" 2>/dev/null; then
    MISSING_DEPS+=("python3-redis")
fi

if ! python3 -c "import websockets" 2>/dev/null; then
    MISSING_DEPS+=("python3-websockets")
fi

if ! python3 -c "import matplotlib" 2>/dev/null; then
    MISSING_DEPS+=("python3-matplotlib")
fi

if ! python3 -c "import numpy" 2>/dev/null; then
    MISSING_DEPS+=("python3-numpy")
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "❌ 缺少依赖: ${MISSING_DEPS[*]}"
    echo ""
    echo "安装依赖:"
    echo "  pip3 install redis websockets matplotlib numpy"
    exit 1
fi

echo "✅ 所有依赖已安装"
echo ""

# 步骤选择
echo "请选择测试模式:"
echo "  1) 仅性能压测 (快速, 5-10分钟)"
echo "  2) 仅内存检测 (需要valgrind, 10-15分钟)"
echo "  3) 完整测试 (性能+内存, 20-30分钟)"
echo ""
read -p "请选择 [1-3]: " CHOICE

case $CHOICE in
    1)
        echo ""
        echo "═══════════════════════════════════════════════════════════"
        echo "启动性能压测..."
        echo "═══════════════════════════════════════════════════════════"
        
        # 1. 启动Redis服务器
        echo "启动 mini_redis 服务器..."
        ./build/bin/netbox_server config/config-redis.yaml > /dev/null 2>&1 &
        REDIS_PID=$!
        sleep 3
        echo "✅ mini_redis 已启动 (PID: $REDIS_PID)"
        
        # 2. 启动WebSocket服务器
        echo "启动 WebSocket 服务器..."
        ./build/bin/netbox_server config/config-websocket.yaml > /dev/null 2>&1 &
        WS_PID=$!
        sleep 3
        echo "✅ WebSocket 已启动 (PID: $WS_PID)"
        
        # 3. 运行压测
        echo ""
        python3 tools/performance_benchmark.py
        
        # 4. 停止服务器
        echo "停止服务器..."
        kill $REDIS_PID $WS_PID 2>/dev/null || true
        wait $REDIS_PID $WS_PID 2>/dev/null || true
        echo "✅ 服务器已停止"
        ;;
        
    2)
        echo ""
        echo "═══════════════════════════════════════════════════════════"
        echo "启动内存检测..."
        echo "═══════════════════════════════════════════════════════════"
        
        chmod +x tools/memory_leak_detection.sh
        ./tools/memory_leak_detection.sh
        ;;
        
    3)
        echo ""
        echo "═══════════════════════════════════════════════════════════"
        echo "启动完整测试..."
        echo "═══════════════════════════════════════════════════════════"
        
        # 1. 性能压测
        echo ""
        echo "【步骤 1/2】 性能压测"
        echo "─────────────────────────────────────────────────────────"
        
        echo "启动 mini_redis 服务器..."
        ./build/bin/netbox_server config/config-redis.yaml > /dev/null 2>&1 &
        REDIS_PID=$!
        sleep 3
        
        echo "启动 WebSocket 服务器..."
        ./build/bin/netbox_server config/config-websocket.yaml > /dev/null 2>&1 &
        WS_PID=$!
        sleep 3
        
        python3 tools/performance_benchmark.py
        
        kill $REDIS_PID $WS_PID 2>/dev/null || true
        wait $REDIS_PID $WS_PID 2>/dev/null || true
        
        sleep 2
        
        # 2. 内存检测
        echo ""
        echo "【步骤 2/2】 内存检测"
        echo "─────────────────────────────────────────────────────────"
        
        chmod +x tools/memory_leak_detection.sh
        ./tools/memory_leak_detection.sh
        ;;
        
    *)
        echo "❌ 无效选择"
        exit 1
        ;;
esac

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                                                          ║"
echo "║                  测试完成！                             ║"
echo "║                                                          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "📊 查看结果:"
echo "   - 性能报告: performance_results/BENCHMARK_REPORT.md"
echo "   - 内存报告: performance_results/memory_check/MEMORY_CHECK_REPORT.md"
echo "   - 性能图表: performance_results/charts/"
echo ""
echo "💡 提示: 将这些报告和图表添加到GitHub，增强项目可信度！"
echo ""




