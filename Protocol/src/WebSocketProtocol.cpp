#include "WebSocketProtocol.h"
#include "ProtocolRegister.h"
#include "base/Logger.h"
#include "base64.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <openssl/evp.h>
#include <cstring>

static const char* WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
static const size_t MAX_FRAME_SIZE = 10 * 1024 * 1024; // 10MB

// --------------- UTF-8 校验 -----------------
static bool isValidUtf8(const std::string& str) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(str.data());
    size_t len = str.size();
    for (size_t i = 0; i < len; ) {
        unsigned char c = s[i];
        if (c < 0x80) { // 0xxxxxxx
            i++;
        } else if ((c & 0xE0) == 0xC0) { // 110xxxxx 10xxxxxx
            if (i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) return false;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) { // 1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80) return false;
            i += 3;
        } else if ((c & 0xF8) == 0xF0) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= len || (s[i + 1] & 0xC0) != 0x80 || (s[i + 2] & 0xC0) != 0x80 || (s[i + 3] & 0xC0) != 0x80) return false;
            i += 4;
        } else {
            return false; // 非法起始字节
        }
    }
    return true;
}


WebSocketProtocol::WebSocketProtocol() : state_(State::CONNECTING) {
    buffer_.reserve(4096);
}

size_t WebSocketProtocol::onDataReceived(const char* data, size_t length) {
    Logger::debug("WebSocketProtocol received data, length: " + std::to_string(length) + ", state: " + std::to_string(static_cast<int>(state_)));
    
    if (state_ == State::CONNECTING) {
        // 处理WebSocket握手
        std::string handshakeData(data, length);
        Logger::debug("WebSocket handshake data: " + handshakeData);
        if (handleHandshake(handshakeData)) {
            setState(State::OPEN);
            // 握手成功，清空缓冲区
            buffer_.clear();
            Logger::info("WebSocket handshake successful");
            return length; // 返回处理的数据长度
        } else {
            Logger::error("握手失败，关闭连接");
            setState(State::CLOSED);
            if (errorCallback_) {
                errorCallback_("Handshake failed (incomplete or invalid)");
            }
            return length;
        }
    } else if (state_ == State::OPEN || state_ == State::CLOSING) {
        // 处理WebSocket帧
        buffer_.insert(buffer_.end(), data, data + length);
        
        size_t totalConsumed = 0;
        while (totalConsumed < buffer_.size()) {
            size_t bytesConsumed = 0;
            if (!parseFrame(buffer_.data() + totalConsumed, 
                           buffer_.size() - totalConsumed, bytesConsumed)) {
                // 如果parseFrame返回false，说明收到了关闭帧或其他需要关闭连接的情况
                // 移除已处理的数据
                if (bytesConsumed > 0) {
                    totalConsumed += bytesConsumed;
                }
                if (totalConsumed > 0) {
                    buffer_.erase(buffer_.begin(), buffer_.begin() + totalConsumed);
                }
                // 返回处理的字节数
                Logger::debug("WebSocket协议处理完成，总消耗字节数: " + std::to_string(totalConsumed));
                return totalConsumed ? totalConsumed : length;
            }
            totalConsumed += bytesConsumed;
        }
        
        // 移除已处理的数据
        if (totalConsumed > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + totalConsumed);
        }
        
        // 对于OPEN状态，我们返回处理的字节数，即使为0也没关系
        Logger::debug("WebSocket协议处理完成，总消耗字节数: " + std::to_string(totalConsumed));
        return totalConsumed;
    } else if (state_ == State::CLOSED) {
        Logger::info("WebSocket连接已关闭，忽略收到的数据，长度: " + std::to_string(length));
        return length; // 即使连接已关闭，也认为处理了所有数据
    }
    
    return length;
}

bool WebSocketProtocol::handleHandshake(const std::string& data) {
    // 检查是否是有效的HTTP请求
    if (data.find("GET ") != 0) {
        Logger::debug("Not a GET request");
        return false;
    }
    
    // 查找WebSocket升级头部（更宽松的检查）
    if (data.find("Upgrade:") == std::string::npos && 
        data.find("upgrade:") == std::string::npos) {
        Logger::debug("No Upgrade header found");
        return false;
    }
    
    if (data.find("websocket") == std::string::npos && 
        data.find("WebSocket") == std::string::npos) {
        Logger::debug("No websocket keyword found in headers");
        return false;
    }
    
    // 查找Sec-WebSocket-Key
    std::string keyHeader = "Sec-WebSocket-Key:";
    size_t keyPos = data.find(keyHeader);
    if (keyPos == std::string::npos) {
        // 尝试小写形式
        keyHeader = "sec-websocket-key:";
        keyPos = data.find(keyHeader);
    }
    
    if (keyPos == std::string::npos) {
        Logger::debug("Sec-WebSocket-Key header not found");
        return false;
    }
    
    keyPos += keyHeader.length();
    // 跳过空格
    while (keyPos < data.length() && data[keyPos] == ' ') {
        keyPos++;
    }
    
    size_t keyEnd = data.find("\r\n", keyPos);
    if (keyEnd == std::string::npos) {
        // 如果没有找到\r\n，尝试查找\n
        keyEnd = data.find("\n", keyPos);
        if (keyEnd == std::string::npos) {
            // 如果还是没找到，就到字符串末尾
            keyEnd = data.length();
        }
    }
    
    std::string clientKey = data.substr(keyPos, keyEnd - keyPos);
    // 去除可能的空格和换行符
    clientKey.erase(0, clientKey.find_first_not_of(" \t\r\n"));
    clientKey.erase(clientKey.find_last_not_of(" \t\r\n") + 1);
    
    Logger::debug("Client key: " + clientKey);
    std::string response = generateHandshakeResponse(clientKey);
    
    // 发送握手响应（使用原始帧回调）
    if (rawFrameCallback_) {
        std::vector<char> responseData(response.begin(), response.end());
        Logger::info("准备发送WebSocket握手响应，长度: " + std::to_string(responseData.size()));
        rawFrameCallback_(responseData);
        Logger::info("WebSocket握手响应已通过原始帧回调发送");
        return true;
    }
    
    Logger::error("WebSocket握手响应发送失败：原始帧回调未设置");
    return false;
}

std::string WebSocketProtocol::calculateHandshakeKey(const std::string& clientKey) {
    std::string concatenated = clientKey + WEBSOCKET_GUID;
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(concatenated.c_str()), 
         concatenated.length(), hash);
    
    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

std::string WebSocketProtocol::generateHandshakeResponse(const std::string& clientKey) {
    std::string acceptKey = calculateHandshakeKey(clientKey);
    
    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << acceptKey << "\r\n";
    response << "\r\n";
    
    Logger::debug("Handshake response: " + response.str());
    return response.str();
}

bool WebSocketProtocol::parseFrame(const char* data, size_t length, size_t& bytesConsumed) {
    FrameHeader header;
    size_t headerSize = 0;
    if (length < 2) {
        bytesConsumed = 0;
        return false; // 需要更多数据
    }

uint8_t byte1 = static_cast<uint8_t>(data[0]);
uint8_t byte2 = static_cast<uint8_t>(data[1]);

// 检查 FIN 位和 RSV 位是否合理
if ((byte1 & 0x70) != 0) {
    Logger::warn("RSV bits not zero, might be compression or extension");
}

// 检查 opcode 是否合法
uint8_t opcode = byte1 & 0x0F;
if (opcode > 0x0A) {
    Logger::error("Invalid opcode, not a WebSocket frame");
    bytesConsumed = 0;
    return false;
}
    
    if (!parseFrameHeader(data, length, header, headerSize)) {
        bytesConsumed = 0;
        return false; // 需要更多数据
    }
    
    size_t totalFrameSize = headerSize + header.payload_length;
    if (length < totalFrameSize) {
        bytesConsumed = 0;
        return false; // 需要更多数据
    }
    
    // 提取负载数据
    const char* payload = data + headerSize;
    std::string payloadData;
    Logger::info("[调试] 解码前 payload 十六进制:");
std::ostringstream raw;
for (size_t i = 0; i < header.payload_length; ++i) {
    raw << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)payload[i] << ' ';
}
Logger::info(raw.str());
    if (header.masked) {
        payloadData = unmaskPayload(payload, header.payload_length, header.masking_key);
    } else {
        payloadData.assign(payload, header.payload_length);
    }
    Logger::info("[调试] 解码后 payload 十六进制:");
std::ostringstream oss;
for (size_t i = 0; i < payloadData.size(); ++i) {
    oss << std::hex << std::setw(2) << std::setfill('0') << (unsigned char)payloadData[i] << ' ';
}
Logger::info(oss.str());

// ✅ 打印解码后的字符串（可选）
Logger::info("[调试] 解码后 payload 字符串: [" + payloadData + "]");
    // 处理不同类型的帧
    switch (header.opcode) {
        case FrameType::TEXT:
        // 校验TEXT帧的UTF-8有效性
        if (!isValidUtf8(payloadData)) {
            if (state_ == State::CLOSED) { // 已关闭则直接返回
            bytesConsumed = totalFrameSize;
            return false;
            }
            Logger::error("Received TEXT frame with invalid UTF-8, closing connection");
            // 按WebSocket协议，发送1007代码（无效UTF-8）的关闭帧
            std::vector<char> closeFrame;
            packClose(1007, "Invalid UTF-8 in TEXT frame", closeFrame);
            if (rawFrameCallback_) {
                rawFrameCallback_(closeFrame);
            }
            // 标记连接关闭，返回false终止后续处理
            setState(State::CLOSED);
            if (errorCallback_) {
                errorCallback_("Invalid UTF-8 in TEXT frame (opcode: TEXT)");
            }
            bytesConsumed = totalFrameSize;
            return false;
        }
        // 校验通过，传递给应用层
        if (packetCallback_) {
            std::vector<char> payloadVector(payloadData.begin(), payloadData.end());
            packetCallback_(payloadVector);
            Logger::debug("TEXT frame (valid UTF-8) passed to application, length: " + std::to_string(payloadVector.size()));
        }
        break;

    // 2. 处理BINARY帧：直接传递（无需UTF-8校验）
    case FrameType::BINARY:
        if (packetCallback_) {
            std::vector<char> payloadVector(payloadData.begin(), payloadData.end());
            packetCallback_(payloadVector);
            Logger::debug("BINARY frame passed to application, length: " + std::to_string(payloadVector.size()));
        }
        break;
            
        case FrameType::PING:
            // 自动回复PONG（使用原始帧回调）
            {
                std::vector<char> pongFrame;
                if (packPong(payloadData, pongFrame)) {
                    if (rawFrameCallback_) {
                        rawFrameCallback_(pongFrame);
                        Logger::debug("PONG frame sent via raw frame callback");
                    }
                }
            }
            break;
            
        case FrameType::PONG:
            // PONG响应，可以记录日志
            Logger::info("Received PONG frame");
            break;
            
        case FrameType::CLOSE:
            // 连接关闭
            {
                setState(State::CLOSED);
                // 设置bytesConsumed并返回false表示连接应该关闭
                bytesConsumed = totalFrameSize;
                return false;
            }
            break;
            
        default:
            Logger::warn("Unknown WebSocket frame type: " + std::to_string(static_cast<int>(header.opcode)) + 
                         ", treating as CLOSE frame");
            // 对于未知帧类型，我们将其视为关闭帧处理，以避免连接挂起
            setState(State::CLOSED);
            if (errorCallback_) {
                errorCallback_("Unknown frame type: " + std::to_string(static_cast<int>(header.opcode)));
            }
            
            // 发送关闭确认帧（使用原始帧回调）
            std::vector<char> closeFrame;
            if (packClose(1003, "Unknown frame type", closeFrame)) {
                if (rawFrameCallback_) {
                    rawFrameCallback_(closeFrame);
                }
            }
            
            bytesConsumed = totalFrameSize;
            return false;
    }
    
    bytesConsumed = totalFrameSize;
    return true;
}

bool WebSocketProtocol::parseFrameHeader(const char* data, size_t length, 
                                        FrameHeader& header, size_t& headerSize) {
    if (length < 2) {
        return false; // 需要更多数据
    }
    
    uint8_t byte1 = static_cast<uint8_t>(data[0]);
    uint8_t byte2 = static_cast<uint8_t>(data[1]);
    
    header.fin = (byte1 & 0x80) != 0;
    header.rsv1 = (byte1 & 0x40) != 0;
    header.rsv2 = (byte1 & 0x20) != 0;
    header.rsv3 = (byte1 & 0x10) != 0;
    header.opcode = static_cast<FrameType>(byte1 & 0x0F);
    header.masked = (byte2 & 0x80) != 0; 
    
    header.payload_length = byte2 & 0x7F;
    size_t pos = 2;
    
    // 检查是否是无效的帧类型
    if (static_cast<int>(header.opcode) > 0x0A) {
        Logger::warn("Invalid WebSocket frame opcode: " + std::to_string(static_cast<int>(header.opcode)));
        // 即使是无效的操作码，我们也应该继续处理，而不是直接返回false
        // 这样可以避免连接挂起
    }
    
    if (header.payload_length == 126) {
        if (length < pos + 2) {
            return false; // 需要更多数据
        }
        header.payload_length = readUint16(data + pos);
        pos += 2;
    } else if (header.payload_length == 127) {
        if (length < pos + 8) {
            return false; // 需要更多数据
        }
        header.payload_length = readUint64(data + pos);
        pos += 8;
    }
    
    if (header.masked) {
        if (length < pos + 4) {
            return false; // 需要更多数据
        }
        // WebSocket掩码键按字节顺序直接复制
        std::memcpy(header.masking_key, data + pos, 4);
        pos += 4;
        Logger::debug("解析到掩码键（4字节）");
    }
    
    // 检查帧大小限制
    if (header.payload_length > MAX_FRAME_SIZE) {
        Logger::error("WebSocket frame too large: " + std::to_string(header.payload_length));
        if (errorCallback_) {
            errorCallback_("Frame too large");
        }
        return false;
    }
    
    headerSize = pos;
    return true;
}

std::string WebSocketProtocol::unmaskPayload(const char* payload, size_t length, const uint8_t maskingKey[4]) {
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result.push_back(payload[i] ^ maskingKey[i % 4]);
    }
    
    return result;
}

bool WebSocketProtocol::pack(const char* data, size_t len, std::vector<char>& out) {
    // 默认封装为文本消息
    std::string text(data, len);
    return packTextMessage(text, out);
}

bool WebSocketProtocol::packMessage(const std::string& message, FrameType type, std::vector<char>& out) {
    std::vector<char> frame = createFrame(type, message);
    out.insert(out.end(), frame.begin(), frame.end());
    return true;
}

bool WebSocketProtocol::packTextMessage(const std::string& text, std::vector<char>& out) {
    if (!isValidUtf8(text)) {
    Logger::error("packTextMessage: 非法 UTF-8，拒绝发送");
    return false;   // 不再打包
}
    return packMessage(text, FrameType::TEXT, out);
}

bool WebSocketProtocol::packBinaryMessage(const std::vector<uint8_t>& data, std::vector<char>& out) {
    std::string binaryData(data.begin(), data.end());
    return packMessage(binaryData, FrameType::BINARY, out);
}

bool WebSocketProtocol::packPing(const std::string& data, std::vector<char>& out) {
    return packMessage(data, FrameType::PING, out);
}

bool WebSocketProtocol::packPong(const std::string& data, std::vector<char>& out) {
    return packMessage(data, FrameType::PONG, out);
}

bool WebSocketProtocol::packClose(uint16_t code, const std::string& reason, std::vector<char>& out) {
    std::vector<char> payload;
    writeUint16(payload, code);
    payload.insert(payload.end(), reason.begin(), reason.end());
    // 确保我们正确创建关闭帧
    std::vector<char> frame = createFrame(FrameType::CLOSE, std::string(payload.begin(), payload.end()), true);
    out.insert(out.end(), frame.begin(), frame.end());
    return true;
}

std::vector<char> WebSocketProtocol::createFrame(FrameType opcode, const std::string& payload, bool fin) {
    std::vector<char> frame;
    
    // 第一个字节：FIN, RSV, Opcode
    uint8_t byte1 = 0;
    if (fin) byte1 |= 0x80;
    byte1 |= static_cast<uint8_t>(opcode);
    frame.push_back(static_cast<char>(byte1));
    
    // 第二个字节：MASK, Payload Length
    size_t payloadLength = payload.length();
    uint8_t byte2 = 0; // 服务器发送的消息不掩码
    
    if (payloadLength < 126) {
        byte2 |= static_cast<uint8_t>(payloadLength);
        frame.push_back(static_cast<char>(byte2));
    } else if (payloadLength <= 0xFFFF) {
        byte2 |= 126;
        frame.push_back(static_cast<char>(byte2));
        writeUint16(frame, static_cast<uint16_t>(payloadLength));
    } else {
        byte2 |= 127;
        frame.push_back(static_cast<char>(byte2));
        writeUint64(frame, payloadLength);
    }
    
    // 负载数据
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

void WebSocketProtocol::writeFrameHeader(std::vector<char>& buffer, FrameType opcode, 
                                         size_t payloadLength, bool masked) {
    // 这个方法在createFrame中已经实现，这里保留为了接口完整性
    (void)buffer;  // 抑制未使用参数警告
    (void)opcode;
    (void)payloadLength;
    (void)masked;
}

uint16_t WebSocketProtocol::readUint16(const char* data) {
    return (static_cast<uint16_t>(static_cast<uint8_t>(data[0])) << 8) |
           static_cast<uint16_t>(static_cast<uint8_t>(data[1]));
}

uint64_t WebSocketProtocol::readUint64(const char* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint8_t>(data[i]);
    }
    return value;
}

void WebSocketProtocol::writeUint16(std::vector<char>& buffer, uint16_t value) {
    buffer.push_back(static_cast<char>((value >> 8) & 0xFF));
    buffer.push_back(static_cast<char>(value & 0xFF));
}

void WebSocketProtocol::writeUint64(std::vector<char>& buffer, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        buffer.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

void WebSocketProtocol::reset() {
    state_ = State::CONNECTING;
    buffer_.clear();
}

// 注册协议
REGISTER_PROTOCOL(WebSocketProtocol);