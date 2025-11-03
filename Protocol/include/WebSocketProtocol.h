#pragma once
#include "ProtocolBase.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

/**
 * @brief WebSocket协议实现类
 * 
 * 协议特点：
 * 1. 基于TCP的全双工通信协议
 * 2. 支持文本和二进制数据传输
 * 3. 使用帧（Frame）格式进行数据封装
 * 4. 包含握手过程进行协议升级
 * 
 * 支持的功能：
 * - WebSocket握手处理
 * - 帧解析和封装
 * - 文本和二进制消息传输
 * - Ping/Pong心跳机制
 * - 连接关闭处理
 * 
 * 使用场景：
 * - 实时Web应用
 * - 在线聊天系统
 * - 实时数据推送
 * - 在线游戏
 */
class WebSocketProtocol : public ProtocolBase {
public:
    static constexpr uint32_t ID = 4;
    
    // 原始帧回调（用于直接发送控制帧）
    using RawFrameCallback = std::function<void(const std::vector<char>&)>;
    
    // WebSocket帧类型
    enum class FrameType {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA
    };
    
    // WebSocket帧头部
    struct FrameHeader {
        bool fin;
        bool rsv1, rsv2, rsv3;
        FrameType opcode;
        bool masked;
        uint64_t payload_length;
        uint8_t masking_key[4];  // 掩码键按字节数组存储
    };

    WebSocketProtocol();
    ~WebSocketProtocol() = default;
    
    // ProtocolBase接口实现
    size_t onDataReceived(const char* data, size_t length) override;
    bool pack(const char* data, size_t len, std::vector<char>& out) override;
    uint32_t getProtocolId() const override { return ID; }
    std::string getType() const override { return "WebSocket"; }
    void reset() override;


    // WebSocket特定方法
    bool packMessage(const std::string& message, FrameType type, std::vector<char>& out);
    bool packTextMessage(const std::string& text, std::vector<char>& out);
    bool packBinaryMessage(const std::vector<uint8_t>& data, std::vector<char>& out);
    bool packPing(const std::string& data, std::vector<char>& out);
    bool packPong(const std::string& data, std::vector<char>& out);
    bool packClose(uint16_t code, const std::string& reason, std::vector<char>& out);

    // WebSocket连接状态
    enum class State {
        CONNECTING,    // 正在握手
        OPEN,          // 连接已建立
        CLOSING,       // 正在关闭
        CLOSED         // 已关闭
    };
    
    // 状态管理
    State getState() const { return state_; }
    void setState(State newState) { state_ = newState; }
    bool isConnected() const { return state_ == State::OPEN; }
    
    // 设置原始帧回调（用于直接发送控制帧和握手响应）
    void setRawFrameCallback(RawFrameCallback callback) { rawFrameCallback_ = callback; }

private:
    State state_;
    std::vector<char> buffer_;  // 数据缓冲区
    RawFrameCallback rawFrameCallback_;  // 原始帧回调
    
    // 握手相关
    bool handleHandshake(const std::string& data);
    std::string calculateHandshakeKey(const std::string& clientKey);
    std::string generateHandshakeResponse(const std::string& clientKey);
    
    // 帧处理
    bool parseFrame(const char* data, size_t length, size_t& bytesConsumed);
    bool parseFrameHeader(const char* data, size_t length, FrameHeader& header, size_t& headerSize);
    std::string unmaskPayload(const char* payload, size_t length, const uint8_t maskingKey[4]);
    
    // 帧创建
    std::vector<char> createFrame(FrameType opcode, const std::string& payload, bool fin = true);
    void writeFrameHeader(std::vector<char>& buffer, FrameType opcode, size_t payloadLength, bool masked = false);
    
    // 工具方法
    uint16_t readUint16(const char* data);
    uint64_t readUint64(const char* data);
    void writeUint16(std::vector<char>& buffer, uint16_t value);
    void writeUint64(std::vector<char>& buffer, uint64_t value);
};