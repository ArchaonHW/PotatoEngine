// Potato Engine - Platform 系統測試
// 測試平台抽象層的基本功能

#include "Platform/PlatformSystem.h"
#include <iostream>

using namespace Potato;

int main() {
    std::cout << "🥔 Potato Engine - Platform 系統測試" << std::endl;
    std::cout << "======================================" << std::endl;
    
    // 初始化平台管理器
    if (!InitializePlatformManager()) {
        std::cerr << "錯誤: 平台管理器初始化失敗" << std::endl;
        return -1;
    }
    
    std::cout << "✓ 平台管理器初始化成功" << std::endl;
    
    // 獲取平台信息
    PlatformManager* platformManager = GetPlatformManager();
    PlatformInfo info = platformManager->GetPlatformInfo();
    
    std::cout << "\n📊 平台信息:" << std::endl;
    std::cout << "  平台: " << info.platformName << std::endl;
    std::cout << "  CPU 數量: " << info.cpuCount << std::endl;
    std::cout << "  總內存: " << (info.totalMemory / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "  可用內存: " << (info.availableMemory / (1024 * 1024)) << " MB" << std::endl;
    
    // 測試線程創建
    std::cout << "\n🧵 測試線程創建..." << std::endl;
    auto thread = platformManager->CreateThread([]() {
        std::cout << "  工作線程運行中..." << std::endl;
    });
    
    if (thread) {
        thread->Start();
        thread->Join();
        platformManager->DestroyThread(thread);
        std::cout << "✓ 線程測試通過" << std::endl;
    }
    
    // 測試互斥鎖
    std::cout << "\n🔒 測試互斥鎖..." << std::endl;
    auto mutex = platformManager->CreateMutex();
    if (mutex) {
        mutex->Lock();
        std::cout << "  互斥鎖已鎖定" << std::endl;
        mutex->Unlock();
        std::cout << "  互斥鎖已解鎖" << std::endl;
        platformManager->DestroyMutex(mutex);
        std::cout << "✓ 互斥鎖測試通過" << std::endl;
    }
    
    // 測試條件變量
    std::cout << "\n⏳ 測試條件變量..." << std::endl;
    auto cv = platformManager->CreateConditionVariable();
    if (cv) {
        std::cout << "  條件變量已創建" << std::endl;
        platformManager->DestroyConditionVariable(cv);
        std::cout << "✓ 條件變量測試通過" << std::endl;
    }
    
    // 關閉平台管理器
    ShutdownPlatformManager();
    std::cout << "\n✓ 平台管理器已關閉" << std::endl;
    
    std::cout << "\n🎉 Platform 系統測試完成！" << std::endl;
    
    return 0;
}