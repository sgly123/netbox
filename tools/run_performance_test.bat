@echo off
REM NetBox Performance Testing - Windows Version

echo ================================================================
echo                NetBox Performance Testing Tool
echo ================================================================
echo.

REM Check Python
where python >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Python not found! Please install Python 3.8+
    pause
    exit /b 1
)

echo [INFO] Python found: 
python --version
echo.

REM Check dependencies
echo [INFO] Checking Python dependencies...
python -c "import redis" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] redis module not found. Installing...
    pip install redis websockets matplotlib numpy
)

python -c "import websockets" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] websockets module not found. Installing...
    pip install redis websockets matplotlib numpy
)

python -c "import matplotlib" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [WARN] matplotlib module not found. Installing...
    pip install redis websockets matplotlib numpy
)

echo [OK] All dependencies installed
echo.

REM Check if server is built
if not exist "build\bin\Release\netbox_server.exe" (
    if not exist "build\bin\netbox_server.exe" (
        echo [ERROR] Server executable not found!
        echo Please build the project first:
        echo   cmake -B build -DCMAKE_BUILD_TYPE=Release
        echo   cmake --build build --config Release
        pause
        exit /b 1
    )
)

echo ================================================================
echo                Starting Performance Test
echo ================================================================
echo.

REM Start Redis server
echo [INFO] Starting mini_redis server...
start "mini_redis" build\bin\Release\netbox_server.exe config\config-redis.yaml
if %ERRORLEVEL% NEQ 0 (
    start "mini_redis" build\bin\netbox_server.exe config\config-redis.yaml
)
timeout /t 3 >nul

REM Start WebSocket server  
echo [INFO] Starting WebSocket server...
start "WebSocket" build\bin\Release\netbox_server.exe config\config-websocket.yaml
if %ERRORLEVEL% NEQ 0 (
    start "WebSocket" build\bin\netbox_server.exe config\config-websocket.yaml
)
timeout /t 3 >nul

echo [INFO] Servers started. Waiting for initialization...
timeout /t 5 >nul
echo.

REM Run benchmark
echo ================================================================
echo                Running Benchmark Tests
echo ================================================================
echo.

python tools\performance_benchmark.py

echo.
echo ================================================================
echo                Test Complete!
echo ================================================================
echo.

REM Stop servers
echo [INFO] Stopping servers...
taskkill /FI "WindowTitle eq mini_redis*" /T /F >nul 2>nul
taskkill /FI "WindowTitle eq WebSocket*" /T /F >nul 2>nul
timeout /t 2 >nul

echo.
echo [OK] Results saved to:
echo   - performance_results\BENCHMARK_REPORT.md
echo   - performance_results\charts\
echo.

pause




