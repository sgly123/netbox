#include "IMProtocol.h"
#include "base/Logger.h"
#include <arpa/inet.h>

IMProtocol::IMProtocol() : ProtocolBase() {
    buffer_.reserve(8192);  // 预分配8KB缓冲区
}

size_t IMProtocol::onDataReceived(const char* data, size_t len) {
    if (!data || len == 0) return 0;
    
    // 追加到缓冲区
    buffer_.insert(buffer_.end(), data, data + len);
    
    size_t processed = 0;
    
    // IM协议格式：[Type 2字节][Length 4字节][Payload]
    // 注意：Magic已经被ProtocolRouter识别并跳过了
    while (buffer_.size() >= 6) {  // 最小包头：2字节type + 4字节length
        // 解析消息类型和长度
        uint16_t msg_type;
        uint32_t length;
        
        std::memcpy(&msg_type, buffer_.data(), 2);
        std::memcpy(&length, buffer_.data() + 2, 4);
        
        msg_type = ntohs(msg_type);
        length = ntohl(length);
        
        // 验证长度（防止恶意包）
        if (length > 10 * 1024 * 1024) {  // 最大10MB
            Logger::error("[IMProtocol] 消息长度异常: " + std::to_string(length));
            buffer_.clear();
            return processed;
        }
        
        // 检查完整包
        size_t total_len = 6 + length;  // 2(type) + 4(length) + payload
        if (buffer_.size() < total_len) {
            // 数据不完整，等待更多数据
            break;
        }
        
        // 提取payload（包含完整的消息头，供上层使用）
        std::vector<char> packet;
        packet.resize(sizeof(ProtocolHeader) + length);
        
        // 重建完整的协议头（包括Magic）
        ProtocolHeader header;
        header.magic = htonl(PROTOCOL_MAGIC);
        header.msg_type = htons(msg_type);
        header.length = htonl(length);
        
        std::memcpy(packet.data(), &header, sizeof(ProtocolHeader));
        std::memcpy(packet.data() + sizeof(ProtocolHeader), 
                   buffer_.data() + 6, length);
        
        // 触发回调
        if (packetCallback_) {
            packetCallback_(packet);
        }
        
        // 移除已处理的数据
        buffer_.erase(buffer_.begin(), buffer_.begin() + total_len);
        processed += total_len;
        
        // Logger::debug("[IMProtocol] 处理消息: " + getMessageTypeName((MessageType)msg_type) +
        //              ", 长度: " + std::to_string(length));
    }
    
    return processed;
}

bool IMProtocol::pack(const char* data, size_t len, std::vector<char>& out) {
    return packMessage(MessageType::CHAT_MESSAGE, std::string(data, len), out);
}

bool IMProtocol::packMessage(MessageType type, const std::string& payload, std::vector<char>& out) {
    ProtocolHeader header;
    header.magic = htonl(PROTOCOL_MAGIC);
    header.msg_type = htons(static_cast<uint16_t>(type));
    header.length = htonl(static_cast<uint32_t>(payload.size()));
    
    // 组装完整包
    out.resize(sizeof(ProtocolHeader) + payload.size());
    std::memcpy(out.data(), &header, sizeof(ProtocolHeader));
    std::memcpy(out.data() + sizeof(ProtocolHeader), payload.data(), payload.size());
    
    return true;
}

bool IMProtocol::parseHeader(const char* data, size_t len, ProtocolHeader& header) {
    if (len < sizeof(ProtocolHeader)) return false;
    
    std::memcpy(&header, data, sizeof(ProtocolHeader));
    
    // 转换字节序
    header.magic = ntohl(header.magic);
    header.msg_type = ntohs(header.msg_type);
    header.length = ntohl(header.length);
    
    // 验证魔数
    if (header.magic != PROTOCOL_MAGIC) {
        Logger::error("[IMProtocol] 魔数错误: 0x" + 
                     std::to_string(header.magic));
        return false;
    }
    
    // 验证长度（防止恶意包）
    if (header.length > 10 * 1024 * 1024) {  // 最大10MB
        Logger::error("[IMProtocol] 消息长度异常: " + std::to_string(header.length));
        return false;
    }
    
    return true;
}

std::string IMProtocol::getMessageTypeName(MessageType type) {
    switch (type) {
        case MessageType::LOGIN_REQUEST: return "LOGIN_REQUEST";
        case MessageType::LOGIN_RESPONSE: return "LOGIN_RESPONSE";
        case MessageType::LOGOUT_REQUEST: return "LOGOUT_REQUEST";
        case MessageType::LOGOUT_RESPONSE: return "LOGOUT_RESPONSE";
        case MessageType::REGISTER_REQUEST: return "REGISTER_REQUEST";
        case MessageType::REGISTER_RESPONSE: return "REGISTER_RESPONSE";
        case MessageType::FRIEND_LIST_REQUEST: return "FRIEND_LIST_REQUEST";
        case MessageType::FRIEND_LIST_RESPONSE: return "FRIEND_LIST_RESPONSE";
        case MessageType::ADD_FRIEND_REQUEST: return "ADD_FRIEND_REQUEST";
        case MessageType::ADD_FRIEND_RESPONSE: return "ADD_FRIEND_RESPONSE";
        case MessageType::DELETE_FRIEND_REQUEST: return "DELETE_FRIEND_REQUEST";
        case MessageType::DELETE_FRIEND_RESPONSE: return "DELETE_FRIEND_RESPONSE";
        case MessageType::CHAT_MESSAGE: return "CHAT_MESSAGE";
        case MessageType::CHAT_MESSAGE_ACK: return "CHAT_MESSAGE_ACK";
        case MessageType::GROUP_MESSAGE: return "GROUP_MESSAGE";
        case MessageType::FILE_UPLOAD_REQUEST: return "FILE_UPLOAD_REQUEST";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        case MessageType::USER_STATUS_CHANGE: return "USER_STATUS_CHANGE";
        case MessageType::ERROR_RESPONSE: return "ERROR_RESPONSE";
        default: return "UNKNOWN";
    }
}
