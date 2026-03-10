#include "ModbusProtocol.h"
#include "base/Logger.h"
#include <cstring>

ModbusProtocol::ModbusProtocol() {
    Logger::info("ModbusProtocol 初始化");
}

ModbusProtocol::~ModbusProtocol() {
}

size_t ModbusProtocol::onDataReceived(const char* data, size_t len) {
    buffer_.insert(buffer_.end(), data, data + len);
    
    if (buffer_.size() < 8) {
        return 0;
    }
    
    Modbus::MBAP_Header header;
    if (!parseMBAPHeader(buffer_.data(), buffer_.size(), header)) {
        return 0;
    }
    
    size_t total_length = 6 + header.length;
    
    if (buffer_.size() < total_length) {
        return 0;
    }
    
    auto response = parseResponse(buffer_.data(), total_length);
    
    if (response) {
        if (response->is_exception) {
            Logger::error("Modbus异常: " + getExceptionMessage(response->exception_code));
        } else {
            Logger::debug("Modbus响应成功");
        }
        
        if (m_callback) {
            std::vector<char> packet(buffer_.begin(), buffer_.begin() + total_length);
            m_callback(packet);
        }
    }
    
    buffer_.erase(buffer_.begin(), buffer_.begin() + total_length);
    
    return total_length;
}

bool ModbusProtocol::pack(const char* data, size_t len, std::vector<char>& out) {
    out.assign(data, data + len);
    return true;
}

std::vector<uint8_t> ModbusProtocol::buildReadHoldingRegistersRequest(
    uint16_t transaction_id,
    uint8_t unit_id,
    uint16_t start_address,
    uint16_t quantity
) {
    std::vector<uint8_t> request;
    
    request.push_back((transaction_id >> 8) & 0xFF);
    request.push_back(transaction_id & 0xFF);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x06);
    request.push_back(unit_id);
    
    request.push_back(Modbus::READ_HOLDING_REGISTERS);
    request.push_back((start_address >> 8) & 0xFF);
    request.push_back(start_address & 0xFF);
    request.push_back((quantity >> 8) & 0xFF);
    request.push_back(quantity & 0xFF);
    
    return request;
}

std::vector<uint8_t> ModbusProtocol::buildWriteSingleRegisterRequest(
    uint16_t transaction_id,
    uint8_t unit_id,
    uint16_t address,
    uint16_t value
) {
    std::vector<uint8_t> request;
    
    request.push_back((transaction_id >> 8) & 0xFF);
    request.push_back(transaction_id & 0xFF);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back(0x06);
    request.push_back(unit_id);
    
    request.push_back(Modbus::WRITE_SINGLE_REGISTER);
    request.push_back((address >> 8) & 0xFF);
    request.push_back(address & 0xFF);
    request.push_back((value >> 8) & 0xFF);
    request.push_back(value & 0xFF);
    
    return request;
}

std::vector<uint8_t> ModbusProtocol::buildWriteMultipleRegistersRequest(
    uint16_t transaction_id,
    uint8_t unit_id,
    uint16_t start_address,
    const std::vector<uint16_t>& values
) {
    std::vector<uint8_t> request;
    
    uint16_t quantity = values.size();
    uint8_t byte_count = quantity * 2;
    uint16_t length = 7 + byte_count;
    
    request.push_back((transaction_id >> 8) & 0xFF);
    request.push_back(transaction_id & 0xFF);
    request.push_back(0x00);
    request.push_back(0x00);
    request.push_back((length >> 8) & 0xFF);
    request.push_back(length & 0xFF);
    request.push_back(unit_id);
    
    request.push_back(Modbus::WRITE_MULTIPLE_REGISTERS);
    request.push_back((start_address >> 8) & 0xFF);
    request.push_back(start_address & 0xFF);
    request.push_back((quantity >> 8) & 0xFF);
    request.push_back(quantity & 0xFF);
    request.push_back(byte_count);
    
    for (uint16_t value : values) {
        request.push_back((value >> 8) & 0xFF);
        request.push_back(value & 0xFF);
    }
    
    return request;
}

std::shared_ptr<Modbus::ModbusResponse> ModbusProtocol::parseResponse(
    const uint8_t* data,
    size_t len
) {
    auto response = std::make_shared<Modbus::ModbusResponse>();
    
    if (!parseMBAPHeader(data, len, response->header)) {
        return nullptr;
    }
    
    if (len < 8) {
        return nullptr;
    }
    
    response->function_code = data[7];
    
    if (response->function_code & 0x80) {
        response->is_exception = true;
        if (len >= 9) {
            response->exception_code = data[8];
        }
        return response;
    }
    
    if (len > 8) {
        response->data.assign(data + 8, data + len);
    }
    
    return response;
}

std::vector<uint16_t> ModbusProtocol::parseReadRegistersResponse(
    const Modbus::ModbusResponse& response
) {
    std::vector<uint16_t> registers;
    
    if (response.is_exception || response.data.empty()) {
        return registers;
    }
    
    uint8_t byte_count = response.data[0];
    
    for (size_t i = 1; i + 1 < response.data.size() && i < byte_count + 1; i += 2) {
        uint16_t value = (response.data[i] << 8) | response.data[i + 1];
        registers.push_back(value);
    }
    
    return registers;
}

bool ModbusProtocol::isValidModbusPacket(const uint8_t* data, size_t len) {
    if (len < 8) {
        return false;
    }
    
    uint16_t protocol_id = (data[2] << 8) | data[3];
    if (protocol_id != 0x0000) {
        return false;
    }
    
    uint16_t length = (data[4] << 8) | data[5];
    if (len < 6 + length) {
        return false;
    }
    
    return true;
}

std::string ModbusProtocol::getExceptionMessage(uint8_t exception_code) {
    switch (exception_code) {
        case Modbus::ILLEGAL_FUNCTION:
            return "非法功能码";
        case Modbus::ILLEGAL_DATA_ADDRESS:
            return "非法数据地址";
        case Modbus::ILLEGAL_DATA_VALUE:
            return "非法数据值";
        case Modbus::SLAVE_DEVICE_FAILURE:
            return "从站设备故障";
        case Modbus::ACKNOWLEDGE:
            return "确认";
        case Modbus::SLAVE_DEVICE_BUSY:
            return "从站设备忙";
        case Modbus::MEMORY_PARITY_ERROR:
            return "内存奇偶校验错误";
        case Modbus::GATEWAY_PATH_UNAVAILABLE:
            return "网关路径不可用";
        case Modbus::GATEWAY_TARGET_FAILED_TO_RESPOND:
            return "网关目标设备无响应";
        default:
            return "未知异常 (0x" + std::to_string(exception_code) + ")";
    }
}

bool ModbusProtocol::parseMBAPHeader(
    const uint8_t* data,
    size_t len,
    Modbus::MBAP_Header& header
) {
    if (len < 7) {
        return false;
    }
    
    header.transaction_id = (data[0] << 8) | data[1];
    header.protocol_id = (data[2] << 8) | data[3];
    header.length = (data[4] << 8) | data[5];
    header.unit_id = data[6];
    
    return true;
}

bool ModbusProtocol::validatePacket(const uint8_t* data, size_t len) {
    return isValidModbusPacket(data, len);
}


