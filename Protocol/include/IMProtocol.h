#pragma once

#include "ProtocolBase.h"
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

/**
 * @brief 即时通讯协议
 * 
 * 协议格式：
 * +--------+--------+----------+----------+
 * | Magic  | MsgType| Length   | Payload  |
 * | 4bytes | 2bytes | 4bytes   | N bytes  |
 * +--------+--------+----------+----------+
 * 
 * Magic: 0x494D4348 ("IMCH" - Instant Message Chat)
 * MsgType: 消息类型
 * Length: Payload长度
 * Payload: JSON格式的消息体
 */

class IMProtocol : public ProtocolBase {
public:
    // 协议魔数
    static constexpr uint32_t PROTOCOL_MAGIC = 0x494D4348;
    
    // 消息类型枚举
    enum class MessageType : uint16_t {
        // 认证相关 (0x01xx)
        LOGIN_REQUEST = 0x0101,
        LOGIN_RESPONSE = 0x0102,
        LOGOUT_REQUEST = 0x0103,
        LOGOUT_RESPONSE = 0x0104,
        REGISTER_REQUEST = 0x0105,
        REGISTER_RESPONSE = 0x0106,
        
        // 好友相关 (0x02xx)
        FRIEND_LIST_REQUEST = 0x0201,
        FRIEND_LIST_RESPONSE = 0x0202,
        ADD_FRIEND_REQUEST = 0x0203,
        ADD_FRIEND_RESPONSE = 0x0204,
        DELETE_FRIEND_REQUEST = 0x0205,
        DELETE_FRIEND_RESPONSE = 0x0206,
        
        // 消息相关 (0x03xx)
        CHAT_MESSAGE = 0x0301,
        CHAT_MESSAGE_ACK = 0x0302,
        OFFLINE_MESSAGE_REQUEST = 0x0303,
        OFFLINE_MESSAGE_RESPONSE = 0x0304,
        
        // 群组相关 (0x04xx)
        CREATE_GROUP_REQUEST = 0x0401,
        CREATE_GROUP_RESPONSE = 0x0402,
        JOIN_GROUP_REQUEST = 0x0403,
        JOIN_GROUP_RESPONSE = 0x0404,
        GROUP_MESSAGE = 0x0405,
        GROUP_MESSAGE_ACK = 0x0406,
        
        // 文件传输 (0x05xx)
        FILE_UPLOAD_REQUEST = 0x0501,
        FILE_UPLOAD_RESPONSE = 0x0502,
        FILE_DOWNLOAD_REQUEST = 0x0503,
        FILE_DOWNLOAD_RESPONSE = 0x0504,
        FILE_CHUNK = 0x0505,
        
        // 状态相关 (0x06xx)
        HEARTBEAT = 0x0601,
        USER_STATUS_CHANGE = 0x0602,
        
        // 错误响应
        ERROR_RESPONSE = 0xFFFF
    };
    
    // 协议头结构
    struct ProtocolHeader {
        uint32_t magic;      // 魔数
        uint16_t msg_type;   // 消息类型
        uint32_t length;     // payload长度
        
        ProtocolHeader() : magic(PROTOCOL_MAGIC), msg_type(0), length(0) {}
    } __attribute__((packed));
    
    IMProtocol();
    ~IMProtocol() override = default;
    
    /**
     * @brief 获取协议ID
     */
    uint32_t getProtocolId() const override { return PROTOCOL_MAGIC; }
    
    /**
     * @brief 获取协议类型
     */
    std::string getType() const override { return "IM"; }
    
    /**
     * @brief 重置协议状态
     */
    void reset() override { buffer_.clear(); }
    
    /**
     * @brief 设置数据包接收回调（兼容旧接口）
     */
    void setOnPacketReceived(PacketCallback callback) { 
        setPacketCallback(callback); 
    }
    
    /**
     * @brief 数据接收处理
     */
    size_t onDataReceived(const char* data, size_t len) override;
    
    /**
     * @brief 打包消息
     */
    bool pack(const char* data, size_t len, std::vector<char>& out) override;
    
    /**
     * @brief 打包带类型的消息
     */
    bool packMessage(MessageType type, const std::string& payload, std::vector<char>& out);
    
    /**
     * @brief 解析协议头
     */
    static bool parseHeader(const char* data, size_t len, ProtocolHeader& header);
    
    /**
     * @brief 获取消息类型名称
     */
    static std::string getMessageTypeName(MessageType type);

private:
    std::vector<char> buffer_;  // 接收缓冲区
};
