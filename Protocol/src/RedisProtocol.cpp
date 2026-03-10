#include "RedisProtocol.h"
#include "base/Logger.h"
#include <sstream>
#include <algorithm>
#include <iomanip>

RedisProtocol::RedisProtocol() {
    Logger::info("RedisProtocol 初始化完成");
}

uint32_t RedisProtocol::getProtocolId() const {
    return REDIS_PROTOCOL_ID;
}

std::string RedisProtocol::getType() const {
    return "Redis";
}

void RedisProtocol::reset() {
    m_clientBuffers.clear();
    // Logger::debug("RedisProtocol状态已重置");
}

size_t RedisProtocol::onDataReceived(const char* data, size_t len) {
    // ProtocolBase接口，不使用clientFd，使用默认客户端ID 0
    return onClientDataReceived(0, data, len);
}

size_t RedisProtocol::onClientDataReceived(int clientFd, const char* data, size_t len) {
    Logger::info("RedisProtocol收到客户端" + std::to_string(clientFd) + "的数据，长度: " + std::to_string(len));
    
    // 调试：打印原始数据的十六进制
    // std::ostringstream hexStream;
    // hexStream << "Redis原始数据十六进制: ";
    // for (size_t i = 0; i < len && i < 50; ++i) {
    //     hexStream << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)data[i] << " ";
    // }
    // Logger::debug(hexStream.str());
    
    // 调试：打印原始数据的可打印字符
    // std::ostringstream charStream;
    // charStream << "Redis原始数据字符: ";
    // for (size_t i = 0; i < len; ++i) {
    //     char c = data[i];
    //     if (c >= 32 && c <= 126) {
    //         charStream << c;
    //     } else if (c == '\r') {
    //         charStream << "\\r";
    //     } else if (c == '\n') {
    //         charStream << "\\n";
    //     } else {
    //         charStream << "[" << (int)(unsigned char)c << "]";
    //     }
    // }
    // Logger::debug(charStream.str());
    
    // 将数据添加到客户端缓冲区
    std::string& buffer = m_clientBuffers[clientFd];
    buffer.append(data, len);
    
    size_t totalProcessed = 0;
    
    // 处理完整的Redis命令
    while (true) {
        size_t commandLen = isCompleteRedisCommand(buffer);
        if (commandLen == 0) {
            break;  // 没有完整的命令
        }
        
        std::string commandLine = buffer.substr(0, commandLen);
        buffer.erase(0, commandLen);
        totalProcessed += commandLen;
        
        // 移除\r\n
        while (!commandLine.empty() && (commandLine.back() == '\r' || commandLine.back() == '\n')) {
            commandLine.pop_back();
        }
        
        Logger::info("Redis处理命令: " + commandLine);
        processRedisCommand(clientFd, commandLine);
    }
    
    // Logger::debug("RedisProtocol处理了 " + std::to_string(totalProcessed) + " 字节");
    return totalProcessed;
}

bool RedisProtocol::pack(const char* data, size_t len, std::vector<char>& packet) {
    // Redis协议不需要额外的封包，直接使用RESP格式
    packet.clear();
    packet.reserve(len);
    packet.assign(data, data + len);
    
    // Logger::debug("RedisProtocol封包成功，长度: " + std::to_string(len));
    return true;
}



// ==================== Redis协议解析 ====================

void RedisProtocol::processRedisCommand(int clientFd, const std::string& commandLine) {
    if (commandLine.empty()) {
        Logger::warn("收到空的Redis命令");
        if (errorCallback_) {
            errorCallback_("Empty Redis command");
        }
        return;
    }
    
    // 解析Redis命令参数
    auto args = parseRedisCommand(commandLine);
    if (args.empty()) {
        Logger::warn("Redis命令解析失败: " + commandLine);
        if (errorCallback_) {
            errorCallback_("Failed to parse Redis command: " + commandLine);
        }
        return;
    }
    
    Logger::info("Redis解析出 " + std::to_string(args.size()) + " 个参数");
    // for (size_t i = 0; i < args.size(); ++i) {
    //     Logger::debug("Redis参数[" + std::to_string(i) + "]: '" + args[i] + "'");
    // }
    
    // 直接在协议层处理Redis命令，不依赖应用层回调
    std::string response = executeRedisCommand(args);
    Logger::info("RedisProtocol直接执行命令，响应: " + response.substr(0, 20) + "...");

    // 直接发送响应到客户端
    sendDirectResponse(clientFd, response);
}

std::vector<std::string> RedisProtocol::parseRedisCommand(const std::string& commandLine) {
    std::vector<std::string> args;
    std::istringstream iss(commandLine);
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

size_t RedisProtocol::isCompleteRedisCommand(const std::string& buffer) {
    // 查找完整的命令行（以\n结尾）
    size_t pos = buffer.find('\n');
    if (pos != std::string::npos) {
        return pos + 1;  // 包含\n的长度
    }
    return 0;  // 没有完整的命令
}

// ==================== RESP协议格式化 ====================

std::string RedisProtocol::formatSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string RedisProtocol::formatBulkString(const std::string& str) {
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RedisProtocol::formatArray(const std::vector<std::string>& arr) {
    std::string result = "*" + std::to_string(arr.size()) + "\r\n";
    for (const auto& item : arr) {
        result += formatBulkString(item);
    }
    return result;
}

std::string RedisProtocol::formatInteger(int num) {
    return ":" + std::to_string(num) + "\r\n";
}

std::string RedisProtocol::formatError(const std::string& error) {
    return "-" + error + "\r\n";
}

std::string RedisProtocol::formatNull() {
    return "$-1\r\n";
}

// ==================== Redis命令执行 ====================

std::string RedisProtocol::executeRedisCommand(const std::vector<std::string>& args) {
    if (args.empty()) {
        return formatError("ERR empty command");
    }

    // 转换命令为大写
    std::string cmd = args[0];
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    Logger::info("RedisProtocol执行命令: " + cmd);

    // 简单的命令处理
    if (cmd == "PING") {
        if (args.size() == 1) {
            return formatSimpleString("PONG");
        } else if (args.size() == 2) {
            return formatBulkString(args[1]);
        } else {
            return formatError("ERR wrong number of arguments for 'ping' command");
        }
    } else {
        return formatError("ERR unknown command '" + cmd + "'");
    }
}

void RedisProtocol::sendDirectResponse(int clientFd, const std::string& response) {
    Logger::info("RedisProtocol直接发送响应到客户端" + std::to_string(clientFd));

    // 这里需要获取socket并直接发送
    // 由于我们在协议层，没有直接的socket访问，所以先记录日志
    // Logger::debug("需要发送的响应: " + response);

    // TODO: 实现直接socket发送
    // 暂时通过回调机制发送
    if (packetCallback_) {
        std::vector<char> responsePacket(response.begin(), response.end());
        packetCallback_(responsePacket);
    }
}
