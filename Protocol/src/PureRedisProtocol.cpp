#include "PureRedisProtocol.h"
#include "base/Logger.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <sys/socket.h>
#include <cstring>       
#include <arpa/inet.h>

// 心跳包魔数定义 - 用于识别和过滤心跳包
const uint32_t HEARTBEAT_MAGIC = 0xFAFBFCFD;
const size_t MAGIC_LEN = 4;

// 构造
PureRedisProtocol::PureRedisProtocol() {
    Logger::info("PureRedisProtocol 初始化完成");
}

uint32_t PureRedisProtocol::getProtocolId() const {
    return PURE_REDIS_PROTOCOL_ID;
}

std::string PureRedisProtocol::getType() const {
    return "PureRedis";
}

void PureRedisProtocol::reset() {
    m_clientBuffers.clear();
    Logger::debug("PureRedisProtocol状态已重置");
}

size_t PureRedisProtocol::onDataReceived(const char* data, size_t len) {
    return onClientDataReceived(0, data, len);
}

// 简化数据接收处理逻辑，移除魔数过滤
size_t PureRedisProtocol::onClientDataReceived(int clientFd, const char* data, size_t len) {
    Logger::debug("PureRedisProtocol 接收客户端[" + std::to_string(clientFd) + "]数据，长度: " + std::to_string(len));
    
    // 1. 添加数据到缓冲区
    std::string& buffer = m_clientBuffers[clientFd];
    buffer.append(data, len);

    // 2. 先过滤心跳包魔数
    std::string heartbeatFiltered = filterHeartbeat(buffer);
    
    // 3. 再过滤空字节
    std::string cleanBuffer = filterNullBytes(heartbeatFiltered);
    
    // 更新缓冲区为清理后的数据
    if (cleanBuffer.size() != buffer.size()) {
        buffer = cleanBuffer;
        Logger::info("客户端[" + std::to_string(clientFd) + "]数据处理完成，剩余长度: " + std::to_string(buffer.size()));
    }
    
    // 3. RESP协议解析（用清理后的数据）
    size_t totalProcessed = 0;
    while (true) {
        size_t consumed = 0;
        auto [success, args] = resp_decode(buffer, consumed);
        if (!success) break;
        
        if (!args.empty()) {
            Logger::info("Pure Redis处理命令: " + args[0]);
            std::string response = executeRedisCommand(args);
            sendDirectResponse(clientFd, response);
        }
        
        buffer.erase(0, consumed);
        totalProcessed += consumed;
    }
    
    return totalProcessed;
}

bool PureRedisProtocol::pack(const char* data, size_t len, std::vector<char>& packet) {
    packet.assign(data, data + len);
    return true;
}

// -------------- RESP 解码 --------------
std::pair<bool, std::vector<std::string>>
PureRedisProtocol::resp_decode(const std::string& buf, size_t& consumed) {
    consumed = 0;
    if (buf.empty()) return {false, {}};
    if (buf[0] != '*') return {false, {}};          // 只支持数组
    size_t le = buf.find("\r\n", 1);
    if (le == std::string::npos) return {false, {}};
    int64_t count = std::stoll(buf.substr(1, le - 1));
    size_t pos = le + 2;
    std::vector<std::string> out;
    for (int64_t i = 0; i < count; ++i) {
        if (pos + 1 >= buf.size()) return {false, {}};
        if (buf[pos] != '$') return {false, {}};
        le = buf.find("\r\n", pos + 1);
        if (le == std::string::npos) return {false, {}};
        int64_t len = std::stoll(buf.substr(pos + 1, le - pos - 1));
        pos = le + 2;
        if (pos + len + 2 > buf.size()) return {false, {}};
        out.emplace_back(buf.substr(pos, len));
        pos += len + 2;   // 跳过 \r\n
    }
    consumed = pos;
    return {true, out};
}

// -------------- 命令处理 --------------
void PureRedisProtocol::processRedisCommand(int clientFd, const std::vector<std::string>& args) {
    if (args.empty()) {
        Logger::warn("收到空的Redis命令参数");
        return;
    }
    std::string response = executeRedisCommand(args);
    sendDirectResponse(clientFd, response);
}

std::string PureRedisProtocol::executeRedisCommand(const std::vector<std::string>& args) {
    std::string cmd = args[0];
    Logger::info("executeRedisCommand 首参数: '" + args[0] + "'");
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);


    if (cmd == "COMMAND") {
    return formatArray({}); 
    }
    if (cmd == "PING") {  // 现在无论输入ping/PING都能匹配
        if (args.size() == 1) return formatSimpleString("PONG");
        if (args.size() == 2) return formatBulkString(args[1]);
        return formatError("ERR wrong number of arguments for 'ping' command");
    }
    if (cmd == "SET" && args.size() == 3) {
        m_stringData[args[1]] = args[2];
        return formatSimpleString("OK");
    }
    if (cmd == "GET" && args.size() == 2) {
        auto it = m_stringData.find(args[1]);
        if (it == m_stringData.end()) return formatNull();
        return formatBulkString(it->second);
    }
    if (cmd == "DEL" && args.size() >= 2) {
        int deleted = 0;
        for (size_t i = 1; i < args.size(); ++i) deleted += m_stringData.erase(args[i]);
        return formatInteger(deleted);
    }
    if (cmd == "KEYS" && args.size() == 2) {
        std::vector<std::string> keys;
        for (auto& p : m_stringData) keys.push_back(p.first);
        return formatArray(keys);
    }
    return formatError("ERR unknown command '" + cmd + "'");
}

// -------------- RESP 格式化 --------------
std::string PureRedisProtocol::formatSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}
std::string PureRedisProtocol::formatError(const std::string& error) {
    return "-" + error + "\r\n";
}
std::string PureRedisProtocol::formatInteger(int64_t value) {
    return ":" + std::to_string(value) + "\r\n";
}
std::string PureRedisProtocol::formatBulkString(const std::string& str) {
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}
std::string PureRedisProtocol::formatArray(const std::vector<std::string>& array) {
    std::string s = "*" + std::to_string(array.size()) + "\r\n";
    for (auto& item : array) s += formatBulkString(item);
    return s;
}
std::string PureRedisProtocol::formatNull() {
    return "$-1\r\n";
}

// -------------- 发送 --------------
void PureRedisProtocol::sendDirectResponse(int clientFd, const std::string& response) {
    // 1. 校验RESP响应首字符合法性
    if (!response.empty()) {
        char first = response[0];
        if (first != '+' && first != '-' && first != ':' && first != '$' && first != '*') {
            Logger::error("非法RESP响应首字符: 0x" + std::to_string(static_cast<unsigned char>(first)));
            return;
        }
    }
    
    // 2. 发送标准RESP响应（使用非阻塞发送）
    std::lock_guard<std::mutex> lock(m_sendMutex);
    Logger::debug("发送RESP响应: " + response);
    
    if (clientFd > 0) {
        // 使用非阻塞发送，避免阻塞
        ssize_t sent = ::send(clientFd, response.c_str(), response.length(), MSG_DONTWAIT);
        if (sent > 0) {
            Logger::info("PureRedisProtocol 发送成功，长度: " + std::to_string(sent));
        } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 缓冲区满，尝试阻塞发送
            sent = ::send(clientFd, response.c_str(), response.length(), 0);
            if (sent > 0) {
                Logger::info("PureRedisProtocol 阻塞发送成功，长度: " + std::to_string(sent));
            } else {
                Logger::error("PureRedisProtocol 发送失败，错误码: " + std::to_string(errno));
            }
        } else {
            Logger::error("PureRedisProtocol 发送失败，错误码: " + std::to_string(errno));
        }
    } else {
        Logger::error("无效的客户端FD，无法发送响应");
    }
}

// 心跳包过滤函数 - 识别并移除心跳包，保留Redis命令
std::string PureRedisProtocol::filterHeartbeat(const std::string& data) {
    std::string filtered = data;
    size_t totalRemoved = 0;
    
    while (filtered.size() >= MAGIC_LEN) {
        uint32_t magic;
        std::memcpy(&magic, filtered.data(), MAGIC_LEN);
        
        // 检查是否是心跳包魔数
        if (ntohl(magic) == HEARTBEAT_MAGIC) {
            Logger::debug("检测到心跳包魔数，移除4字节");
            filtered = filtered.substr(MAGIC_LEN);
            totalRemoved += MAGIC_LEN;
        } else {
            // 不是心跳包魔数，停止过滤
            break;
        }
    }
    
    if (totalRemoved > 0) {
        Logger::info("过滤心跳包完成，移除了 " + std::to_string(totalRemoved) + " 字节，剩余长度: " + std::to_string(filtered.size()));
    }
    
    return filtered;
}

std::vector<std::string> PureRedisProtocol::parseRedisCommand(const std::string& commandLine) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_quotes = false; // 标记是否在引号内
    char quote_char = '\0'; // 记录当前引号类型（单引号或双引号）

    for (char c : commandLine) {
        if (c == ' ' && !in_quotes) {
            // 空格且不在引号内，结束当前参数
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } else if ((c == '"' || c == '\'') && (quote_char == '\0' || c == quote_char)) {
            // 处理引号（开始或结束）
            in_quotes = !in_quotes;
            if (!in_quotes) {
                quote_char = '\0'; // 结束引号时重置
            } else {
                quote_char = c; // 记录开始的引号类型
            }
        } else {
            // 普通字符，添加到当前参数
            current_arg += c;
        }
    }

    // 添加最后一个参数
    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    return args;
}

size_t PureRedisProtocol::isCompleteRedisCommand(const std::string& buffer) {
    size_t consumed = 0;
    // 调用resp_decode检查是否能解析出完整命令
    auto [success, _] = resp_decode(buffer, consumed);
    return success ? consumed : 0;  // 完整则返回消费长度，否则返回0
}

std::string PureRedisProtocol::filterNullBytes(const std::string& data) {
    std::string filtered;
    filtered.reserve(data.size());  // 预分配空间，提升效率
    
    for (char c : data) {
        if (c != '\0') {  // 跳过空字节，保留其他所有有效字符
            filtered += c;
        }
    }
    
    // 日志记录：显示过滤效果
    if (filtered.size() != data.size()) {
        Logger::info("过滤空字节完成，原始长度: " + std::to_string(data.size()) + 
                    ", 过滤后长度: " + std::to_string(filtered.size()));
    }
    return filtered;
}