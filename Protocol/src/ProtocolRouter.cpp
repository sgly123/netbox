#include "ProtocolRouter.h"
#include "base/Logger.h"
#include <cstring>
#include <arpa/inet.h>
#include <algorithm>
#include <string>


void ProtocolRouter::registerProtocol(uint32_t protoId, std::shared_ptr<ProtocolBase> proto) {
    protocols_[protoId] = proto;
    proto->setPacketCallback([this, protoId](const std::vector<char>& packet) {
        if (packetCallback_) {
            packetCallback_(protoId, packet);
        }
    });
    proto->setErrorCallback(errorCallback_);
}

size_t ProtocolRouter::onDataReceived(int client_fd, const char* data, size_t len) {
    (void)client_fd; // 避免未使用参数警告
    if (!data || len == 0) return 0;


    // ==================== 心跳包识别和过滤 ====================
    if (len >= sizeof(uint32_t)) {  // 确保有足够字节解析魔数
    uint32_t magic_recv = 0;
    std::memcpy(&magic_recv, data, sizeof(magic_recv));
    magic_recv = ntohl(magic_recv);  // 网络字节序转主机字节序
    
    if (magic_recv == HEARTBEAT_MAGIC) {
        // Logger::debug("协议路由器识别到心跳包，已过滤");
        return sizeof(uint32_t);  // 返回处理的心跳包长度，不再向下传递
    }
}

    // ✅ 如果是 RESP 协议（以 '*' 开头），直接走 PureRedisProtocol
    if (len > 0 && data[0] == '*') {
        auto it = protocols_.find(3); // PureRedisProtocol
        if (it != protocols_.end()) {
            return it->second->onDataReceived(data, len); // ✅ 完整数据交给 RESP 解码器
        }
        return 0;
    }

    // ==================== 协议路由处理 ====================
    // 检查是否有协议路由头（至少4字节）
    if (len >= 4) {
        // 解析协议ID（前4字节，大端序）
        uint32_t protocolId = 0;
        std::memcpy(&protocolId, data, 4);
        protocolId = ntohl(protocolId);

        Logger::info("协议分发器收到数据包，协议ID: " + std::to_string(protocolId) +
                    ", 总长度: " + std::to_string(len));

        // 查找对应的协议处理器
        auto it = protocols_.find(protocolId);
        if (it != protocols_.end()) {
            // 跳过协议ID，只传递协议数据部分
            const char* protocolData = data + 4;
            size_t protocolDataLen = len - 4;

            Logger::info("传递给协议" + std::to_string(protocolId) + "的数据长度: " +
                        std::to_string(protocolDataLen));

            size_t processed = it->second->onDataReceived(protocolData, protocolDataLen);
            if (processed > 0) {
                // 返回实际处理的总字节数（包括协议ID）
                return processed + 4;
            }
        } else {
            Logger::warn("未找到协议ID " + std::to_string(protocolId) + " 的处理器");
        }
    }

    // 智能协议识别（用于没有协议头的数据）
    uint32_t detectedProtocolId = detectProtocol(data, len);
    auto it = protocols_.find(detectedProtocolId);
    if (it != protocols_.end()) {
        return it->second->onDataReceived(data, len);
    }

    return 0;
}

uint32_t ProtocolRouter::detectProtocol(const char* data, size_t len) {
    if (!data || len == 0) {
        return 1; // 默认使用SimpleHeader协议
    }

    // Redis协议检测：查找Redis命令特征
    std::string dataStr(data, std::min(len, size_t(50))); // 只检查前50字节

    // 转换为大写进行匹配
    std::string upperData = dataStr;
    std::transform(upperData.begin(), upperData.end(), upperData.begin(), ::toupper);

    // Redis命令特征检测
    if (upperData.find("PING") != std::string::npos ||
        upperData.find("SET ") != std::string::npos ||
        upperData.find("GET ") != std::string::npos ||
        upperData.find("DEL ") != std::string::npos ||
        upperData.find("KEYS") != std::string::npos ||
        upperData.find("LPUSH") != std::string::npos ||
        upperData.find("LPOP") != std::string::npos ||
        upperData.find("LRANGE") != std::string::npos ||
        upperData.find("HSET") != std::string::npos ||
        upperData.find("HGET") != std::string::npos ||
        upperData.find("HKEYS") != std::string::npos) {
        return 3; // PureRedisProtocol
    }

    // 默认使用SimpleHeader协议
    return 1;
}