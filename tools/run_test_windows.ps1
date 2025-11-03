# NetBox Windows宿主机性能测试脚本
# 服务器在Docker容器内运行，测试在Windows运行

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "       NetBox 性能测试 (Windows宿主机 + Docker容器)" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# 步骤1: 检查Python
Write-Host "[1/5] 检查Python环境..." -ForegroundColor Yellow
try {
    $pythonVersion = python --version 2>&1
    Write-Host "  ✓ Python已安装: $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "  ✗ Python未安装，请先安装Python 3.8+" -ForegroundColor Red
    exit 1
}
Write-Host ""

# 步骤2: 检查和安装Python依赖
Write-Host "[2/5] 检查Python依赖..." -ForegroundColor Yellow

$modules = @("redis", "websockets", "matplotlib", "numpy")
$missing = @()

foreach ($module in $modules) {
    python -c "import $module" 2>$null
    if ($LASTEXITCODE -ne 0) {
        $missing += $module
        Write-Host "  ✗ $module 未安装" -ForegroundColor Yellow
    } else {
        Write-Host "  ✓ $module 已安装" -ForegroundColor Green
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "  正在安装缺失的依赖: $($missing -join ', ')..." -ForegroundColor Yellow
    
    # 尝试使用清华源（中国用户更快）
    pip install $missing -i https://pypi.tuna.tsinghua.edu.cn/simple --quiet
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  清华源失败，尝试默认源..." -ForegroundColor Yellow
        pip install $missing --quiet
    }
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  ✓ 依赖安装成功" -ForegroundColor Green
    } else {
        Write-Host "  ✗ 依赖安装失败，请手动运行:" -ForegroundColor Red
        Write-Host "    pip install redis websockets matplotlib numpy" -ForegroundColor Red
        exit 1
    }
}
Write-Host ""

# 步骤3: 测试容器连接
Write-Host "[3/5] 测试Docker容器连接..." -ForegroundColor Yellow

# 测试Redis连接
Write-Host "  测试Redis连接 (localhost:6380)..." -ForegroundColor Gray
python -c "import redis; r = redis.Redis(host='localhost', port=6380, socket_connect_timeout=3); r.ping(); print('OK')" 2>$null | Out-Null

if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ Redis连接成功" -ForegroundColor Green
} else {
    Write-Host "  ✗ Redis连接失败" -ForegroundColor Red
    Write-Host ""
    Write-Host "  请确保:" -ForegroundColor Yellow
    Write-Host "    1. Docker容器正在运行: docker ps" -ForegroundColor Yellow
    Write-Host "    2. 端口已映射: -p 6380:6379" -ForegroundColor Yellow
    Write-Host "    3. 容器内服务器已启动:" -ForegroundColor Yellow
    Write-Host "       docker exec -it <容器名> bash" -ForegroundColor Yellow
    Write-Host "       ./build/bin/netbox_server config/config-redis.yaml &" -ForegroundColor Yellow
    Write-Host ""
    
    $continue = Read-Host "  是否继续测试WebSocket? (y/N)"
    if ($continue -ne "y" -and $continue -ne "Y") {
        exit 1
    }
}

# 测试WebSocket连接
Write-Host "  测试WebSocket连接 (localhost:8002)..." -ForegroundColor Gray
$wsTest = @"
import asyncio
import websockets
import sys

async def test():
    try:
        async with websockets.connect('ws://localhost:8002', timeout=3):
            return True
    except:
        return False

result = asyncio.run(test())
sys.exit(0 if result else 1)
"@

$wsTest | python 2>$null

if ($LASTEXITCODE -eq 0) {
    Write-Host "  ✓ WebSocket连接成功" -ForegroundColor Green
} else {
    Write-Host "  ✗ WebSocket连接失败" -ForegroundColor Red
    Write-Host ""
    Write-Host "  请确保容器内WebSocket服务器已启动:" -ForegroundColor Yellow
    Write-Host "    docker exec -it <容器名> bash" -ForegroundColor Yellow
    Write-Host "    ./build/bin/netbox_server config/config-websocket.yaml &" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}
Write-Host ""

# 步骤4: 创建结果目录
Write-Host "[4/5] 准备测试环境..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path "performance_results\charts" | Out-Null
Write-Host "  ✓ 结果目录已创建" -ForegroundColor Green
Write-Host ""

# 步骤5: 运行性能测试
Write-Host "[5/5] 开始性能压测..." -ForegroundColor Yellow
Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "                    性能压测进行中" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

python tools\performance_benchmark.py

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host "                      测试完成！" -ForegroundColor Green
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "📊 查看结果:" -ForegroundColor Cyan
    Write-Host "   type performance_results\BENCHMARK_REPORT.md" -ForegroundColor White
    Write-Host ""
    Write-Host "📈 查看图表:" -ForegroundColor Cyan
    Write-Host "   explorer performance_results\charts" -ForegroundColor White
    Write-Host ""
    Write-Host "🚀 提交到GitHub:" -ForegroundColor Cyan
    Write-Host "   git add performance_results/" -ForegroundColor White
    Write-Host "   git commit -m 'Add performance benchmark results'" -ForegroundColor White
    Write-Host "   git push" -ForegroundColor White
    Write-Host ""
    
    # 询问是否打开结果目录
    $openFolder = Read-Host "是否打开结果目录? (Y/n)"
    if ($openFolder -ne "n" -and $openFolder -ne "N") {
        explorer performance_results\charts
    }
    
} else {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host "                      测试失败" -ForegroundColor Red
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "请检查:" -ForegroundColor Yellow
    Write-Host "  1. 容器内服务器是否正在运行" -ForegroundColor Yellow
    Write-Host "  2. 端口映射是否正确 (6380:6379, 8002:8001)" -ForegroundColor Yellow
    Write-Host "  3. 查看详细日志进行调试" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}

