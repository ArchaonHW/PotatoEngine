#include "Logging/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace Potato;

void TestThread(int threadId) {
    for (int i = 0; i < 5; i++) {
        std::string message = "Thread " + std::to_string(threadId) + " message " + std::to_string(i);
        GetLogger()->Info(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    std::cout << "=== Potato Engine Logger Test ===" << std::endl;
    
    // 初始化日誌系統
    if (!InitializeLogger("Debug")) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return -1;
    }
    
    Logger* logger = GetLogger();
    
    // 測試不同日誌級別
    std::cout << "\n--- Log Level Tests ---" << std::endl;
    logger->Trace("This is a trace message");
    logger->Debug("This is a debug message");
    logger->Info("This is an info message");
    logger->Warning("This is a warning message");
    logger->Error("This is an error message");
    logger->Fatal("This is a fatal message");
    
    // 測試分類日誌
    std::cout << "\n--- Category Tests ---" << std::endl;
    logger->Log(LogLevel::Info, "System", "System started");
    logger->Log(LogLevel::Debug, "Renderer", "Rendering frame 1");
    logger->Log(LogLevel::Warning, "Physics", "Physics simulation lag detected");
    logger->Log(LogLevel::Error, "Network", "Connection timeout");
    
    // 測試日誌級別過濾
    std::cout << "\n--- Log Level Filtering Tests ---" << std::endl;
    logger->SetLogLevel(LogLevel::Warning);
    logger->Trace("This trace should not appear");
    logger->Debug("This debug should not appear");
    logger->Info("This info should not appear");
    logger->Warning("This warning should appear");
    logger->Error("This error should appear");
    
    logger->SetLogLevel(LogLevel::Debug);
    logger->Info("Log level reset to Debug");
    
    // 測試日誌統計
    std::cout << "\n--- Statistics Tests ---" << std::endl;
    std::cout << "Total messages: " << logger->GetTotalMessageCount() << std::endl;
    std::cout << "Trace messages: " << logger->GetMessageCount(LogLevel::Trace) << std::endl;
    std::cout << "Debug messages: " << logger->GetMessageCount(LogLevel::Debug) << std::endl;
    std::cout << "Info messages: " << logger->GetMessageCount(LogLevel::Info) << std::endl;
    std::cout << "Warning messages: " << logger->GetMessageCount(LogLevel::Warning) << std::endl;
    std::cout << "Error messages: " << logger->GetMessageCount(LogLevel::Error) << std::endl;
    std::cout << "Fatal messages: " << logger->GetMessageCount(LogLevel::Fatal) << std::endl;
    
    // 測試文件輸出
    std::cout << "\n--- File Output Tests ---" << std::endl;
    logger->SetLogFile("test_log.txt");
    logger->EnableFileOutput(true);
    logger->Info("This message should appear in both console and file");
    logger->Flush();
    
    // 測試控制台輸出開關
    std::cout << "\n--- Console Output Toggle Tests ---" << std::endl;
    logger->EnableConsoleOutput(false);
    logger->Info("This should only appear in file");
    logger->Flush();
    
    logger->EnableConsoleOutput(true);
    logger->Info("Console output re-enabled");
    
    // 測試時間戳和分類開關
    std::cout << "\n--- Timestamp and Category Toggle Tests ---" << std::endl;
    logger->SetTimestampEnabled(false);
    logger->Info("Message without timestamp");
    
    logger->SetTimestampEnabled(true);
    logger->SetCategoryEnabled(false);
    logger->Log(LogLevel::Info, "TestCategory", "Message without category");
    
    logger->SetCategoryEnabled(true);
    logger->Info("Both timestamp and category enabled");
    
    // 測試多線程日誌
    std::cout << "\n--- Multi-threading Tests ---" << std::endl;
    logger->SetAsyncLogging(true);
    logger->Info("Starting multi-threading test");
    
    std::thread thread1(TestThread, 1);
    std::thread thread2(TestThread, 2);
    std::thread thread3(TestThread, 3);
    
    thread1.join();
    thread2.join();
    thread3.join();
    
    logger->Info("Multi-threading test complete");
    logger->Flush();
    
    logger->SetAsyncLogging(false);
    
    // 測試日誌流
    std::cout << "\n--- Log Stream Tests ---" << std::endl;
    int value = 42;
    float pi = 3.14159f;
    std::string text = "stream test";
    
    logger->Info("Stream test: value=" + std::to_string(value) + 
                ", pi=" + std::to_string(pi) + ", text=" + text);
    
    // 最終統計
    std::cout << "\n--- Final Statistics ---" << std::endl;
    std::cout << "Total messages logged: " << logger->GetTotalMessageCount() << std::endl;
    
    // 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    logger->Flush();
    ShutdownLogger();
    
    std::cout << "\n=== Logger Test Complete ===" << std::endl;
    std::cout << "Check test_log.txt for file output" << std::endl;
    
    return 0;
}