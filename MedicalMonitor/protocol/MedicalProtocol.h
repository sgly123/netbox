#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

/**
 * @file MedicalProtocol.h
 * @brief 医疗设备通信协议定义
 * 
 * 协议格式：
 * +--------+----------+-----------+--------+------+----------+
 * | 帧头   | 设备ID   | 数据类型  | 长度   | 数据 | 校验和   |
 * | 2字节  | 2字节    | 1字节     | 2字节  | N字节| 1字节    |
 * +--------+----------+-----------+--------+------+----------+
 * 
 * 帧头：0xAA55（固定）
 * 设备ID：1-65535（唯一标识设备）
 * 数据类型：0x01=心电数据, 0x02=血压, 0x03=体温
 * 长度：数据部分的字节数
 * 数据：实际的医疗数据
 * 校验和：所有字节相加取低8位
 */

namespace MedicalProtocol {

// 协议常量
const uint16_t FRAME_HEADER = 0xAA55;  // 帧头标识
const uint8_t DATA_TYPE_ECG = 0x01;    // 心电数据
const uint8_t DATA_TYPE_BP = 0x02;     // 血压数据
const uint8_t DATA_TYPE_TEMP = 0x03;   // 体温数据

// 最小帧长度：帧头(2) + 设备ID(2) + 类型(1) + 长度(2) + 校验和(1) = 8字节
const size_t MIN_FRAME_SIZE = 8;

/**
 * @brief 心电数据包结构
 * 
 * 每秒发送500个采样点（500Hz采样率）
 * 每个数据包包含1个采样点
 */
struct ECGData {
    uint32_t timestamp;   // 时间戳（毫秒）
    int16_t  value;       // 心电值（-2048 ~ 2047，单位：mV）
    uint8_t  heart_rate;  // 心率（BPM，正常范围60-100）
    
    ECGData() : timestamp(0), value(0), heart_rate(75) {}
    ECGData(uint32_t ts, int16_t val, uint8_t hr) 
        : timestamp(ts), value(val), heart_rate(hr) {}
} __attribute__((packed));  // 确保结构体紧凑排列

/**
 * @brief 血压数据包结构
 */
struct BPData {
    uint32_t timestamp;      // 时间戳（毫秒）
    uint8_t  systolic;       // 收缩压（mmHg，正常范围90-140）
    uint8_t  diastolic;      // 舒张压（mmHg，正常范围60-90）
    uint8_t  heart_rate;     // 心率（BPM）
    
    BPData() : timestamp(0), systolic(120), diastolic(80), heart_rate(75) {}
} __attribute__((packed));

/**
 * @brief 体温数据包结构
 */
struct TempData {
    uint32_t timestamp;      // 时间戳（毫秒）
    uint16_t temperature;    // 体温（单位：0.01℃，例如3650表示36.50℃）
    
    TempData() : timestamp(0), temperature(3650) {}
} __attribute__((packed));

/**
 * @brief 协议帧封装类
 */
class Frame {
public:
    /**
     * @brief 封装数据为协议帧
     * @param device_id 设备ID
     * @param data_type 数据类型
     * @param data 数据指针
     * @param data_len 数据长度
     * @return 封装后的完整帧
     */
    static std::vector<char> pack(uint16_t device_id, uint8_t data_type, 
                                   const void* data, uint16_t data_len) {
        std::vector<char> frame;
        frame.reserve(MIN_FRAME_SIZE + data_len);
        
        // 1. 帧头（大端序）
        frame.push_back((FRAME_HEADER >> 8) & 0xFF);
        frame.push_back(FRAME_HEADER & 0xFF);
        
        // 2. 设备ID（大端序）
        frame.push_back((device_id >> 8) & 0xFF);
        frame.push_back(device_id & 0xFF);
        
        // 3. 数据类型
        frame.push_back(data_type);
        
        // 4. 数据长度（大端序）
        frame.push_back((data_len >> 8) & 0xFF);
        frame.push_back(data_len & 0xFF);
        
        // 5. 数据
        const char* data_ptr = static_cast<const char*>(data);
        for (uint16_t i = 0; i < data_len; i++) {
            frame.push_back(data_ptr[i]);
        }
        
        // 6. 计算校验和（所有字节相加）
        uint8_t checksum = 0;
        for (char byte : frame) {
            checksum += static_cast<uint8_t>(byte);
        }
        frame.push_back(checksum);
        
        return frame;
    }
    
    /**
     * @brief 解析协议帧
     * @param frame_data 帧数据
     * @param frame_len 帧长度
     * @param device_id [out] 设备ID
     * @param data_type [out] 数据类型
     * @param data [out] 数据内容
     * @return 是否解析成功
     */
    static bool unpack(const char* frame_data, size_t frame_len,
                      uint16_t& device_id, uint8_t& data_type, 
                      std::vector<char>& data) {
        // 检查最小长度
        if (frame_len < MIN_FRAME_SIZE) {
            return false;
        }
        
        // 1. 验证帧头
        uint16_t header = (static_cast<uint8_t>(frame_data[0]) << 8) | 
                         static_cast<uint8_t>(frame_data[1]);
        if (header != FRAME_HEADER) {
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
        if (frame_len != MIN_FRAME_SIZE + data_len) {
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
};

} // namespace MedicalProtocol
