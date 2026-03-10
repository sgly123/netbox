#pragma once

#include "ProtocolBase.h"
#include <cstdint>
#include <vector>
#include <cstring>

/**
 * @file MedicalProtocol.h
 * @brief 医疗设备通信协议
 * 
 * 协议格式：
 * +--------+----------+-----------+--------+------+----------+
 * | 帧头   | 设备ID   | 数据类型  | 长度   | 数据 | 校验和   |
 * | 2字节  | 2字节    | 1字节     | 2字节  | N字节| 1字节    |
 * +--------+----------+-----------+--------+------+----------+
 */

// 协议常量
namespace MedicalProtocolConst {
    const uint16_t FRAME_HEADER = 0xAA55;  // 帧头标识
    const uint8_t DATA_TYPE_ECG = 0x01;    // 心电数据
    const uint8_t DATA_TYPE_BP = 0x02;     // 血压数据
    const uint8_t DATA_TYPE_TEMP = 0x03;   // 体温数据
    const size_t MIN_FRAME_SIZE = 8;       // 最小帧长度
}

/**
 * @brief 心电数据包结构
 */
struct ECGData {
    uint32_t timestamp;   // 时间戳（毫秒）
    int16_t  value;       // 心电值（-2048 ~ 2047，单位：mV）
    uint8_t  heart_rate;  // 心率（BPM，正常范围60-100）
    
    ECGData() : timestamp(0), value(0), heart_rate(75) {}
    ECGData(uint32_t ts, int16_t val, uint8_t hr) 
        : timestamp(ts), value(val), heart_rate(hr) {}
} __attribute__((packed));

/**
 * @brief 医疗协议处理类
 * 
 * 继承自ProtocolBase，实现医疗设备数据的封装和解析
 */
class MedicalProtocol : public ProtocolBase {
public:
    static constexpr uint32_t ID = 5;  // 协议ID
    
    MedicalProtocol();
    ~MedicalProtocol() override = default;
    
    /**
     * @brief 获取协议ID
     */
    uint32_t getProtocolId() const override { return ID; }
    
    /**
     * @brief 接收数据处理
     * @param data 接收到的数据
     * @param len 数据长度
     * @return 已处理的字节数
     */
    size_t onDataReceived(const char* data, size_t len) override;
    
    /**
     * @brief 封装数据
     * @param data 原始数据
     * @param len 数据长度
     * @param out 输出缓冲区
     * @return 是否成功
     */
    bool pack(const char* data, size_t len, std::vector<char>& out) override;
    
    /**
     * @brief 封装心电数据
     * @param device_id 设备ID
     * @param ecg_data 心电数据
     * @param out 输出缓冲区
     * @return 是否成功
     */
    static bool packECGData(uint16_t device_id, const ECGData& ecg_data, 
                           std::vector<char>& out);
    
    /**
     * @brief 解析协议帧
     * @param frame_data 帧数据
     * @param frame_len 帧长度
     * @param device_id [out] 设备ID
     * @param data_type [out] 数据类型
     * @param data [out] 数据内容
     * @return 是否解析成功
     */
    static bool unpackFrame(const char* frame_data, size_t frame_len,
                           uint16_t& device_id, uint8_t& data_type, 
                           std::vector<char>& data);

private:
    std::vector<char> buffer_;  // 接收缓冲区（处理粘包）
};
