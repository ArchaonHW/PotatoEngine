#pragma once

#include <string>
#include <sstream>
#include <memory>

namespace Potato {

/**
 * 日誌級別枚舉
 */
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/**
 * 日誌系統接口
 * 定義日誌記錄的基本功能
 */
class ILogger {
public:
    virtual ~ILogger() = default;
    
    // 初始化和關閉
    virtual bool Initialize(const std::string& logLevel = "Info") = 0;
    virtual void Shutdown() = 0;
    
    // 日誌記錄方法
    virtual void Log(LogLevel level, const std::string& message) = 0;
    virtual void Log(LogLevel level, const std::string& category, const std::string& message) = 0;
    
    // 便捷方法
    virtual void Trace(const std::string& message) = 0;
    virtual void Debug(const std::string& message) = 0;
    virtual void Info(const std::string& message) = 0;
    virtual void Warning(const std::string& message) = 0;
    virtual void Error(const std::string& message) = 0;
    virtual void Fatal(const std::string& message) = 0;
    
    // 設置日誌級別
    virtual void SetLogLevel(LogLevel level) = 0;
    virtual LogLevel GetLogLevel() const = 0;
    
    // 日誌輸出設置
    virtual void SetLogFile(const std::string& filePath) = 0;
    virtual void EnableConsoleOutput(bool enable) = 0;
    virtual void EnableFileOutput(bool enable) = 0;
    
    // 日誌格式設置
    virtual void SetTimestampEnabled(bool enable) = 0;
    virtual void SetCategoryEnabled(bool enable) = 0;
};

/**
 * 日誌流輔助類
 * 用於流式日誌記錄
 */
class LogStream {
public:
    LogStream(ILogger* logger, LogLevel level, const std::string& category = "")
        : logger(logger), level(level), category(category) {}
    
    ~LogStream() {
        if (logger) {
            if (category.empty()) {
                logger->Log(level, stream.str());
            } else {
                logger->Log(level, category, stream.str());
            }
        }
    }
    
    template<typename T>
    LogStream& operator<<(const T& value) {
        stream << value;
        return *this;
    }
    
private:
    ILogger* logger;
    LogLevel level;
    std::string category;
    std::stringstream stream;
};

} // namespace Potato