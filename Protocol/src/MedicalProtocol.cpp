#include "MedicalProtocol.h"
#include "base/Logger.h"
#include <cstring>

MedicalProtocol::MedicalProtocol() {
    // Logger::debug("MedicalProtocol 初始化");
}

size_t MedicalProtocol::onDataReceived(const char* data, size_t len) {
    if (!data || len == 0) return 0;
    
    // 将新数据添加到缓冲区
    buffer_.insert(buffer_.end(), data, data + len);
    
    size_t total_processed = 0;
    
    // 循环处理缓冲区中的完整帧
    while (buffer_.size() >= MedicalProtocolConst::MIN_FRAME_SIZE) {
        // 查找帧头 0xAA55
        size_t header_pos = 0;
        bool found_header = false;
        
        for (size_t i = 0; i <= buffer_.size() - 2; i++) {
            uint16_t header = (static_cast<uint8_t>(buffer_[i]) << 8) | 
                             static_cast<uint8_t>(buffer_[i + 1]);
            if (header == MedicalProtocolConst::FRAME_HEADER) {
                header_pos = i;
                found_header = true;
                break;
            }
        }
        
        if (!found_header) {
            // 没有找到帧头，清空缓冲区
            buffer_.clear();
            break;
        }
        
        if (header_pos > 0) {
            // 丢弃帧头前的数据
            buffer_.erase(buffer_.begin(), buffer_.begin() + header_pos);
        }
        
        // 检查是否有完整的帧头信息
        if (buffer_.size() < MedicalProtocolConst::MIN_FRAME_SIZE) {
            break;
        }
        
        // 解析数据长度
        uint16_t data_len = (static_cast<uint8_t>(buffer_[5]) << 8) | 
                           static_cast<uint8_t>(buffer_[6]);
        
        size_t frame_len = MedicalProtocolConst::MIN_FRAME_SIZE + data_len;
        
        // 检查是否有完整的帧
        if (buffer_.size() < frame_len) {
            break;  // 数据不完整，等待更多数据
        }
        
        // 解析帧
        uint16_t device_id;
        uint8_t data_type;
        std::vector<char> frame_data;
        
        if (unpackFrame(buffer_.data(), frame_len, device_id, data_type, frame_data)) {
            // 解析成功，触发回调
            if (packetCallback_) {
                packetCallback_(frame_data);
            }
            
            total_processed += frame_len;
        } else {
            Logger::warn("医疗协议帧校验失败，丢弃该帧");
            // 校验失败，跳过这个帧头，继续查找下一个
            buffer_.erase(buffer_.begin(), buffer_.begin() + 2);
            continue;
        }
        
        // 移除已处理的帧
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_len);
    }
    
    return total_processed;
}

bool MedicalProtocol::pack(const char* data, size_t len, std::vector<char>& out) {
    // 简单封装（假设data已经是完整的协议帧）
    out.clear();
    out.insert(out.end(), data, data + len);
    return true;
}

bool MedicalProtocol::packECGData(uint16_t device_id, const ECGData& ecg_data, 
                                 std::vector<char>& out) {
    out.clear();
    out.reserve(MedicalProtocolConst::MIN_FRAME_SIZE + sizeof(ECGData));
    
    // 1. 帧头（大端序）
    out.push_back(static_cast<char>((MedicalProtocolConst::FRAME_HEADER >> 8) & 0xFF));
    out.push_back(static_cast<char>(MedicalProtocolConst::FRAME_HEADER & 0xFF));
    
    // 2. 设备ID（大端序）
    out.push_back(static_cast<char>((device_id >> 8) & 0xFF));
    out.push_back(static_cast<char>(device_id & 0xFF));
    
    // 3. 数据类型
    out.push_back(MedicalProtocolConst::DATA_TYPE_ECG);
    
    // 4. 数据长度（大端序）
    uint16_t data_len = sizeof(ECGData);
    out.push_back(static_cast<char>((data_len >> 8) & 0xFF));
    out.push_back(static_cast<char>(data_len & 0xFF));
    
    // 5. 数据
    const char* data_ptr = reinterpret_cast<const char*>(&ecg_data);
    out.insert(out.end(), data_ptr, data_ptr + sizeof(ECGData));
    
    // 6. 计算校验和
    uint8_t checksum = 0;
    for (char byte : out) {
        checksum += static_cast<uint8_t>(byte);
    }
    out.push_back(static_cast<char>(checksum));
    
    return true;
}

bool MedicalProtocol::unpackFrame(const char* frame_data, size_t frame_len,
                                 uint16_t& device_id, uint8_t& data_type, 
                                 std::vector<char>& data) {
    // 检查最小长度
    if (frame_len < MedicalProtocolConst::MIN_FRAME_SIZE) {
        return false;
    }
    
    // 1. 验证帧头
    uint16_t header = (static_cast<uint8_t>(frame_data[0]) << 8) | 
                     static_cast<uint8_t>(frame_data[1]);
    if (header != MedicalProtocolConst::FRAME_HEADER) {
        return false;
    }
    
    // 2. 解析设备ID
    device_id = (static_cast<uint8_t>(frame_data[2]) << 8) | 
                static_cast<uint8_t>(frame_data[3]);
    
    // 3. 解析数据类型
    data_type = static_cast<uint8_t>(frame_data[4]);
    
    // 4. 解析数据长度
    uint16_t data_len = (static_cast<uint8_t>(frame_data[5]) << 8) | 
                       static_cast<uint8_t>(frame_data[6]);
    
    // 5. 检查帧长度是否匹配
    if (frame_len != MedicalProtocolConst::MIN_FRAME_SIZE + data_len) {
        return false;
    }
    
    // 6. 验证校验和
    uint8_t checksum = 0;
    for (size_t i = 0; i < frame_len - 1; i++) {
        checksum += static_cast<uint8_t>(frame_data[i]);
    }
    if (checksum != static_cast<uint8_t>(frame_data[frame_len - 1])) {
        return false;
    }
    
    // 7. 提取数据
    data.clear();
    data.insert(data.end(), frame_data + 7, frame_data + 7 + data_len);
    
    return true;
}
