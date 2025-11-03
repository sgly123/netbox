# NetBox Windows Performance Test Script
# Server runs in Docker, test runs on Windows

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "       NetBox Performance Test (Windows + Docker)" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Check Python
Write-Host "[1/5] Checking Python..." -ForegroundColor Yellow
try {
    $pythonVersion = python --version 2>&1
    Write-Host "  OK Python installed: $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "  ERROR Python not found, install Python 3.8+" -ForegroundColor Red
    exit 1
}
Write-Host ""

# Step 2: Check Python dependencies
Write-Host "[2/5] Checking Python dependencies..." -ForegroundColor Yellow

$modules = @("redis", "websockets", "matplotlib", "numpy")
$missing = @()

foreach ($module in $modules) {
    python -c "import $module" 2>$null
    if ($LASTEXITCODE -ne 0) {
        $missing += $module
        Write-Host "  Missing $module" -ForegroundColor Yellow
    } else {
        Write-Host "  OK $module installed" -ForegroundColor Green
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "  Installing missing dependencies: $($missing -join ', ')..." -ForegroundColor Yellow
    
    pip install $missing -i https://pypi.tuna.tsinghua.edu.cn/simple --quiet
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  Try default source..." -ForegroundColor Yellow
        pip install $missing --quiet
    }
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK Dependencies installed" -ForegroundColor Green
    } else {
        Write-Host "  ERROR Installation failed, run manually:" -ForegroundColor Red
        Write-Host "    pip install redis websockets matplotlib numpy" -ForegroundColor Red
        exit 1
    }
}
Write-Host ""

# Step 3: Test Docker connection
Write-Host "[3/5] Testing Docker container connection..." -ForegroundColor Yellow

# Test Redis
Write-Host "  Testing Redis (localhost:6380)..." -ForegroundColor Gray
python -c "import redis; r = redis.Redis(host='localhost', port=6380, socket_connect_timeout=3); r.ping(); print('OK')" 2>$null | Out-Null

if ($LASTEXITCODE -eq 0) {
    Write-Host "  OK Redis connected" -ForegroundColor Green
} else {
    Write-Host "  ERROR Redis connection failed" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Please check:" -ForegroundColor Yellow
    Write-Host "    1. Docker container is running: docker ps" -ForegroundColor Yellow
    Write-Host "    2. Port is mapped: -p 6380:6379" -ForegroundColor Yellow
    Write-Host "    3. Server is running in container" -ForegroundColor Yellow
    Write-Host ""
    
    $continue = Read-Host "  Continue to test WebSocket? (y/N)"
    if ($continue -ne "y" -and $continue -ne "Y") {
        exit 1
    }
}

# Test WebSocket
Write-Host "  Testing WebSocket (localhost:8002)..." -ForegroundColor Gray
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
    Write-Host "  OK WebSocket connected" -ForegroundColor Green
} else {
    Write-Host "  ERROR WebSocket connection failed" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Make sure WebSocket server is running in container" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}
Write-Host ""

# Step 4: Prepare test environment
Write-Host "[4/5] Preparing test environment..." -ForegroundColor Yellow
New-Item -ItemType Directory -Force -Path "performance_results\charts" | Out-Null
Write-Host "  OK Result directory created" -ForegroundColor Green
Write-Host ""

# Step 5: Run performance test
Write-Host "[5/5] Starting performance test..." -ForegroundColor Yellow
Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "                    Running Tests" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

python tools\performance_benchmark.py

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host "                      Test Completed!" -ForegroundColor Green
    Write-Host "================================================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "View results:" -ForegroundColor Cyan
    Write-Host "   type performance_results\BENCHMARK_REPORT.md" -ForegroundColor White
    Write-Host ""
    Write-Host "View charts:" -ForegroundColor Cyan
    Write-Host "   explorer performance_results\charts" -ForegroundColor White
    Write-Host ""
    Write-Host "Push to GitHub:" -ForegroundColor Cyan
    Write-Host "   git add performance_results/" -ForegroundColor White
    Write-Host "   git commit -m 'Add performance benchmark results'" -ForegroundColor White
    Write-Host "   git push" -ForegroundColor White
    Write-Host ""
    
    $openFolder = Read-Host "Open results folder? (Y/n)"
    if ($openFolder -ne "n" -and $openFolder -ne "N") {
        explorer performance_results\charts
    }
    
} else {
    Write-Host ""
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host "                      Test Failed" -ForegroundColor Red
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please check:" -ForegroundColor Yellow
    Write-Host "  1. Server is running in container" -ForegroundColor Yellow
    Write-Host "  2. Port mapping is correct (6380:6379, 8002:8001)" -ForegroundColor Yellow
    Write-Host "  3. Check logs for details" -ForegroundColor Yellow
    Write-Host ""
    exit 1
}


