cmake_minimum_required(VERSION 3.10)
project(NetBox VERSION 1.0.0 LANGUAGES CXX)

# 项目描述
set(PROJECT_DESCRIPTION "NetBox - 企业级高性能网络框架")
set(PROJECT_HOMEPAGE_URL "https://github.com/sgly123/NetBox")

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 跨平台编译选项设置
if(WIN32)
    # Windows编译选项
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /RTC1 /DNETBOX_DEBUG")

    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /EHsc")

    # Windows特定定义
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7+
    add_definitions(-DNOMINMAX)  # 避免min/max宏冲突

    # Windows链接库
    set(PLATFORM_LIBS ws2_32 mswsock kernel32 user32)

elseif(APPLE)
    # macOS编译选项
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DNETBOX_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

    # macOS特定选项
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

    # macOS链接库
    set(PLATFORM_LIBS pthread)

elseif(UNIX)
    # Linux/Unix编译选项
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DNETBOX_DEBUG")

    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

    # Linux特定选项
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GNU_SOURCE")
    endif()

    # Linux/Unix链接库
    set(PLATFORM_LIBS pthread)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND PLATFORM_LIBS rt)  # Linux需要librt
    endif()

else()
    # 其他平台默认设置
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
    set(PLATFORM_LIBS)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 添加头文件目录
include_directories(
    ${CMAKE_SOURCE_DIR}/NetFramework/include
    ${CMAKE_SOURCE_DIR}/Protocol/include
    ${CMAKE_SOURCE_DIR}/app/include
)

# 查找依赖库
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(OpenSSL REQUIRED)

# 主程序可执行文件
add_executable(NetBox
    main.cpp
    # 基础库
    NetFramework/src/base/Logger.cpp
    NetFramework/src/base/AsyncConsoleLogger.cpp
    NetFramework/src/base/AsyncLogger.cpp
    NetFramework/src/base/ThreadPool.cpp
    NetFramework/src/base/DoubleLockThreadPool.cpp
    NetFramework/src/base/SpdlogLogger.cpp
    # 工具库
    NetFramework/src/util/ConfigReader.cpp
    NetFramework/src/util/EnhancedConfigReader.cpp
    # IO库 - 跨平台支持
    NetFramework/src/IO/SelectMultiplexer.cpp  # 所有平台都支持
    # 平台特定的IO多路复用器
    $<$<PLATFORM_ID:Linux>:NetFramework/src/IO/EpollMultiplexer.cpp>
    $<$<PLATFORM_ID:Linux>:NetFramework/src/IO/PollMultiplexer.cpp>
    # Windows平台源文件将在后续添加
    # macOS平台源文件将在后续添加
    # 服务器库
    NetFramework/src/server/TcpServer.cpp
    NetFramework/src/server/UdpServer.cpp
        # 协议库
    Protocol/src/ProtocolBase.cpp
    Protocol/src/ProtocolRouter.cpp
    Protocol/src/SimpleHeaderProtocol.cpp
    Protocol/src/HttpProtocol.cpp
    Protocol/src/RedisProtocol.cpp
    Protocol/src/PureRedisProtocol.cpp
    Protocol/src/WebSocketProtocol.cpp
    Protocol/src/base64.cpp
    Protocol/src/ProtocolFactory.cpp
    # 应用层
    app/src/server.cpp
    app/src/ApplicationServer.cpp
    app/src/RedisApplicationServer.cpp
    app/src/DirectRedisServer.cpp
    app/src/UdpEchoServer.cpp
    app/src/WebSocketServer.cpp
    # 插件化框架
    NetFramework/src/app/ApplicationRegistry.cpp
    # 应用插件
    plugins/echo/EchoServerPlugin.cpp
    plugins/redis/RedisApplicationPlugin.cpp
    plugins/redis/DirectRedisPlugin.cpp
    plugins/udp/UdpEchoPlugin.cpp
    plugins/websocket/WebSocketPlugin.cpp
)

# 链接库 - 跨平台支持
target_link_libraries(NetBox PRIVATE
    Threads::Threads
    spdlog::spdlog
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PLATFORM_LIBS}  # 平台特定库
)

# 协议测试程序 (已删除，因为缺少main函数)
# add_executable(protocol_test
#     Protocol/src/ProtocolBase.cpp
#     Protocol/src/ProtocolRouter.cpp
#     Protocol/src/SimpleHeaderProtocol.cpp
#     Protocol/src/ProtocolFactory.cpp
#     NetFramework/src/base/Logger.cpp
#     NetFramework/src/base/AsyncConsoleLogger.cpp
#     NetFramework/src/base/AsyncLogger.cpp
#     app/src/protocol_test.cpp
# )

# HTTP协议测试程序 (已删除，因为缺少main函数)
# add_executable(http_test
#     Protocol/src/ProtocolBase.cpp
#     Protocol/src/HttpProtocol.cpp
#     NetFramework/src/base/Logger.cpp
#     NetFramework/src/base/AsyncConsoleLogger.cpp
#     NetFramework/src/base/AsyncLogger.cpp
#     app/src/http_test.cpp
# )

# ================================
# 客户端程序
# ================================

# Echo客户端 (修复版本)
add_executable(echo_client_fixed
    Protocol/src/SimpleHeaderProtocol.cpp
    Protocol/src/ProtocolFactory.cpp
    NetFramework/src/base/Logger.cpp
    NetFramework/src/base/AsyncConsoleLogger.cpp
    NetFramework/src/base/AsyncLogger.cpp
    client/echo_client_fixed.cpp
)
target_link_libraries(echo_client_fixed PRIVATE Threads::Threads)

# 智能Redis客户端
add_executable(smart_netbox_redis_client
    client/smart_netbox_redis_client.cpp
)
target_link_libraries(smart_netbox_redis_client PRIVATE Threads::Threads)

# ================================
# 工具程序 (可选编译)
# ================================

# 编码测试程序 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/test_encoding.cpp")
    add_executable(test_encoding test_encoding.cpp)
    target_link_libraries(test_encoding PRIVATE Threads::Threads)
endif()

# 调试编码程序 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/debug_encoding.cpp")
    add_executable(debug_encoding debug_encoding.cpp)
    target_link_libraries(debug_encoding PRIVATE Threads::Threads)
endif()

# 简单测试程序 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/simple_test.cpp")
    add_executable(simple_test simple_test.cpp)
    target_link_libraries(simple_test PRIVATE Threads::Threads)
endif()

# EchoRedis客户端程序 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/echo_redis_client.cpp")
    add_executable(echo_redis_client echo_redis_client.cpp)
    target_link_libraries(echo_redis_client PRIVATE Threads::Threads)
endif()

# Simple Redis客户端 (如果存在)
if(EXISTS "${CMAKE_SOURCE_DIR}/simple_redis_client.cpp")
    add_executable(simple_redis_client simple_redis_client.cpp)
    target_link_libraries(simple_redis_client PRIVATE Threads::Threads)
endif()

# ================================
# 安装配置
# ================================

# 安装主程序
install(TARGETS NetBox
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# 安装客户端程序
install(TARGETS echo_client_fixed smart_netbox_redis_client
    RUNTIME DESTINATION bin
    COMPONENT Clients
)

# 安装配置文件
install(DIRECTORY config/
    DESTINATION etc/netbox
    COMPONENT Configuration
)

# 安装文档
install(DIRECTORY docs/
    DESTINATION share/doc/netbox
    COMPONENT Documentation
)

# ================================
# 测试配置
# ================================

# 启用测试
option(BUILD_TESTS "Build unit tests" OFF)

# WebSocket测试程序
add_executable(WebSocketTest
    app/src/WebSocketTest.cpp
    app/src/WebSocketServer.cpp
    app/src/ApplicationServer.cpp
    Protocol/src/WebSocketProtocol.cpp
    Protocol/src/base64.cpp
    Protocol/src/ProtocolBase.cpp
    Protocol/src/ProtocolRouter.cpp
    Protocol/src/ProtocolFactory.cpp
    Protocol/src/PureRedisProtocol.cpp
    NetFramework/src/server/TcpServer.cpp
    NetFramework/src/server/server.cpp
    NetFramework/src/base/Logger.cpp
    NetFramework/src/base/AsyncConsoleLogger.cpp
    NetFramework/src/base/AsyncLogger.cpp
    NetFramework/src/base/ThreadPool.cpp
    NetFramework/src/base/DoubleLockThreadPool.cpp
    NetFramework/src/base/SpdlogLogger.cpp
    NetFramework/src/util/ConfigReader.cpp
    NetFramework/src/util/EnhancedConfigReader.cpp
    NetFramework/src/IO/SelectMultiplexer.cpp
    $<$<PLATFORM_ID:Linux>:NetFramework/src/IO/EpollMultiplexer.cpp>
    $<$<PLATFORM_ID:Linux>:NetFramework/src/IO/PollMultiplexer.cpp>
)
target_link_libraries(WebSocketTest PRIVATE
    Threads::Threads
    spdlog::spdlog
    fmt::fmt
    OpenSSL::SSL
    OpenSSL::Crypto
    ${PLATFORM_LIBS}
)

if(BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests")
    enable_testing()
    add_subdirectory(tests)
endif()

