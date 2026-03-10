#ifndef AI_ASSISTANT_H
#define AI_ASSISTANT_H

#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <deque>

/**
 * @brief AI助手模块
 * 
 * 功能：
 * 1. 调用AI API（OpenAI/Claude等）
 * 2. 管理对话上下文
 * 3. 限流控制
 * 4. 异步处理
 */
class AIAssistant {
public:
    struct Config {
        bool enabled = true;
        std::string api_key;
        std::string api_url;
        std::string model;
        std::string system_prompt;
        int max_tokens = 1000;
        float temperature = 0.7f;
        int timeout = 30;
        
        // 限流配置
        bool rate_limit_enabled = true;
        int max_requests_per_minute = 10;
        int max_requests_per_hour = 100;
        int max_requests_per_day = 500;
    };
    
    using ResponseCallback = std::function<void(const std::string& response, bool success, const std::string& error)>;
    
    AIAssistant(const Config& config);
    ~AIAssistant();
    
    /**
     * @brief 发送消息给AI（异步）
     * @param user_id 用户ID
     * @param message 用户消息
     * @param callback 回调函数
     */
    void sendMessage(uint32_t user_id, const std::string& message, ResponseCallback callback);
    
    /**
     * @brief 清除用户的对话历史
     * @param user_id 用户ID
     */
    void clearHistory(uint32_t user_id);
    
    /**
     * @brief 检查是否启用
     */
    bool isEnabled() const { return config_.enabled; }
    
private:
    struct Message {
        std::string role;     // "system", "user", "assistant"
        std::string content;
    };
    
    struct RateLimitInfo {
        std::deque<std::chrono::steady_clock::time_point> requests;
    };
    
    /**
     * @brief 调用AI API（同步）
     * @param messages 消息历史
     * @return AI回复
     */
    std::string callAPI(const std::vector<Message>& messages);
    
    /**
     * @brief 检查限流
     * @param user_id 用户ID
     * @return 是否允许请求
     */
    bool checkRateLimit(uint32_t user_id);
    
    /**
     * @brief 构建请求JSON
     */
    std::string buildRequestJSON(const std::vector<Message>& messages);
    
    /**
     * @brief 解析响应JSON
     */
    std::string parseResponseJSON(const std::string& response);
    
    /**
     * @brief HTTP POST请求
     */
    std::string httpPost(const std::string& url, const std::string& data);
    
    Config config_;
    std::mutex mutex_;
    
    // 每个用户的对话历史（最多保留10条）
    std::unordered_map<uint32_t, std::vector<Message>> user_histories_;
    
    // 限流信息
    std::unordered_map<uint32_t, RateLimitInfo> rate_limits_;
};

#endif // AI_ASSISTANT_H
