#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>

/**
 * @brief Base64编码解码工具类
 * 
 * 提供WebSocket握手所需的Base64编码功能
 */
class Base64 {
public:
    /**
     * @brief 对二进制数据进行Base64编码
     * @param data 输入数据指针
     * @param length 数据长度
     * @return Base64编码后的字符串
     */
    static std::string encode(const unsigned char* data, size_t length);
    
    /**
     * @brief 对字符串进行Base64编码
     * @param input 输入字符串
     * @return Base64编码后的字符串
     */
    static std::string encode(const std::string& input);
    
    /**
     * @brief 对Base64字符串进行解码
     * @param encoded Base64编码的字符串
     * @return 解码后的二进制数据
     */
    static std::vector<unsigned char> decode(const std::string& encoded);

private:
    static const std::string base64_chars;
    static inline bool is_base64(unsigned char c);
};

// 全局函数接口，保持与原有代码兼容
std::string base64_encode(const unsigned char* data, size_t length);
std::string base64_encode(const std::string& input);
std::vector<unsigned char> base64_decode(const std::string& encoded);

#endif // BASE64_H