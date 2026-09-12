#pragma once

#include "Core/CoreTypes.h"
#include "Core/Interfaces/ILogger.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>

namespace Potato {

/**
 * 日誌消息結構
 */
struct LogMessage {
    LogLevel level;
    std::string category;
    std::string message;
    std::string timestamp;
    std::string threadId;
    size_t sequenceNumber;
};

/**
 * 日誌輸出器接口
 */
class ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void Write(const LogMessage& message) = 0;
    virtual void Flush() = 0;
};

/**
 * 控制台輸出器
 */
class ConsoleOutput : public ILogOutput {
public:
    ConsoleOutput(bool enableColors = true);
    void Write(const LogMessage& message) override;
    void Flush() override;
    
private:
    bool enableColors;
    std::string GetLevelColor(LogLevel level) const;
    std::string GetLevelString(LogLevel level) const;
    void ResetColor();
};

/**
 * 文件輸出器
 */
class FileOutput : public ILogOutput {
public:
    FileOutput(const std::string& filePath);
    ~FileOutput();
    
    void Write(const LogMessage& message) override;
    void Flush() override;
    
    bool IsOpen() const;
    void Reopen();
    
private:
    std::ofstream fileStream;
    std::string filePath;
    std::mutex fileMutex;
};

/**
 * 日誌系統實現
 */
class Logger : public ILogger {
public:
    Logger();
    ~Logger();
    
    // ILogger 接口實現
    bool Initialize(const std::string& logLevel = "Info") override;
    void Shutdown() override;
    
    void Log(LogLevel level, const std::string& message) override;
    void Log(LogLevel level, const std::string& category, const std::string& message) override;
    
    void Trace(const std::string& message) override;
    void Debug(const std::string& message) override;
    void Info(const std::string& message) override;
    void Warning(const std::string& message) override;
    void Error(const std::string& message) override;
    void Fatal(const std::string& message) override;
    
    void SetLogLevel(LogLevel level) override;
    LogLevel GetLogLevel() const override;
    
    void SetLogFile(const std::string& filePath) override;
    void EnableConsoleOutput(bool enable) override;
    void EnableFileOutput(bool enable) override;
    
    void SetTimestampEnabled(bool enable) override;
    void SetCategoryEnabled(bool enable) override;
    
    // Logger 特定方法
    void AddOutput(SharedPtr<ILogOutput> output);
    void RemoveOutput(SharedPtr<ILogOutput> output);
    
    void SetAsyncLogging(bool enable);
    void SetBufferSize(size_t size);
    
    void Flush();
    
    // 獲取日誌統計
    size_t GetMessageCount(LogLevel level) const;
    size_t GetTotalMessageCount() const;
    
private:
    void ProcessLogQueue();
    void WriteMessage(const LogMessage& message);
    std::string FormatMessage(const LogMessage& message) const;
    std::string GetCurrentTimestamp() const;
    std::string GetThreadId() const;
    LogLevel StringToLogLevel(const std::string& levelStr) const;
    std::string LogLevelToString(LogLevel level) const;
    
private:
    LogLevel currentLogLevel;
    bool consoleOutputEnabled;
    bool fileOutputEnabled;
    bool timestampEnabled;
    bool categoryEnabled;
    bool asyncLogging;
    size_t bufferSize;
    
    std::vector<SharedPtr<ILogOutput>> outputs;
    UniquePtr<ConsoleOutput> consoleOutput;
    UniquePtr<FileOutput> fileOutput;
    
    // 異步日誌
    std::queue<LogMessage> logQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::thread logThread;
    std::atomic<bool> shutdownRequested;
    
    // 統計
    std::atomic<size_t> messageCounts[6]; // Trace, Debug, Info, Warning, Error, Fatal
    std::atomic<size_t> sequenceNumber;
    
    bool initialized;
};

// 全局日誌系統指針
extern Logger* gLogger;

/**
 * 初始化全局日誌系統
 */
bool InitializeLogger(const std::string& logLevel = "Info");

/**
 * 關閉全局日誌系統
 */
void ShutdownLogger();

/**
 * 獲取全局日誌系統
 */
Logger* GetLogger();

// 便捷日誌宏
#define POTATO_LOG_TRACE(msg) Potato::GetLogger()->Trace(msg)
#define POTATO_LOG_DEBUG(msg) Potato::GetLogger()->Debug(msg)
#define POTATO_LOG_INFO(msg) Potato::GetLogger()->Info(msg)
#define POTATO_LOG_WARNING(msg) Potato::GetLogger()->Warning(msg)
#define POTATO_LOG_ERROR(msg) Potato::GetLogger()->Error(msg)
#define POTATO_LOG_FATAL(msg) Potato::GetLogger()->Fatal(msg)

#define POTATO_LOG_TRACE_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Trace, cat, msg)
#define POTATO_LOG_DEBUG_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Debug, cat, msg)
#define POTATO_LOG_INFO_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Info, cat, msg)
#define POTATO_LOG_WARNING_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Warning, cat, msg)
#define POTATO_LOG_ERROR_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Error, cat, msg)
#define POTATO_LOG_FATAL_CAT(cat, msg) Potato::GetLogger()->Log(Potato::LogLevel::Fatal, cat, msg)

} // namespace Potato