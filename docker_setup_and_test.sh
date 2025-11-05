#!/bin/bash

# Docker容器内 NetBox 测试环境一键配置脚本

set -e

echo "╔══════════════════════════════════════════════════════════╗"
echo "║                                                          ║"
echo "║     NetBox Docker 测试环境配置 v1.0                     ║"
echo "║                                                          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# 检测是否在Docker容器内
if [ ! -f /.dockerenv ]; then
    echo "⚠️  警告: 似乎不在Docker容器内"
    echo "   如果确实在容器内，可以忽略此警告"
    echo ""
fi

echo "【步骤 1/5】 检查系统环境"
echo "────────────────────────────────────────────────────────"
echo "操作系统: $(cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)"
echo "CPU核心数: $(nproc)"
echo "内存: $(free -h | grep Mem | awk '{print $2}')"
echo ""

echo "【步骤 2/5】 安装系统依赖"
echo "────────────────────────────────────────────────────────"

# 更新包管理器
echo "更新包管理器..."
apt-get update > /dev/null 2>&1 || {
    echo "❌ apt-get update 失败，尝试使用已有包"
}

# 检查并安装必要工具
MISSING_PACKAGES=()

# 检查Python3
if ! command -v python3 &> /dev/null; then
    MISSING_PACKAGES+=("python3")
fi

# 检查pip3
if ! command -v pip3 &> /dev/null; then
    MISSING_PACKAGES+=("python3-pip")
fi

# 检查CMake
if ! command -v cmake &> /dev/null; then
    MISSING_PACKAGES+=("cmake")
fi

# 检查编译器
if ! command -v g++ &> /dev/null; then
    MISSING_PACKAGES+=("build-essential")
fi

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo "需要安装: ${MISSING_PACKAGES[*]}"
    apt-get install -y ${MISSING_PACKAGES[@]} || {
        echo "❌ 安装失败，请手动运行:"
        echo "   apt-get install -y ${MISSING_PACKAGES[*]}"
        exit 1
    }
    echo "✅ 系统依赖安装完成"
else
    echo "✅ 系统依赖已安装"
fi
echo ""

echo "【步骤 3/5】 安装Python测试依赖"
echo "────────────────────────────────────────────────────────"

# 检查Python模块
MISSING_MODULES=()

python3 -c "import redis" 2>/dev/null || MISSING_MODULES+=("redis")
python3 -c "import websockets" 2>/dev/null || MISSING_MODULES+=("websockets")
python3 -c "import matplotlib" 2>/dev/null || MISSING_MODULES+=("matplotlib")
python3 -c "import numpy" 2>/dev/null || MISSING_MODULES+=("numpy")

if [ ${#MISSING_MODULES[@]} -gt 0 ]; then
    echo "需要安装Python模块: ${MISSING_MODULES[*]}"
    
    # 使用清华源加速（如果网络慢）
    pip3 install redis websockets matplotlib numpy -i https://pypi.tuna.tsinghua.edu.cn/simple || {
        # 如果清华源失败，使用默认源
        echo "尝试使用默认pip源..."
        pip3 install redis websockets matplotlib numpy
    }
    
    echo "✅ Python依赖安装完成"
else
    echo "✅ Python依赖已安装"
fi

# 验证安装
echo ""
echo "验证安装:"
echo "  Python: $(python3 --version)"
echo "  CMake: $(cmake --version | head -1)"
echo "  GCC: $(g++ --version | head -1)"
echo "  Redis模块: $(python3 -c 'import redis; print(redis.__version__)')"
echo "  WebSockets模块: $(python3 -c 'import websockets; print(websockets.__version__)')"
echo ""

echo "【步骤 4/5】 编译项目"
echo "────────────────────────────────────────────────────────"

# 检查是否已编译
if [ -f "build/bin/netbox_server" ]; then
    echo "检测到已有编译文件"
    read -p "是否重新编译? [y/N]: " RECOMPILE
    if [[ ! $RECOMPILE =~ ^[Yy]$ ]]; then
        echo "✅ 跳过编译"
        echo ""
        SKIP_COMPILE=1
    fi
fi

if [ -z "$SKIP_COMPILE" ]; then
    echo "开始编译 (Release模式)..."
    
    # 清理旧的构建
    rm -rf build/CMakeCache.txt
    
    # 配置CMake
    cmake -B build -DCMAKE_BUILD_TYPE=Release || {
        echo "❌ CMake配置失败"
        exit 1
    }
    
    # 编译（使用所有CPU核心）
    cmake --build build -j$(nproc) || {
        echo "❌ 编译失败"
        exit 1
    }
    
    echo "✅ 编译完成"
    
    # 检查可执行文件
    if [ -f "build/bin/netbox_server" ]; then
        ls -lh build/bin/netbox_server
    else
        echo "❌ 可执行文件未生成"
        exit 1
    fi
fi
echo ""

echo "【步骤 5/5】 准备测试环境"
echo "────────────────────────────────────────────────────────"

# 创建结果目录
mkdir -p performance_results/charts
echo "✅ 结果目录已创建"

# 设置matplotlib后端（Docker容器无GUI）
export MPLBACKEND=Agg
echo "✅ matplotlib配置为非GUI模式"
echo ""

echo "╔══════════════════════════════════════════════════════════╗"
echo "║                                                          ║"
echo "║              配置完成！准备运行测试                      ║"
echo "║                                                          ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# 提供测试选项
echo "请选择测试模式:"
echo "  1) 自动运行性能测试 (推荐)"
echo "  2) 手动启动服务器"
echo "  3) 退出，稍后手动测试"
echo ""
read -p "请选择 [1-3]: " CHOICE

case $CHOICE in
    1)
        echo ""
        echo "═══════════════════════════════════════════════════════════"
        echo "开始自动性能测试"
        echo "═══════════════════════════════════════════════════════════"
        echo ""
        
        # 启动Redis服务器
        echo "启动 mini_redis 服务器..."
        ./build/bin/netbox_server config/config-redis.yaml > /dev/null 2>&1 &
        REDIS_PID=$!
        sleep 3
        
        if ps -p $REDIS_PID > /dev/null; then
            echo "✅ mini_redis 已启动 (PID: $REDIS_PID)"
        else
            echo "❌ mini_redis 启动失败"
            exit 1
        fi
        
        # 启动WebSocket服务器
        echo "启动 WebSocket 服务器..."
        ./build/bin/netbox_server config/config-websocket.yaml > /dev/null 2>&1 &
        WS_PID=$!
        sleep 3
        
        if ps -p $WS_PID > /dev/null; then
            echo "✅ WebSocket 已启动 (PID: $WS_PID)"
        else
            echo "❌ WebSocket 启动失败"
            kill $REDIS_PID 2>/dev/null
            exit 1
        fi
        
        echo ""
        echo "服务器已启动，等待初始化..."
        sleep 2
        echo ""
        
        # 运行压测
        echo "═══════════════════════════════════════════════════════════"
        echo "开始性能压测"
        echo "═══════════════════════════════════════════════════════════"
        echo ""
        
        python3 tools/performance_benchmark.py || {
            echo ""
            echo "❌ 压测失败"
            kill $REDIS_PID $WS_PID 2>/dev/null
            exit 1
        }
        
        # 停止服务器
        echo ""
        echo "停止服务器..."
        kill $REDIS_PID $WS_PID 2>/dev/null
        wait $REDIS_PID $WS_PID 2>/dev/null || true
        echo "✅ 服务器已停止"
        
        echo ""
        echo "╔══════════════════════════════════════════════════════════╗"
        echo "║                                                          ║"
        echo "║                  测试完成！                             ║"
        echo "║                                                          ║"
        echo "╚══════════════════════════════════════════════════════════╝"
        echo ""
        echo "📊 查看结果:"
        echo "   cat performance_results/BENCHMARK_REPORT.md"
        echo ""
        echo "📈 查看图表:"
        echo "   ls -lh performance_results/charts/"
        echo ""
        echo "💡 将结果复制到宿主机:"
        echo "   在宿主机运行: docker cp <container_name>:/workspace/performance_results ./"
        echo ""
        ;;
        
    2)
        echo ""
        echo "═══════════════════════════════════════════════════════════"
        echo "手动启动服务器"
        echo "═══════════════════════════════════════════════════════════"
        echo ""
        echo "在不同终端运行以下命令:"
        echo ""
        echo "终端1 - Redis服务器:"
        echo "  ./build/bin/netbox_server config/config-redis.yaml"
        echo ""
        echo "终端2 - WebSocket服务器:"
        echo "  ./build/bin/netbox_server config/config-websocket.yaml"
        echo ""
        echo "终端3 - 运行压测:"
        echo "  python3 tools/performance_benchmark.py"
        echo ""
        ;;
        
    3)
        echo ""
        echo "配置已完成！"
        echo ""
        echo "稍后运行测试:"
        echo "  ./build/bin/netbox_server config/config-redis.yaml &"
        echo "  ./build/bin/netbox_server config/config-websocket.yaml &"
        echo "  python3 tools/performance_benchmark.py"
        echo ""
        ;;
        
    *)
        echo "无效选择"
        exit 1
        ;;
esac




