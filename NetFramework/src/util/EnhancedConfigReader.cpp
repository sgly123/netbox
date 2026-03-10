#include "util/EnhancedConfigReader.h"
#include "base/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

bool EnhancedConfigReader::load(const std::string& filename) {
    clear();
    
    if (isYamlFile(filename)) {
    // 检测配置文件格式（生产环境不打印）
        return loadYamlFormat(filename);
    } else {
    // 检测配置文件格式（生产环境不打印）
        return loadTraditionalFormat(filename);
    }
}

bool EnhancedConfigReader::loadTraditionalFormat(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Logger::error("无法打开配置文件: " + filename);
        return false;
    }
    
    std::string line;
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        line = trim(line);
        
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // 查找等号分隔符
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            Logger::warn("配置文件第" + std::to_string(lineNumber) + "行格式错误: " + line);
            continue;
        }
        
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        
        if (key.empty()) {
            Logger::warn("配置文件第" + std::to_string(lineNumber) + "行键名为空");
            continue;
        }
        
        m_config[key] = value;
    }
    
    // 配置加载成功（生产环境不打印）
    return true;
}

bool EnhancedConfigReader::loadYamlFormat(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        Logger::error("无法打开配置文件: " + filename);
        return false;
    }
    
    std::string line;
    std::string currentSection;
    int lineNumber = 0;
    
    while (std::getline(file, line)) {
        lineNumber++;
        
        if (!parseYamlLine(line, currentSection)) {
            Logger::warn("配置文件第" + std::to_string(lineNumber) + "行解析失败: " + line);
        }
    }
    
    // YAML配置加载成功（生产环境不打印）
    return true;
}

bool EnhancedConfigReader::parseYamlLine(const std::string& line, std::string& currentSection) {
    std::string trimmedLine = trim(line);
    
    // 跳过空行和注释行
    if (trimmedLine.empty() || trimmedLine[0] == '#') {
        return true;
    }
    
    // 检查是否是节标题（以冒号结尾，没有值）
    if (trimmedLine.back() == ':' && trimmedLine.find(' ') == std::string::npos) {
        currentSection = trimmedLine.substr(0, trimmedLine.length() - 1);
        return true;
    }
    
    // 查找冒号分隔符
    size_t pos = trimmedLine.find(':');
    if (pos == std::string::npos) {
        return false;
    }
    
    std::string key = trim(trimmedLine.substr(0, pos));
    std::string value = trim(trimmedLine.substr(pos + 1));

    // 移除值中的注释部分
    size_t commentPos = value.find('#');
    if (commentPos != std::string::npos) {
        value = trim(value.substr(0, commentPos));
    }

    if (key.empty()) {
        return false;
    }

    // 构建完整的键名
    std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
    m_config[fullKey] = value;
    
    return true;
}

std::string EnhancedConfigReader::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_config.find(key);
    if (it == m_config.end()) {
        return defaultValue;
    }
    
    std::string value = it->second;
    
    // 去除可能的引号（单引号或双引号）
    if (value.length() >= 2) {
        if ((value.front() == '"' && value.back() == '"') ||
            (value.front() == '\'' && value.back() == '\'')) {
            value = value.substr(1, value.length() - 2);
        }
    }
    
    return value;
}

int EnhancedConfigReader::getInt(const std::string& key, int defaultValue) const {
    auto it = m_config.find(key);
    if (it == m_config.end()) {
        return defaultValue;
    }
    
    try {
        return std::stoi(it->second);
    } catch (const std::exception&) {
        Logger::warn("配置项 " + key + " 转换为整数失败，使用默认值: " + std::to_string(defaultValue));
        return defaultValue;
    }
}

bool EnhancedConfigReader::getBool(const std::string& key, bool defaultValue) const {
    auto it = m_config.find(key);
    if (it == m_config.end()) {
        return defaultValue;
    }
    
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    
    if (value == "true" || value == "yes" || value == "1" || value == "on") {
        return true;
    } else if (value == "false" || value == "no" || value == "0" || value == "off") {
        return false;
    } else {
        Logger::warn("配置项 " + key + " 转换为布尔值失败，使用默认值");
        return defaultValue;
    }
}

double EnhancedConfigReader::getDouble(const std::string& key, double defaultValue) const {
    auto it = m_config.find(key);
    if (it == m_config.end()) {
        return defaultValue;
    }
    
    try {
        return std::stod(it->second);
    } catch (const std::exception&) {
        Logger::warn("配置项 " + key + " 转换为浮点数失败，使用默认值: " + std::to_string(defaultValue));
        return defaultValue;
    }
}

bool EnhancedConfigReader::hasKey(const std::string& key) const {
    return m_config.find(key) != m_config.end();
}

std::vector<std::string> EnhancedConfigReader::getAllKeys() const {
    std::vector<std::string> keys;
    keys.reserve(m_config.size());
    
    for (const auto& pair : m_config) {
        keys.push_back(pair.first);
    }
    
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::unordered_map<std::string, std::string> EnhancedConfigReader::getKeysWithPrefix(const std::string& prefix) const {
    std::unordered_map<std::string, std::string> result;
    
    for (const auto& pair : m_config) {
        if (pair.first.substr(0, prefix.length()) == prefix) {
            result[pair.first] = pair.second;
        }
    }
    
    return result;
}

void EnhancedConfigReader::clear() {
    m_config.clear();
}

size_t EnhancedConfigReader::size() const {
    return m_config.size();
}

std::string EnhancedConfigReader::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool EnhancedConfigReader::isYamlFile(const std::string& filename) const {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos) {
        return false;
    }
    
    std::string extension = filename.substr(pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    
    return extension == "yaml" || extension == "yml";
}
