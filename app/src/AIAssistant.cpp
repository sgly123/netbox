#include "AIAssistant.h"
#include "base/Logger.h"
#include <curl/curl.h>
#include <sstream>
#include <thread>
#include <algorithm>

// JSON简单解析（生产环境建议使用nlohmann/json库）
static std::string escapeJSON(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

// libcurl回调函数
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

AIAssistant::AIAssistant(const Config& config) : config_(config) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Logger::info("[AIAssistant] AI助手初始化完成");
    Logger::info("[AIAssistant] API URL: " + config_.api_url);
    Logger::info("[AIAssistant] Model: " + config_.model);
}

AIAssistant::~AIAssistant() {
    curl_global_cleanup();
}

void AIAssistant::sendMessage(uint32_t user_id, const std::string& message, ResponseCallback callback) {
    if (!config_.enabled) {
        callback("", false, "AI助手未启用");
        return;
    }
    
    // 检查限流
    if (!checkRateLimit(user_id)) {
        callback("", false, "请求过于频繁，请稍后再试");
        Logger::warn("[AIAssistant] 用户 " + std::to_string(user_id) + " 触发限流");
        return;
    }
    
    // 异步处理（避免阻塞主线程）
    std::thread([this, user_id, message, callback]() {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // 获取或创建用户对话历史
            auto& history = user_histories_[user_id];
            
            // 首次对话，添加系统提示
            if (history.empty() && !config_.system_prompt.empty()) {
                history.push_back({"system", config_.system_prompt});
            }
            
            // 添加用户消息
            history.push_back({"user", message});
            
            // 限制历史长度（保留最近10条对话）
            if (history.size() > 21) {  // system + 10对话 = 21条
                history.erase(history.begin() + 1, history.begin() + 3);  // 删除最旧的一对对话
            }
            
            // 调用API
            std::string response = callAPI(history);
            
            if (!response.empty()) {
                // 添加AI回复到历史
                history.push_back({"assistant", response});
                callback(response, true, "");
                Logger::info("[AIAssistant] 用户 " + std::to_string(user_id) + " 收到AI回复");
            } else {
                callback("", false, "AI服务暂时不可用，请稍后再试");
                Logger::error("[AIAssistant] API调用失败");
            }
            
        } catch (const std::exception& e) {
            callback("", false, "处理请求时发生错误");
            Logger::error("[AIAssistant] 异常: " + std::string(e.what()));
        }
    }).detach();
}

void AIAssistant::clearHistory(uint32_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_histories_.erase(user_id);
    Logger::info("[AIAssistant] 清除用户 " + std::to_string(user_id) + " 的对话历史");
}

bool AIAssistant::checkRateLimit(uint32_t user_id) {
    if (!config_.rate_limit_enabled) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto& info = rate_limits_[user_id];
    auto now = std::chrono::steady_clock::now();
    
    // 清理过期的请求记录
    while (!info.requests.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - info.requests.front()).count();
        if (elapsed > 86400) {  // 24小时
            info.requests.pop_front();
        } else {
            break;
        }
    }
    
    // 检查各时间段的限制
    int count_minute = 0, count_hour = 0, count_day = 0;
    for (const auto& req_time : info.requests) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - req_time).count();
        if (elapsed < 60) count_minute++;
        if (elapsed < 3600) count_hour++;
        if (elapsed < 86400) count_day++;
    }
    
    if (count_minute >= config_.max_requests_per_minute ||
        count_hour >= config_.max_requests_per_hour ||
        count_day >= config_.max_requests_per_day) {
        return false;
    }
    
    // 记录本次请求
    info.requests.push_back(now);
    return true;
}

std::string AIAssistant::callAPI(const std::vector<Message>& messages) {
    std::string request_json = buildRequestJSON(messages);
    std::string response = httpPost(config_.api_url, request_json);
    return parseResponseJSON(response);
}

std::string AIAssistant::buildRequestJSON(const std::vector<Message>& messages) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"model\":\"" << config_.model << "\",";
    oss << "\"messages\":[";
    
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) oss << ",";
        oss << "{";
        oss << "\"role\":\"" << messages[i].role << "\",";
        oss << "\"content\":\"" << escapeJSON(messages[i].content) << "\"";
        oss << "}";
    }
    
    oss << "],";
    oss << "\"max_tokens\":" << config_.max_tokens << ",";
    oss << "\"temperature\":" << config_.temperature;
    oss << "}";
    
    return oss.str();
}

std::string AIAssistant::parseResponseJSON(const std::string& response) {
    if (response.empty()) {
        return "";
    }
    
    // 简单的JSON解析（查找content字段）
    size_t content_pos = response.find("\"content\"");
    if (content_pos == std::string::npos) {
        Logger::error("[AIAssistant] 响应中未找到content字段");
        return "";
    }
    
    size_t start = response.find("\"", content_pos + 9);
    if (start == std::string::npos) {
        return "";
    }
    start++;
    
    size_t end = start;
    while (end < response.length()) {
        if (response[end] == '\"' && response[end - 1] != '\\') {
            break;
        }
        end++;
    }
    
    std::string content = response.substr(start, end - start);
    
    // 反转义JSON字符串
    std::string result;
    for (size_t i = 0; i < content.length(); ++i) {
        if (content[i] == '\\' && i + 1 < content.length()) {
            switch (content[i + 1]) {
                case 'n': result += '\n'; i++; break;
                case 'r': result += '\r'; i++; break;
                case 't': result += '\t'; i++; break;
                case '\"': result += '\"'; i++; break;
                case '\\': result += '\\'; i++; break;
                case '/': result += '/'; i++; break;
                case 'b': result += '\b'; i++; break;
                case 'f': result += '\f'; i++; break;
                case 'u': {
                    // Unicode转义 \uXXXX
                    if (i + 5 < content.length()) {
                        std::string hex = content.substr(i + 2, 4);
                        try {
                            int codepoint = std::stoi(hex, nullptr, 16);
                            // 简单处理：只处理ASCII范围
                            if (codepoint < 128) {
                                result += static_cast<char>(codepoint);
                            } else {
                                // 保留原始转义序列（完整UTF-8处理较复杂）
                                result += "\\u" + hex;
                            }
                            i += 5;
                        } catch (...) {
                            result += content[i];
                        }
                    } else {
                        result += content[i];
                    }
                    break;
                }
                default: 
                    // 未知转义，保留反斜杠
                    result += content[i]; 
                    break;
            }
        } else {
            result += content[i];
        }
    }
    
    return result;
}

std::string AIAssistant::httpPost(const std::string& url, const std::string& data) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        Logger::error("[AIAssistant] curl初始化失败");
        return "";
    }
    
    std::string response;
    struct curl_slist* headers = nullptr;
    
    // 设置请求头
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + config_.api_key;
    headers = curl_slist_append(headers, auth_header.c_str());
    
    // 配置curl
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        Logger::error("[AIAssistant] HTTP请求失败: " + std::string(curl_easy_strerror(res)));
        response.clear();
    }
    
    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return response;
}
