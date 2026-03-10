#include "IMServer.h"
// #include "AIAssistant.h"  // 暂时禁用 AI 助手
#include "base/Logger.h"
#include <chrono>
#include <sstream>

IMServer::IMServer(const std::string& ip, int port, IOMultiplexer::IOType io_type, EnhancedConfigReader* config)
    : ApplicationServer(ip, port, io_type, nullptr), next_user_id_(1000), next_group_id_(1), config_(config) {
    
    Logger::info("[IMServer] Constructor called, config pointer: " + std::string(config ? "valid" : "null"));
    
    // 初始化测试用户
    UserInfo admin;
    admin.user_id = 1;
    admin.username = "admin";
    admin.password = "123456";  // 明文密码（生产环境应该加密）
    admin.nickname = "管理员";
    admin.online = false;
    users_[1] = admin;
    username_map_["admin"] = 1;
    
    // 添加测试用户 bobs
    UserInfo bobs;
    bobs.user_id = 2;
    bobs.username = "bobs";
    bobs.password = "123456";  // 明文密码（生产环境应该加密）
    bobs.nickname = "Bobs";
    bobs.online = false;
    users_[2] = bobs;
    username_map_["bobs"] = 2;
    
    // 初始化AI助手
    initializeAIAssistant();
    
    Logger::info("[IMServer] 即时通讯服务器初始化完成");
}

IMServer::~IMServer() {
    stop();
}

bool IMServer::start() {
    Logger::info("[IMServer] 启动即时通讯服务器...");
    return ApplicationServer::start();
}

void IMServer::stop() {
    Logger::info("[IMServer] 停止即时通讯服务器...");
    ApplicationServer::stop();
}

void IMServer::initializeProtocolRouter() {
    // 确保router已初始化
    if (!m_router) {
        Logger::error("[IMServer] ProtocolRouter未初始化");
        return;
    }
    
    // 注册IM协议
    auto imProtocol = std::make_shared<IMProtocol>();
    imProtocol->setOnPacketReceived([this](const std::vector<char>& packet) {
        this->onPacketReceived(packet);
    });
    
    m_router->registerProtocol(IMProtocol::PROTOCOL_MAGIC, imProtocol);
    Logger::info("[IMServer] IM协议注册成功");
}

std::string IMServer::handleHttpRequest(const std::string&, int) {
    return "{\"error\":\"IM server does not support HTTP\"}";
}

std::string IMServer::handleBusinessLogic(const std::string&, const std::vector<std::string>&) {
    return "{\"error\":\"Not implemented\"}";
}

bool IMServer::parseRequestPath(const std::string&, std::string&, std::vector<std::string>&) {
    return false;
}

void IMServer::onClientConnected(int clientFd) {
    Logger::info("[IMServer] 客户端连接: FD=" + std::to_string(clientFd));
}

void IMServer::onClientDisconnected(int clientFd) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = fd_to_user_.find(clientFd);
    if (it != fd_to_user_.end()) {
        uint32_t user_id = it->second;
        
        if (users_.find(user_id) != users_.end()) {
            users_[user_id].online = false;
            users_[user_id].client_fd = -1;
            Logger::info("[IMServer] 用户下线: " + users_[user_id].username);
        }
        
        fd_to_user_.erase(it);
    }
}

void IMServer::onProtocolPacket(uint32_t protoId, const std::vector<char>& packet) {
    Logger::debug("[IMServer] onProtocolPacket 被调用，协议ID: " + std::to_string(protoId));
    
    // 只处理 IM 协议
    if (protoId == IMProtocol::PROTOCOL_MAGIC) {
        onPacketReceived(packet);
    } else {
        // 其他协议交给父类处理
        ApplicationServer::onProtocolPacket(protoId, packet);
    }
}

void IMServer::onPacketReceived(const std::vector<char>& packet) {
    if (packet.size() < sizeof(IMProtocol::ProtocolHeader)) {
        Logger::error("[IMServer] 数据包太小");
        return;
    }
    
    // 解析协议头
    IMProtocol::ProtocolHeader header;
    std::memcpy(&header, packet.data(), sizeof(header));
    header.msg_type = ntohs(header.msg_type);
    
    // 提取payload
    std::string payload(packet.begin() + sizeof(IMProtocol::ProtocolHeader), packet.end());
    
    int clientFd = m_currentClientFd;
    
    IMProtocol::MessageType type = static_cast<IMProtocol::MessageType>(header.msg_type);
    
    Logger::debug("[IMServer] 收到消息: " + IMProtocol::getMessageTypeName(type));
    
    switch (type) {
        case IMProtocol::MessageType::LOGIN_REQUEST:
            handleLoginRequest(clientFd, payload);
            break;
        case IMProtocol::MessageType::REGISTER_REQUEST:
            handleRegisterRequest(clientFd, payload);
            break;
        case IMProtocol::MessageType::LOGOUT_REQUEST:
            handleLogoutRequest(clientFd);
            break;
        case IMProtocol::MessageType::FRIEND_LIST_REQUEST:
            handleFriendListRequest(clientFd);
            break;
        case IMProtocol::MessageType::ADD_FRIEND_REQUEST:
            handleAddFriendRequest(clientFd, payload);
            break;
        case IMProtocol::MessageType::DELETE_FRIEND_REQUEST:
            handleDeleteFriendRequest(clientFd, payload);
            break;
        case IMProtocol::MessageType::CHAT_MESSAGE:
            handleChatMessage(clientFd, payload);
            break;
        case IMProtocol::MessageType::GROUP_MESSAGE:
            handleGroupMessage(clientFd, payload);
            break;
        case IMProtocol::MessageType::CREATE_GROUP_REQUEST:
            handleCreateGroupRequest(clientFd, payload);
            break;
        case IMProtocol::MessageType::HEARTBEAT:
            sendResponse(clientFd, IMProtocol::MessageType::HEARTBEAT, "{}");
            break;
        default:
            Logger::warn("[IMServer] 未实现的消息类型: " + std::to_string(header.msg_type));
            break;
    }
}

void IMServer::handleLoginRequest(int clientFd, const std::string& payload) {
    Logger::info("[IMServer] 处理登录请求，客户端FD: " + std::to_string(clientFd));
    Logger::debug("[IMServer] 登录payload: " + payload);
    
    // 简单解析JSON获取用户名和密码
    size_t user_pos = payload.find("\"username\"");
    size_t pass_pos = payload.find("\"password\"");
    
    if (user_pos == std::string::npos || pass_pos == std::string::npos) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE, 
                    "{\"success\":false,\"message\":\"Invalid request\"}");
        return;
    }
    
    // 提取用户名
    size_t start = payload.find(":", user_pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    size_t end = start;
    while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
    std::string username = payload.substr(start, end - start);
    
    // 提取密码
    start = payload.find(":", pass_pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    end = start;
    while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
    std::string password = payload.substr(start, end - start);
    
    Logger::debug("[IMServer] 登录用户名: " + username);
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // 查找用户
    auto it = username_map_.find(username);
    if (it == username_map_.end()) {
        sendResponse(clientFd, IMProtocol::MessageType::LOGIN_RESPONSE,
                    "{\"success\":false,\"message\":\"用户不存在\"}");
        return;
    }
    
    uint32_t user_id = it->second;
    UserInfo& user = users_[user_id];
    
    // 验证密码
    if (!user.password.empty() && user.password != password) {
        sendResponse(clientFd, IMProtocol::MessageType::LOGIN_RESPONSE,
                    "{\"success\":false,\"message\":\"密码错误\"}");
        Logger::warn("[IMServer] 用户 " + username + " 密码错误");
        return;
    }
    
    // 设置在线状态
    user.online = true;
    user.client_fd = clientFd;
    
    fd_to_user_[clientFd] = user_id;
    
    // 自动添加AI助手为好友（如果还不是好友）
    if (friends_[user_id].find(ai_user_id_) == friends_[user_id].end()) {
        friends_[user_id].insert(ai_user_id_);
        friends_[ai_user_id_].insert(user_id);
        Logger::info("[IMServer] 自动添加AI助手为用户 " + std::to_string(user_id) + " 的好友");
    }
    
    std::ostringstream oss;
    oss << "{\"success\":true,\"user_id\":" << user_id 
        << ",\"username\":\"" << user.username << "\""
        << ",\"nickname\":\"" << user.nickname << "\"}";
    
    sendResponse(clientFd, IMProtocol::MessageType::LOGIN_RESPONSE, oss.str());
    Logger::info("[IMServer] 用户登录成功: " + user.username + ", 响应已发送");
}

void IMServer::handleRegisterRequest(int clientFd, const std::string& payload) {
    Logger::info("[IMServer] 处理注册请求，客户端FD: " + std::to_string(clientFd));
    Logger::debug("[IMServer] 注册payload: " + payload);
    
    // 解析用户名、密码、昵称
    size_t user_pos = payload.find("\"username\"");
    size_t pass_pos = payload.find("\"password\"");
    size_t nick_pos = payload.find("\"nickname\"");
    
    if (user_pos == std::string::npos) {
        sendResponse(clientFd, IMProtocol::MessageType::REGISTER_RESPONSE,
                    "{\"success\":false,\"message\":\"无效的请求\"}");
        return;
    }
    
    // 提取用户名
    size_t start = payload.find(":", user_pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    size_t end = start;
    while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
    std::string username = payload.substr(start, end - start);
    
    // 提取密码
    std::string password;
    if (pass_pos != std::string::npos) {
        start = payload.find(":", pass_pos) + 1;
        while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
        end = start;
        while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
        password = payload.substr(start, end - start);
    }
    
    // 提取昵称
    std::string nickname = username;  // 默认使用用户名
    if (nick_pos != std::string::npos) {
        start = payload.find(":", nick_pos) + 1;
        while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
        end = start;
        while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
        nickname = payload.substr(start, end - start);
    }
    
    Logger::debug("[IMServer] 注册用户名: " + username + ", 昵称: " + nickname);
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // 检查用户名是否已存在
    if (username_map_.find(username) != username_map_.end()) {
        sendResponse(clientFd, IMProtocol::MessageType::REGISTER_RESPONSE,
                    "{\"success\":false,\"message\":\"用户名已存在\"}");
        return;
    }
    
    // 创建新用户
    uint32_t user_id = next_user_id_++;
    UserInfo newUser;
    newUser.user_id = user_id;
    newUser.username = username;
    newUser.password = password;  // 保存密码
    newUser.nickname = nickname;
    newUser.online = false;
    newUser.client_fd = -1;
    
    users_[user_id] = newUser;
    username_map_[username] = user_id;
    
    std::ostringstream oss;
    oss << "{\"success\":true,\"user_id\":" << user_id
        << ",\"message\":\"注册成功\"}";
    
    sendResponse(clientFd, IMProtocol::MessageType::REGISTER_RESPONSE, oss.str());
    Logger::info("[IMServer] 注册成功: " + username + " (ID: " + std::to_string(user_id) + ")");
    Logger::info("[IMServer] 注册响应已发送");
}

void IMServer::handleLogoutRequest(int clientFd) {
    Logger::info("[IMServer] 处理登出请求，客户端FD: " + std::to_string(clientFd));
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    auto it = fd_to_user_.find(clientFd);
    if (it != fd_to_user_.end()) {
        uint32_t user_id = it->second;
        if (users_.find(user_id) != users_.end()) {
            users_[user_id].online = false;
            users_[user_id].client_fd = -1;
            Logger::info("[IMServer] 用户登出: " + users_[user_id].username);
        }
        fd_to_user_.erase(it);
    }
    
    sendResponse(clientFd, IMProtocol::MessageType::LOGOUT_RESPONSE,
                "{\"success\":true,\"message\":\"登出成功\"}");
}

void IMServer::handleFriendListRequest(int clientFd) {
    Logger::info("[IMServer] 处理好友列表请求，客户端FD: " + std::to_string(clientFd));
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    uint32_t user_id = getUserIdByFd(clientFd);
    if (user_id == 0) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"未登录\"}");
        return;
    }
    
    // 构建好友列表JSON
    std::ostringstream oss;
    oss << "{\"success\":true,\"friends\":[";
    
    auto it = friends_.find(user_id);
    if (it != friends_.end()) {
        bool first = true;
        for (uint32_t friend_id : it->second) {
            if (users_.find(friend_id) != users_.end()) {
                if (!first) oss << ",";
                first = false;
                
                const UserInfo& friendInfo = users_[friend_id];
                oss << "{\"user_id\":" << friend_id
                    << ",\"username\":\"" << friendInfo.username << "\""
                    << ",\"nickname\":\"" << friendInfo.nickname << "\""
                    << ",\"online\":" << (friendInfo.online ? "true" : "false") << "}";
            }
        }
    }
    
    oss << "]}";
    sendResponse(clientFd, IMProtocol::MessageType::FRIEND_LIST_RESPONSE, oss.str());
    Logger::info("[IMServer] 好友列表响应已发送");
}

void IMServer::handleAddFriendRequest(int clientFd, const std::string& payload) {
    Logger::info("[IMServer] 处理添加好友请求，客户端FD: " + std::to_string(clientFd));
    Logger::debug("[IMServer] 添加好友payload: " + payload);
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    uint32_t user_id = getUserIdByFd(clientFd);
    if (user_id == 0) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"未登录\"}");
        return;
    }
    
    // 简单解析JSON获取好友用户名（支持 "username" 或 "friend_username"）
    size_t pos = payload.find("\"username\"");
    if (pos == std::string::npos) {
        pos = payload.find("\"friend_username\"");
    }
    
    if (pos == std::string::npos) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"无效的请求\"}");
        return;
    }
    
    // 提取用户名（简化版，实际应使用JSON库）
    size_t start = payload.find(":", pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    size_t end = start;
    while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
    std::string friend_username = payload.substr(start, end - start);
    
    Logger::debug("[IMServer] 提取的好友用户名: " + friend_username);
    
    // 查找好友
    auto it = username_map_.find(friend_username);
    if (it == username_map_.end()) {
        sendResponse(clientFd, IMProtocol::MessageType::ADD_FRIEND_RESPONSE,
                    "{\"success\":false,\"message\":\"用户不存在\"}");
        return;
    }
    
    uint32_t friend_id = it->second;
    
    // 不能添加自己为好友
    if (friend_id == user_id) {
        sendResponse(clientFd, IMProtocol::MessageType::ADD_FRIEND_RESPONSE,
                    "{\"success\":false,\"message\":\"不能添加自己为好友\"}");
        return;
    }
    
    // 添加好友关系（双向）
    friends_[user_id].insert(friend_id);
    friends_[friend_id].insert(user_id);
    
    const UserInfo& friendInfo = users_[friend_id];
    std::ostringstream oss;
    oss << "{\"success\":true"
        << ",\"user_id\":" << friend_id
        << ",\"username\":\"" << friendInfo.username << "\""
        << ",\"nickname\":\"" << friendInfo.nickname << "\""
        << ",\"message\":\"添加好友成功\"}";
    
    sendResponse(clientFd, IMProtocol::MessageType::ADD_FRIEND_RESPONSE, oss.str());
    Logger::info("[IMServer] 添加好友成功: " + friend_username);
}

void IMServer::handleDeleteFriendRequest(int clientFd, const std::string& payload) {
    Logger::info("[IMServer] 处理删除好友请求，客户端FD: " + std::to_string(clientFd));
    Logger::debug("[IMServer] 删除好友payload: " + payload);
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    uint32_t user_id = getUserIdByFd(clientFd);
    if (user_id == 0) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"未登录\"}");
        return;
    }
    
    // 解析JSON获取好友ID或用户名
    size_t id_pos = payload.find("\"friend_id\"");
    size_t name_pos = payload.find("\"friend_username\"");
    
    uint32_t friend_id = 0;
    
    // 优先使用friend_id
    if (id_pos != std::string::npos) {
        size_t start = payload.find(":", id_pos) + 1;
        while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
        size_t end = start;
        while (end < payload.length() && payload[end] >= '0' && payload[end] <= '9') end++;
        friend_id = std::stoi(payload.substr(start, end - start));
    }
    // 如果没有friend_id，使用friend_username
    else if (name_pos != std::string::npos) {
        size_t start = payload.find(":", name_pos) + 1;
        while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
        size_t end = start;
        while (end < payload.length() && payload[end] != '\"' && payload[end] != ',' && payload[end] != '}') end++;
        std::string friend_username = payload.substr(start, end - start);
        
        auto it = username_map_.find(friend_username);
        if (it == username_map_.end()) {
            sendResponse(clientFd, IMProtocol::MessageType::DELETE_FRIEND_RESPONSE,
                        "{\"success\":false,\"message\":\"用户不存在\"}");
            return;
        }
        friend_id = it->second;
    } else {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"无效的请求\"}");
        return;
    }
    
    // ==================== 禁止删除AI助手 ====================
    if (friend_id == ai_user_id_) {
        sendResponse(clientFd, IMProtocol::MessageType::DELETE_FRIEND_RESPONSE,
                    "{\"success\":false,\"message\":\"AI助手无法删除\"}");
        Logger::warn("[IMServer] 用户 " + std::to_string(user_id) + " 尝试删除AI助手");
        return;
    }
    
    // 检查好友关系是否存在
    if (friends_[user_id].find(friend_id) == friends_[user_id].end()) {
        sendResponse(clientFd, IMProtocol::MessageType::DELETE_FRIEND_RESPONSE,
                    "{\"success\":false,\"message\":\"不是好友关系\"}");
        return;
    }
    
    // 删除好友关系（双向）
    friends_[user_id].erase(friend_id);
    friends_[friend_id].erase(user_id);
    
    std::ostringstream oss;
    oss << "{\"success\":true"
        << ",\"friend_id\":" << friend_id
        << ",\"message\":\"删除好友成功\"}";
    
    sendResponse(clientFd, IMProtocol::MessageType::DELETE_FRIEND_RESPONSE, oss.str());
    Logger::info("[IMServer] 删除好友成功: user_id=" + std::to_string(user_id) + 
                " 删除了 friend_id=" + std::to_string(friend_id));
}

void IMServer::handleChatMessage(int clientFd, const std::string& payload) {
    Logger::info("[IMServer] 收到聊天消息，客户端FD: " + std::to_string(clientFd));
    Logger::debug("[IMServer] 消息payload: " + payload);
    
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // 获取发送者ID
    uint32_t from_user_id = getUserIdByFd(clientFd);
    if (from_user_id == 0) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"未登录\"}");
        return;
    }
    
    // 解析目标用户ID和消息内容
    size_t to_pos = payload.find("\"to_user_id\"");
    size_t content_pos = payload.find("\"content\"");
    
    if (to_pos == std::string::npos || content_pos == std::string::npos) {
        sendResponse(clientFd, IMProtocol::MessageType::ERROR_RESPONSE,
                    "{\"success\":false,\"message\":\"无效的消息格式\"}");
        return;
    }
    
    // 提取目标用户ID
    size_t start = payload.find(":", to_pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    size_t end = start;
    while (end < payload.length() && payload[end] >= '0' && payload[end] <= '9') end++;
    uint32_t to_user_id = std::stoi(payload.substr(start, end - start));
    
    // 提取消息内容
    start = payload.find(":", content_pos) + 1;
    while (start < payload.length() && (payload[start] == ' ' || payload[start] == '\"')) start++;
    end = payload.find_last_of('\"');
    std::string content = payload.substr(start, end - start);
    
    Logger::info("[IMServer] 消息: " + std::to_string(from_user_id) + " -> " + 
                std::to_string(to_user_id) + ": " + content);
    
    // ==================== 拦截发给AI助手的消息 ====================
    if (to_user_id == ai_user_id_) {
        handleAIMessage(from_user_id, content, clientFd);
        return;
    }
    
    // 检查目标用户是否存在
    if (users_.find(to_user_id) == users_.end()) {
        sendResponse(clientFd, IMProtocol::MessageType::CHAT_MESSAGE_ACK,
                    "{\"success\":false,\"delivered\":false,\"message\":\"用户不存在\"}");
        return;
    }
    
    // 构建转发消息
    const UserInfo& fromUser = users_[from_user_id];
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "{\"from_user_id\":" << from_user_id
        << ",\"from_username\":\"" << fromUser.username << "\""
        << ",\"to_user_id\":" << to_user_id
        << ",\"content\":\"" << content << "\""
        << ",\"timestamp\":" << timestamp << "}";
    
    // 检查目标用户是否在线
    if (isUserOnline(to_user_id)) {
        // 在线，直接转发
        int target_fd = getFdByUserId(to_user_id);
        sendResponse(target_fd, IMProtocol::MessageType::CHAT_MESSAGE, oss.str());
        
        // 发送ACK给发送者
        sendResponse(clientFd, IMProtocol::MessageType::CHAT_MESSAGE_ACK,
                    "{\"success\":true,\"delivered\":true}");
        
        Logger::info("[IMServer] 消息已转发给在线用户 " + std::to_string(to_user_id));
    } else {
        // 离线，存储离线消息
        storeOfflineMessage(to_user_id, oss.str());
        
        // 发送ACK给发送者
        sendResponse(clientFd, IMProtocol::MessageType::CHAT_MESSAGE_ACK,
                    "{\"success\":true,\"delivered\":false,\"message\":\"用户离线，消息已存储\"}");
        
        Logger::info("[IMServer] 用户 " + std::to_string(to_user_id) + " 离线，消息已存储");
    }
}

void IMServer::handleGroupMessage(int, const std::string&) {
    // 实现群消息
}

void IMServer::handleCreateGroupRequest(int, const std::string&) {
    // 实现创建群组
}

void IMServer::handleFileUploadRequest(int, const std::string&) {
    // 实现文件上传
}

void IMServer::sendResponse(int clientFd, IMProtocol::MessageType type, const std::string& payload) {
    // 构建响应包：[ProtocolID 4字节][Type 2字节][Length 4字节][Payload]
    uint32_t protocolId = htonl(IMProtocol::PROTOCOL_MAGIC);
    uint16_t msgType = htons(static_cast<uint16_t>(type));
    uint32_t length = htonl(static_cast<uint32_t>(payload.size()));
    
    std::vector<char> packet;
    packet.resize(4 + 2 + 4 + payload.size());
    
    size_t offset = 0;
    std::memcpy(packet.data() + offset, &protocolId, 4); offset += 4;
    std::memcpy(packet.data() + offset, &msgType, 2); offset += 2;
    std::memcpy(packet.data() + offset, &length, 4); offset += 4;
    std::memcpy(packet.data() + offset, payload.data(), payload.size());
    
    sendBusinessData(clientFd, std::string(packet.begin(), packet.end()));
    
    Logger::debug("[IMServer] 发送响应: " + IMProtocol::getMessageTypeName(type) + 
                 ", payload长度: " + std::to_string(payload.size()) +
                 ", 总长度: " + std::to_string(packet.size()));
}

void IMServer::broadcastToGroup(uint32_t, const std::string&, uint32_t) {
    // 实现群组广播
}

void IMServer::storeOfflineMessage(uint32_t user_id, const std::string& message) {
    offline_messages_[user_id].push_back(message);
}

uint32_t IMServer::getUserIdByFd(int clientFd) {
    auto it = fd_to_user_.find(clientFd);
    return (it != fd_to_user_.end()) ? it->second : 0;
}

int IMServer::getFdByUserId(uint32_t user_id) {
    if (users_.find(user_id) != users_.end() && users_[user_id].online) {
        return users_[user_id].client_fd;
    }
    return -1;
}

bool IMServer::isUserOnline(uint32_t user_id) {
    return users_.find(user_id) != users_.end() && users_[user_id].online;
}


// ==================== AI助手功能 ====================

void IMServer::initializeAIAssistant() {
    // 从配置读取AI设置
    AIAssistant::Config ai_config;
    
    Logger::info("[IMServer] initializeAIAssistant called, config_: " + std::string(config_ ? "valid" : "null"));
    
    if (config_) {
        Logger::info("[IMServer] Reading AI settings from config file...");
        
        ai_config.enabled = config_->getBool("ai_assistant.enabled", true);
        ai_config.api_key = config_->getString("ai_assistant.api_key", "");
        ai_config.api_url = config_->getString("ai_assistant.api_url", "https://api.openai.com/v1/chat/completions");
        ai_config.model = config_->getString("ai_assistant.model", "gpt-3.5-turbo");
        ai_config.system_prompt = config_->getString("ai_assistant.system_prompt", "你是一个友好的AI助手，帮助用户解答问题。请用简洁、友好的语言回复。");
        ai_config.max_tokens = config_->getInt("ai_assistant.max_tokens", 1000);
        ai_config.temperature = static_cast<float>(config_->getDouble("ai_assistant.temperature", 0.7));
        ai_config.timeout = config_->getInt("ai_assistant.timeout", 30);
        
        // 限流配置
        ai_config.rate_limit_enabled = config_->getBool("ai_assistant.rate_limit_enabled", true);
        ai_config.max_requests_per_minute = config_->getInt("ai_assistant.max_requests_per_minute", 10);
        ai_config.max_requests_per_hour = config_->getInt("ai_assistant.max_requests_per_hour", 100);
        ai_config.max_requests_per_day = config_->getInt("ai_assistant.max_requests_per_day", 500);
        
        // 可选：从配置读取AI用户ID
        ai_user_id_ = config_->getInt("ai_assistant.user_id", 999);
        
        Logger::info("[IMServer] AI config loaded:");
        Logger::info("  - Enabled: " + std::string(ai_config.enabled ? "yes" : "no"));
        Logger::info("  - API URL: " + ai_config.api_url);
        Logger::info("  - Model: " + ai_config.model);
        Logger::info(std::string("  - API Key: ") + (ai_config.api_key.empty() ? "not configured" : "configured"));
    } else {
        // 使用默认值
        Logger::warn("[IMServer] No config object provided, using default AI settings");
        ai_config.enabled = true;
        ai_config.api_key = "";
        ai_config.api_url = "https://api.openai.com/v1/chat/completions";
        ai_config.model = "gpt-3.5-turbo";
        ai_config.system_prompt = "你是一个友好的AI助手，帮助用户解答问题。请用简洁、友好的语言回复。";
        ai_config.max_tokens = 1000;
        ai_config.temperature = 0.7f;
        ai_config.timeout = 30;
    }
    
    // 创建AI助手实例
    ai_assistant_ = std::make_unique<AIAssistant>(ai_config);
    
    // 创建AI助手虚拟用户
    UserInfo ai_user;
    ai_user.user_id = ai_user_id_;
    ai_user.username = config_ ? config_->getString("ai_assistant.username", "ai_assistant") : "ai_assistant";
    ai_user.nickname = config_ ? config_->getString("ai_assistant.nickname", "AI助手") : "AI助手";
    ai_user.online = true;  // AI助手永远在线
    ai_user.client_fd = -1;
    
    users_[ai_user_id_] = ai_user;
    username_map_[ai_user.username] = ai_user_id_;
    
    Logger::info("[IMServer] AI助手已初始化 (user_id=" + std::to_string(ai_user_id_) + 
                 ", username=" + ai_user.username + ")");
}

void IMServer::handleAIMessage(uint32_t from_user_id, const std::string& message, int clientFd) {
    Logger::info("[IMServer] 收到发给AI助手的消息，来自用户: " + std::to_string(from_user_id));
    
    // 1. 立即发送ACK
    sendResponse(clientFd, IMProtocol::MessageType::CHAT_MESSAGE_ACK,
                "{\"success\":true,\"delivered\":true,\"message\":\"AI正在思考中...\"}");
    
    // 2. 检查AI助手是否启用
    if (!ai_assistant_ || !ai_assistant_->isEnabled()) {
        sendAIResponse(from_user_id, "抱歉，AI助手当前不可用。");
        return;
    }
    
    // 3. 异步调用AI API
    ai_assistant_->sendMessage(from_user_id, message, 
        [this, from_user_id](const std::string& response, bool success, const std::string& error) {
            if (success) {
                sendAIResponse(from_user_id, response);
            } else {
                std::string error_msg = "抱歉，我遇到了一些问题：" + error;
                sendAIResponse(from_user_id, error_msg);
                Logger::error("[IMServer] AI调用失败: " + error);
            }
        });
}

void IMServer::sendAIResponse(uint32_t to_user_id, const std::string& response) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    
    // JSON转义函数
    auto escapeJSON = [](const std::string& str) -> std::string {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                default: result += c; break;
            }
        }
        return result;
    };
    
    // 构建AI回复消息
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    std::ostringstream oss;
    oss << "{\"from_user_id\":" << ai_user_id_
        << ",\"from_username\":\"ai_assistant\""
        << ",\"to_user_id\":" << to_user_id
        << ",\"content\":\"" << escapeJSON(response) << "\""
        << ",\"timestamp\":" << timestamp << "}";
    
    // 发送消息
    if (isUserOnline(to_user_id)) {
        int target_fd = getFdByUserId(to_user_id);
        sendResponse(target_fd, IMProtocol::MessageType::CHAT_MESSAGE, oss.str());
        Logger::info("[IMServer] AI回复已发送给在线用户 " + std::to_string(to_user_id));
    } else {
        storeOfflineMessage(to_user_id, oss.str());
        Logger::info("[IMServer] AI回复已存储为离线消息");
    }
}
