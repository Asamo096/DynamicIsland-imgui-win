#include "config.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>

// 简单的JSON解析/生成实现 (不依赖外部库)
class SimpleJSON {
public:
    static std::string EscapeString(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c;
            }
        }
        return result;
    }
    
    static std::string FormatFloat(float f, int precision = 2) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(precision) << f;
        std::string result = ss.str();
        // 移除末尾的0
        while (result.length() > 1 && result.find('.') != std::string::npos && 
               (result.back() == '0' || result.back() == '.')) {
            if (result.back() == '.') {
                result.pop_back();
                break;
            }
            result.pop_back();
        }
        return result;
    }
};

Config& Config::Instance() {
    static Config instance;
    return instance;
}

std::string Config::GetConfigPath() const {
    return "config.json";
}

void Config::ResetToDefaults() {
    std::lock_guard<std::mutex> lock(mtx);
    island = IslandConfig();
    appearance = AppearanceConfig();
    system = SystemConfig();
    behavior = BehaviorConfig();
}

bool Config::Load() {
    std::lock_guard<std::mutex> lock(mtx);
    // 暂时跳过文件操作，直接使用默认值
    return true;
}

bool Config::Save() {
    std::lock_guard<std::mutex> lock(mtx);
    // 暂时跳过文件操作
    return true;
}
