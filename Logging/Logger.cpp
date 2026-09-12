#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <ctime>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <unistd.h>
#endif

namespace Potato {

// 全局日誌系統指針
Logger* gLogger = nullptr;

// ============================================================================
// ConsoleOutput 實現
// ============================================================================

ConsoleOutput::ConsoleOutput(bool enableColors)
    : enableColors(enableColors)
{
#ifdef _WIN32
    // 啟用 Windows 控制台 ANSI 顏色支持
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hConsole, &mode)) {
            SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}

void ConsoleOutput::Write(const LogMessage& message) {
    std::string color = GetLevelColor(message.level);
    std::string levelStr = GetLevelString(message.level);
    
    std::cout << color;
    
    if (!message.timestamp.empty()) {
        std::cout << "[" << message.timestamp << "] ";
    }
    
    std::cout << "[" << levelStr << "]";
    
    if (!message.category.empty()) {
        std::cout << "[" << message.category << "]";
    }
    
    std::cout << ": " << message.message << std::endl;
    
    ResetColor();
}

void ConsoleOutput::Flush() {
    std::cout.flush();
}

std::string ConsoleOutput::GetLevelColor(LogLevel level) const {
    if (!enableColors) return "";
    
#ifdef _WIN32
    // Windows ANSI 顏色代碼
    switch (level) {
        case LogLevel::Trace:   return "\033[90m";    // 灰色
        case LogLevel::Debug:   return "\033[36m";    // 青色
        case LogLevel::Info:    return "\033[32m";    // 綠色
        case LogLevel::Warning: return "\033[33m";    // 黃色
        case LogLevel::Error:   return "\033[31m";    // 紅色
        case LogLevel::Fatal:   return "\033[35m";    // 紫色
        default:               return "\033[0m";     // 重置
    }
#else
    // Unix 顏色代碼
    switch (level) {
        case LogLevel::Trace:   return "\033[90m";    // 灰色
        case LogLevel::Debug:   return "\033[36m";    // 青色
        case LogLevel::Info:    return "\033[32m";    // 綠色
        case LogLevel::Warning: return "\033[33m";    // 黃色
        case LogLevel::Error:   return "\033[31m";    // 紅色
        case LogLevel::Fatal:   return "\033[35m";    // 紫色
        default:               return "\033[0m";     // 重置
    }
#endif
}

std::string ConsoleOutput::GetLevelString(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:               return "UNKNOWN";
    }
}

void ConsoleOutput::ResetColor() {
    if (enableColors) {
        std::cout << "\033[0m";
    }
}

// ============================================================================
// FileOutput 實現
// ============================================================================

FileOutput::FileOutput(const std::string& filePath)
    : filePath(filePath)
{
    Reopen();
}

FileOutput::~FileOutput() {
    if (fileStream.is_open()) {
        fileStream.close();
    }
}

void FileOutput::Write(const LogMessage& message) {
    std::lock_guard<std::mutex> lock(fileMutex);
    
    if (!fileStream.is_open()) {
        return;
    }
    
    std::string levelStr;
    switch (message.level) {
        case LogLevel::Trace:   levelStr = "TRACE"; break;
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO";  break;
        case LogLevel::Warning: levelStr = "WARN";  break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
        case LogLevel::Fatal:   levelStr = "FATAL"; break;
        default:               levelStr = "UNKNOWN"; break;
    }
    
    fileStream << "[";
    if (!message.timestamp.empty()) {
        fileStream << message.timestamp << " ";
    }
    fileStream << levelStr;
    
    if (!message.category.empty()) {
        fileStream << " " << message.category;
    }
    
    fileStream << "] " << message.message << std::endl;
}

void FileOutput::Flush() {
    std::lock_guard<std::mutex> lock(fileMutex);
    if (fileStream.is_open()) {
        fileStream.flush();
    }
}

bool FileOutput::IsOpen() const {
    return fileStream.is_open();
}

void FileOutput::Reopen() {
    std::lock_guard<std::mutex> lock(fileMutex);
    
    if (fileStream.is_open()) {
        fileStream.close();
    }
    
    fileStream.open(filePath, std::ios::app);
    if (!fileStream.is_open()) {
        std::cerr << "Failed to open log file: " << filePath << std::endl;
    }
}

// ============================================================================
// Logger 實現
// ============================================================================

Logger::Logger()
    : currentLogLevel(LogLevel::Info)
    , consoleOutputEnabled(true)
    , fileOutputEnabled(false)
    , timestampEnabled(true)
    , categoryEnabled(true)
    , asyncLogging(false)
    , bufferSize(1000)
    , shutdownRequested(false)
    , sequenceNumber(0)
    , initialized(false)
{
    // 初始化統計計數器
    for (int i = 0; i < 6; i++) {
        messageCounts[i] = 0;
    }
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Initialize(const std::string& logLevel) {
    if (initialized) {
        return true;
    }
    
    currentLogLevel = StringToLogLevel(logLevel);
    
    // 創建控制台輸出器
    consoleOutput = MakeUnique<ConsoleOutput>(true);
    outputs.push_back(consoleOutput);
    
    // 啟動異步日誌線程
    if (asyncLogging) {
        shutdownRequested = false;
        logThread = std::thread(&Logger::ProcessLogQueue, this);
    }
    
    initialized = true;
    std::cout << "Logger initialized with level: " << LogLevelToString(currentLogLevel) << std::endl;
    
    return true;
}

void Logger::Shutdown() {
    if (!initialized) {
        return;
    }
    
    // 停止異步日誌線程
    if (asyncLogging && logThread.joinable()) {
        shutdownRequested = true;
        queueCondition.notify_all();
        logThread.join();
    }
    
    // 處理剩餘的日誌消息
    if (!asyncLogging) {
        while (!logQueue.empty()) {
            LogMessage message = logQueue.front();
            logQueue.pop();
            WriteMessage(message);
        }
    }
    
    // 刷新所有輸出器
    for (auto& output : outputs) {
        output->Flush();
    }
    
    outputs.clear();
    consoleOutput.reset();
    fileOutput.reset();
    
    initialized = false;
}

void Logger::Log(LogLevel level, const std::string& message) {
    Log(level, "", message);
}

void Logger::Log(LogLevel level, const std::string& category, const std::string& message) {
    if (level < currentLogLevel) {
        return;
    }
    
    LogMessage logMessage;
    logMessage.level = level;
    logMessage.category = categoryEnabled ? category : "";
    logMessage.message = message;
    logMessage.timestamp = timestampEnabled ? GetCurrentTimestamp() : "";
    logMessage.threadId = GetThreadId();
    logMessage.sequenceNumber = sequenceNumber++;
    
    // 更新統計
    int levelIndex = static_cast<int>(level);
    if (levelIndex >= 0 && levelIndex < 6) {
        messageCounts[levelIndex]++;
    }
    
    if (asyncLogging) {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        // 檢查緩衝區大小
        if (logQueue.size() >= bufferSize) {
            // 丟棄最舊的消息
            logQueue.pop();
        }
        
        logQueue.push(logMessage);
        queueCondition.notify_one();
    } else {
        WriteMessage(logMessage);
    }
}

void Logger::Trace(const std::string& message) {
    Log(LogLevel::Trace, message);
}

void Logger::Debug(const std::string& message) {
    Log(LogLevel::Debug, message);
}

void Logger::Info(const std::string& message) {
    Log(LogLevel::Info, message);
}

void Logger::Warning(const std::string& message) {
    Log(LogLevel::Warning, message);
}

void Logger::Error(const std::string& message) {
    Log(LogLevel::Error, message);
}

void Logger::Fatal(const std::string& message) {
    Log(LogLevel::Fatal, message);
}

void Logger::SetLogLevel(LogLevel level) {
    currentLogLevel = level;
}

LogLevel Logger::GetLogLevel() const {
    return currentLogLevel;
}

void Logger::SetLogFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    // 移除舊的文件輸出器
    if (fileOutput) {
        auto it = std::find(outputs.begin(), outputs.end(), fileOutput);
        if (it != outputs.end()) {
            outputs.erase(it);
        }
        fileOutput.reset();
    }
    
    // 創建新的文件輸出器
    fileOutput = MakeUnique<FileOutput>(filePath);
    if (fileOutput->IsOpen()) {
        outputs.push_back(fileOutput);
        fileOutputEnabled = true;
        std::cout << "Log file set to: " << filePath << std::endl;
    } else {
        fileOutputEnabled = false;
    }
}

void Logger::EnableConsoleOutput(bool enable) {
    std::lock_guard<std::mutex> lock(queueMutex);
    
    if (enable && !consoleOutputEnabled) {
        if (consoleOutput) {
            outputs.push_back(consoleOutput);
        }
        consoleOutputEnabled = true;
    } else if (!enable && consoleOutputEnabled) {
        if (consoleOutput) {
            auto it = std::find(outputs.begin(), outputs.end(), consoleOutput);
            if (it != outputs.end()) {
                outputs.erase(it);
            }
        }
        consoleOutputEnabled = false;
    }
}

void Logger::EnableFileOutput(bool enable) {
    fileOutputEnabled = enable;
}

void Logger::SetTimestampEnabled(bool enable) {
    timestampEnabled = enable;
}

void Logger::SetCategoryEnabled(bool enable) {
    categoryEnabled = enable;
}

void Logger::AddOutput(SharedPtr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(queueMutex);
    outputs.push_back(output);
}

void Logger::RemoveOutput(SharedPtr<ILogOutput> output) {
    std::lock_guard<std::mutex> lock(queueMutex);
    auto it = std::find(outputs.begin(), outputs.end(), output);
    if (it != outputs.end()) {
        outputs.erase(it);
    }
}

void Logger::SetAsyncLogging(bool enable) {
    if (asyncLogging == enable) {
        return;
    }
    
    asyncLogging = enable;
    
    if (enable && !logThread.joinable()) {
        shutdownRequested = false;
        logThread = std::thread(&Logger::ProcessLogQueue, this);
    } else if (!enable && logThread.joinable()) {
        shutdownRequested = true;
        queueCondition.notify_all();
        logThread.join();
    }
}

void Logger::SetBufferSize(size_t size) {
    bufferSize = size;
}

void Logger::Flush() {
    if (asyncLogging) {
        // 等待隊列處理完成
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondition.wait(lock, [this] { return logQueue.empty(); });
    }
    
    for (auto& output : outputs) {
        output->Flush();
    }
}

size_t Logger::GetMessageCount(LogLevel level) const {
    int levelIndex = static_cast<int>(level);
    if (levelIndex >= 0 && levelIndex < 6) {
        return messageCounts[levelIndex];
    }
    return 0;
}

size_t Logger::GetTotalMessageCount() const {
    size_t total = 0;
    for (int i = 0; i < 6; i++) {
        total += messageCounts[i];
    }
    return total;
}

void Logger::ProcessLogQueue() {
    while (!shutdownRequested) {
        LogMessage message;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this] { 
                return !logQueue.empty() || shutdownRequested; 
            });
            
            if (shutdownRequested && logQueue.empty()) {
                break;
            }
            
            if (!logQueue.empty()) {
                message = logQueue.front();
                logQueue.pop();
            }
        }
        
        if (!shutdownRequested) {
            WriteMessage(message);
        }
    }
}

void Logger::WriteMessage(const LogMessage& message) {
    for (auto& output : outputs) {
        output->Write(message);
    }
}

std::string Logger::FormatMessage(const LogMessage& message) const {
    std::stringstream ss;
    
    if (!message.timestamp.empty()) {
        ss << "[" << message.timestamp << "] ";
    }
    
    ss << "[" << LogLevelToString(message.level) << "]";
    
    if (!message.category.empty()) {
        ss << "[" << message.category << "]";
    }
    
    ss << ": " << message.message;
    
    return ss.str();
}

std::string Logger::GetCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::string Logger::GetThreadId() const {
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

LogLevel Logger::StringToLogLevel(const std::string& levelStr) const {
    std::string upperLevel = levelStr;
    std::transform(upperLevel.begin(), upperLevel.end(), upperLevel.begin(), ::toupper);
    
    if (upperLevel == "TRACE") return LogLevel::Trace;
    if (upperLevel == "DEBUG") return LogLevel::Debug;
    if (upperLevel == "INFO") return LogLevel::Info;
    if (upperLevel == "WARNING" || upperLevel == "WARN") return LogLevel::Warning;
    if (upperLevel == "ERROR") return LogLevel::Error;
    if (upperLevel == "FATAL") return LogLevel::Fatal;
    
    return LogLevel::Info; // 默认級別
}

std::string Logger::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:               return "UNKNOWN";
    }
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeLogger(const std::string& logLevel) {
    if (gLogger) {
        std::cerr << "Logger already initialized" << std::endl;
        return false;
    }
    
    gLogger = new Logger();
    return gLogger->Initialize(logLevel);
}

void ShutdownLogger() {
    if (gLogger) {
        delete gLogger;
        gLogger = nullptr;
    }
}

Logger* GetLogger() {
    return gLogger;
}

} // namespace Potato