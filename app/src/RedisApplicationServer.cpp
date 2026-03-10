#include "RedisApplicationServer.h"
#include "base/Logger.h"
#include "PureRedisProtocol.h"
#include <sstream>
#include <algorithm>
#include <sys/socket.h>
#include <iomanip>

RedisApplicationServer::RedisApplicationServer(const std::string& ip, int port,
                                             IOMultiplexer::IOType io_type, IThreadPool* pool)
    : ApplicationServer(ip, port, io_type, pool), m_currentClientFd(-1) {
    Logger::info("RedisApplicationServer 初始化完成");
    Logger::info("支持命令: PING, SET, GET, DEL, KEYS, LPUSH, LPOP, LRANGE, HSET, HGET, HKEYS");

    // 启用心跳检测但重写发送逻辑：保持连接监控，但不发送心跳包数据
    setHeartbeatEnabled(true);
    Logger::info("Redis应用已启用智能心跳：保持连接监控，但不发送心跳包数据，确保RESP协议纯净");
}

void RedisApplicationServer::initializeProtocolRouter() {
    Logger::info("开始初始化Redis协议路由器");

    // 使用纯RESP协议的PureRedisProtocol
    auto redisProto = std::make_shared<PureRedisProtocol>();
    Logger::info("PureRedisProtocol对象创建完成");

    // 设置PureRedisProtocol的回调，用于发送响应
    Logger::info("设置PureRedisProtocol回调函数");
    redisProto->setPacketCallback([this](const std::vector<char>& packet) {
        Logger::info("PureRedisProtocol回调被调用，响应长度: " + std::to_string(packet.size()));
        this->onPureRedisResponse(packet);
    });
    // 协议层错误回调
    redisProto->setErrorCallback([](const std::string& error) {
        Logger::error("Pure Redis协议错误: " + error);
    });
    Logger::info("PureRedisProtocol配置完成");

    // 设置流量控制
    redisProto->setFlowControl(4096, 4096);  // Redis需要更大的缓冲区

    // 注册Pure Redis协议到分发器
    m_router->registerProtocol(redisProto->getProtocolId(), redisProto);
    Logger::info("注册PureRedisProtocol，ID: " + std::to_string(redisProto->getProtocolId()));

    Logger::info("Pure Redis协议路由器初始化完成");
}

std::string RedisApplicationServer::handleHttpRequest(const std::string& request, int clientFd) {
    (void)request;  // 避免未使用参数警告
    (void)clientFd; // 避免未使用参数警告
    // Redis不处理HTTP请求
    return "HTTP/1.1 400 Bad Request\r\n\r\nRedis server does not support HTTP";
}

std::string RedisApplicationServer::handleBusinessLogic([[maybe_unused]] const std::string& command, const std::vector<std::string>& args) {
    // 这个方法在当前架构中不会被直接调用
    return executeRedisCommand(args);
}

bool RedisApplicationServer::parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) {
    // 解析Redis命令路径
    args = parseRedisCommand(path);
    if (!args.empty()) {
        command = args[0];
        return true;
    }
    return false;
}

void RedisApplicationServer::onDataReceived(int clientFd, const char* data, size_t len) {
    Logger::info("RedisApplicationServer 收到客户端[" + std::to_string(clientFd) + "]数据，长度: " + std::to_string(len));

    // 移除魔数检测 - Redis协议不需要自定义魔数
    // Redis协议使用纯RESP格式，不包含自定义魔数

    // 记录当前处理的客户端fd
    m_currentClientFd = clientFd;

    // 调用基类方法处理协议分发（最终会进入 PureRedisProtocol）
    ApplicationServer::onDataReceived(clientFd, data, len);

    // 清除当前客户端fd
    m_currentClientFd = -1;
}

void RedisApplicationServer::sendHeartbeat(int client_fd) {
    // Redis服务器不向客户端发送心跳包，避免污染RESP协议流
    // 保持连接活跃检测，但不发送实际的心跳包数据
    Logger::debug("Redis服务器跳过向客户端[" + std::to_string(client_fd) + "]发送心跳包，保持RESP协议纯净");
}




void RedisApplicationServer::onPacketReceived(const std::vector<char>& packet) {
    Logger::info("RedisApplicationServer::onPacketReceived 被调用！");

    // RedisProtocol已经处理了命令并生成了RESP响应
    std::string respResponse(packet.begin(), packet.end());

    Logger::info("收到RedisProtocol的RESP响应: " + respResponse.substr(0, 20) + "...");

    // 直接发送RESP响应，不需要再次处理
    sendRawRedisResponse(respResponse);
}

std::vector<std::string> RedisApplicationServer::parseRedisCommand(const std::string& command) {
    std::vector<std::string> args;
    std::istringstream iss(command);
    std::string arg;
    
    // 简单的空格分割解析
    while (iss >> arg) {
        // 移除引号（如果有）
        if (arg.length() >= 2 && arg.front() == '"' && arg.back() == '"') {
            arg = arg.substr(1, arg.length() - 2);
        }
        args.push_back(arg);
    }
    
    return args;
}

std::string RedisApplicationServer::executeRedisCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return formatError("ERR empty command");
    }
    
    // 转换命令为大写
    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
    
    Logger::info("执行命令: " + cmd);
    
    // 路由到具体的命令处理函数
    if (cmd == "PING") {
        return cmdPing(args);
    } else if (cmd == "SET") {
        return cmdSet(args);
    } else if (cmd == "GET") {
        return cmdGet(args);
    } else if (cmd == "DEL") {
        return cmdDel(args);
    } else if (cmd == "KEYS") {
        return cmdKeys(args);
    } else if (cmd == "LPUSH") {
        return cmdLPush(args);
    } else if (cmd == "LPOP") {
        return cmdLPop(args);
    } else if (cmd == "LRANGE") {
        return cmdLRange(args);
    } else if (cmd == "HSET") {
        return cmdHSet(args);
    } else if (cmd == "HGET") {
        return cmdHGet(args);
    } else if (cmd == "HKEYS") {
        return cmdHKeys(args);
    } else {
        return formatError("ERR unknown command '" + cmd + "'");
    }
}

void RedisApplicationServer::sendRedisResponse(const std::string& response) {
    Logger::info("准备发送Redis响应: " + response.substr(0, 50) + (response.length() > 50 ? "..." : ""));
    if (m_currentClientFd <= 0) {
        Logger::error("无效的客户端FD，无法发送响应");
        return;
    }
    // 直接发送原始RESP响应
    sendRawRedisResponse(response);
}

void RedisApplicationServer::sendRawRedisResponse(const std::string& response) {
    Logger::info("准备发送原始Redis响应: " + response.substr(0, 50) + (response.length() > 50 ? "..." : ""));

    // 检查是否有有效的客户端连接
    if (m_currentClientFd <= 0) {
        Logger::error("无效的客户端FD，无法发送响应");
        return;
    }

    // 直接发送RESP格式的响应，不需要协议封包
      auto* pureProto = dynamic_cast<PureRedisProtocol*>(m_router->getProtocol(3));
    if (pureProto) {
        pureProto->sendDirectResponse(m_currentClientFd, response);
    } else {
        Logger::error("PureRedisProtocol 未注册");
    }
}

void RedisApplicationServer::onPureRedisResponse(const std::vector<char>& packet) {
    Logger::info("RedisApplicationServer::onPureRedisResponse 被调用！");

    // PureRedisProtocol已经生成了完整的RESP响应
    std::string respResponse(packet.begin(), packet.end());

    Logger::info("收到PureRedisProtocol的RESP响应: " + respResponse.substr(0, 20) + "...");

    // 直接发送RESP响应，不需要再次处理
    auto* pureProto = dynamic_cast<PureRedisProtocol*>(m_router->getProtocol(3));
    if (pureProto) {
        pureProto->sendDirectResponse(m_currentClientFd, std::string(packet.begin(), packet.end()));
    }
}

// ==================== Redis命令实现 ====================

std::string RedisApplicationServer::cmdPing(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return formatSimpleString("PONG");
    } else if (args.size() == 2) {
        return formatBulkString(args[1]);
    } else {
        return formatError("ERR wrong number of arguments for 'ping' command");
    }
}

std::string RedisApplicationServer::cmdSet(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        return formatError("ERR wrong number of arguments for 'set' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    const std::string& value = args[2];

    // 删除其他类型的数据（如果存在）
    m_listData.erase(key);
    m_hashData.erase(key);

    // 设置字符串值
    m_stringData[key] = value;

    Logger::info("SET " + key + " = " + value);
    return formatBulkString("OK");
}

std::string RedisApplicationServer::cmdGet(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return formatError("ERR wrong number of arguments for 'get' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    auto it = m_stringData.find(key);
    if (it != m_stringData.end()) {
        return formatBulkString(it->second);
    }

    return formatNull();
}

std::string RedisApplicationServer::cmdDel(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        return formatError("ERR wrong number of arguments for 'del' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    int deletedCount = 0;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& key = args[i];
        if (m_stringData.erase(key) > 0) {
            deletedCount++;
        }
        if (m_listData.erase(key) > 0) {
            deletedCount++;
        }
        if (m_hashData.erase(key) > 0) {
            deletedCount++;
        }
    }

    return formatInteger(deletedCount);
}

std::string RedisApplicationServer::cmdKeys(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return formatError("ERR wrong number of arguments for 'keys' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    std::vector<std::string> keys;

    // 收集所有键
    for (const auto& pair : m_stringData) {
        keys.push_back(pair.first);
    }
    for (const auto& pair : m_listData) {
        keys.push_back(pair.first);
    }
    for (const auto& pair : m_hashData) {
        keys.push_back(pair.first);
    }

    // 简单的通配符匹配（只支持*）
    const std::string& pattern = args[1];
    if (pattern != "*") {
        std::vector<std::string> filteredKeys;
        for (const auto& key : keys) {
            if (key.find(pattern) != std::string::npos) {
                filteredKeys.push_back(key);
            }
        }
        keys = filteredKeys;
    }

    std::sort(keys.begin(), keys.end());
    return formatArray(keys);
}

std::string RedisApplicationServer::cmdLPush(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        return formatError("ERR wrong number of arguments for 'lpush' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];

    // 删除字符串和哈希类型数据（如果存在）
    m_stringData.erase(key);
    m_hashData.erase(key);

    // 从左边插入所有值
    auto& list = m_listData[key];
    for (size_t i = 2; i < args.size(); ++i) {
        list.push_front(args[i]);
    }

    Logger::info("LPUSH " + key + " (size: " + std::to_string(list.size()) + ")");
    return formatInteger(static_cast<int>(list.size()));
}

std::string RedisApplicationServer::cmdLPop(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return formatError("ERR wrong number of arguments for 'lpop' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    auto it = m_listData.find(key);
    if (it == m_listData.end() || it->second.empty()) {
        return formatNull();
    }

    std::string value = it->second.front();
    it->second.pop_front();

    if (it->second.empty()) {
        m_listData.erase(it);
    }

    return formatBulkString(value);
}

std::string RedisApplicationServer::cmdLRange(const std::vector<std::string>& args) {
    if (args.size() != 4) {
        return formatError("ERR wrong number of arguments for 'lrange' command");
    }

    int start, stop;
    try {
        start = std::stoi(args[2]);
        stop = std::stoi(args[3]);
    } catch (const std::exception&) {
        return formatError("ERR value is not an integer or out of range");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    auto it = m_listData.find(key);
    if (it == m_listData.end()) {
        return formatArray({});
    }

    const auto& list = it->second;
    std::vector<std::string> result;

    int size = static_cast<int>(list.size());
    if (start < 0) start += size;
    if (stop < 0) stop += size;

    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;

    if (start <= stop) {
        auto listIt = list.begin();
        std::advance(listIt, start);

        for (int i = start; i <= stop && listIt != list.end(); ++i, ++listIt) {
            result.push_back(*listIt);
        }
    }

    return formatArray(result);
}

std::string RedisApplicationServer::cmdHSet(const std::vector<std::string>& args) {
    if (args.size() != 4) {
        return formatError("ERR wrong number of arguments for 'hset' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    const std::string& field = args[2];
    const std::string& value = args[3];

    // 删除字符串和列表类型数据（如果存在）
    m_stringData.erase(key);
    m_listData.erase(key);

    auto& hash = m_hashData[key];
    bool isNew = hash.find(field) == hash.end();
    hash[field] = value;

    return formatInteger(isNew ? 1 : 0);
}

std::string RedisApplicationServer::cmdHGet(const std::vector<std::string>& args) {
    if (args.size() != 3) {
        return formatError("ERR wrong number of arguments for 'hget' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    const std::string& field = args[2];

    auto it = m_hashData.find(key);
    if (it == m_hashData.end()) {
        return formatNull();
    }

    auto fieldIt = it->second.find(field);
    if (fieldIt == it->second.end()) {
        return formatNull();
    }

    return formatBulkString(fieldIt->second);
}

std::string RedisApplicationServer::cmdHKeys(const std::vector<std::string>& args) {
    if (args.size() != 2) {
        return formatError("ERR wrong number of arguments for 'hkeys' command");
    }

    std::lock_guard<std::mutex> lock(m_dataLock);

    const std::string& key = args[1];
    auto it = m_hashData.find(key);
    if (it == m_hashData.end()) {
        return formatArray({});
    }

    std::vector<std::string> keys;
    for (const auto& pair : it->second) {
        keys.push_back(pair.first);
    }

    return formatArray(keys);
}

// ==================== RESP协议格式化 ====================

std::string RedisApplicationServer::formatSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string RedisApplicationServer::formatBulkString(const std::string& str) {
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RedisApplicationServer::formatArray(const std::vector<std::string>& arr) {
    std::string result = "*" + std::to_string(arr.size()) + "\r\n";
    for (const auto& item : arr) {
        result += formatBulkString(item);
    }
    return result;
}

std::string RedisApplicationServer::formatInteger(int num) {
    return ":" + std::to_string(num) + "\r\n";
}

std::string RedisApplicationServer::formatError(const std::string& error) {
    return "-" + error + "\r\n";
}

std::string RedisApplicationServer::formatNull() {
    return "$-1\r\n";
}
