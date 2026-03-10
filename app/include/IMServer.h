#pragma once

#include "ApplicationServer.h"
#include "IMProtocol.h"
#include "AIAssistant.h"
#include "util/EnhancedConfigReader.h"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <string>

/**
 * @brief 即时通讯服务器
 * 
 * 功能：
 * 1. 用户认证与会话管理
 * 2. 好友关系管理
 * 3. 点对点消息转发
 * 4. 群组消息广播
 * 5. 离线消息存储
 * 6. 文件传输支持
 */
class IMServer : public ApplicationServer {
public:
    // 用户信息结构
    struct UserInfo {
        uint32_t user_id;
        std::string username;
        std::string password;  // 用户密码（生产环境应该加密存储）
        std::string nickname;
        int client_fd;
        bool online;
        uint64_t last_active;
        
        UserInfo() : user_id(0), client_fd(-1), online(false), last_active(0) {}
    };
    
    // 群组信息结构
    struct GroupInfo {
        uint32_t group_id;
        std::string group_name;
        uint32_t owner_id;
        std::unordered_set<uint32_t> members;
        uint64_t create_time;
    };
    
    IMServer(const std::string& ip, int port, IOMultiplexer::IOType io_type, EnhancedConfigReader* config = nullptr);
    ~IMServer() override;
    
    bool start() override;
    void stop() override;

protected:
    void initializeProtocolRouter() override;
    void onClientConnected(int clientFd) override;
    void onClientDisconnected(int clientFd) override;
    void onPacketReceived(const std::vector<char>& packet);
    void onProtocolPacket(uint32_t protoId, const std::vector<char>& packet) override;
    
    // 实现ApplicationServer的纯虚函数
    std::string handleHttpRequest(const std::string& request, int clientFd) override;
    std::string handleBusinessLogic(const std::string& command, const std::vector<std::string>& args) override;
    bool parseRequestPath(const std::string& path, std::string& command, std::vector<std::string>& args) override;

private:
    // 消息处理函数
    void handleLoginRequest(int clientFd, const std::string& payload);
    void handleRegisterRequest(int clientFd, const std::string& payload);
    void handleLogoutRequest(int clientFd);
    void handleFriendListRequest(int clientFd);
    void handleAddFriendRequest(int clientFd, const std::string& payload);
    void handleDeleteFriendRequest(int clientFd, const std::string& payload);
    void handleChatMessage(int clientFd, const std::string& payload);
    void handleGroupMessage(int clientFd, const std::string& payload);
    void handleCreateGroupRequest(int clientFd, const std::string& payload);
    void handleFileUploadRequest(int clientFd, const std::string& payload);
    
    // 工具函数
    void sendResponse(int clientFd, IMProtocol::MessageType type, const std::string& payload);
    void broadcastToGroup(uint32_t group_id, const std::string& message, uint32_t exclude_user = 0);
    void storeOfflineMessage(uint32_t user_id, const std::string& message);
    uint32_t getUserIdByFd(int clientFd);
    int getFdByUserId(uint32_t user_id);
    bool isUserOnline(uint32_t user_id);
    
    // 数据存储（简化版，实际应使用数据库）
    std::unordered_map<uint32_t, UserInfo> users_;           // user_id -> UserInfo
    std::unordered_map<std::string, uint32_t> username_map_; // username -> user_id
    std::unordered_map<int, uint32_t> fd_to_user_;           // client_fd -> user_id
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> friends_; // user_id -> friend_ids
    std::unordered_map<uint32_t, GroupInfo> groups_;         // group_id -> GroupInfo
    std::unordered_map<uint32_t, std::vector<std::string>> offline_messages_; // user_id -> messages
    
    std::mutex data_mutex_;
    uint32_t next_user_id_;
    uint32_t next_group_id_;
    
    // AI助手
    std::unique_ptr<AIAssistant> ai_assistant_;
    uint32_t ai_user_id_ = 999;  // AI助手的用户ID
    EnhancedConfigReader* config_;  // 配置对象
    
    // AI助手相关方法
    void initializeAIAssistant();
    void handleAIMessage(uint32_t from_user_id, const std::string& message, int clientFd);
    void sendAIResponse(uint32_t to_user_id, const std::string& response);
};
