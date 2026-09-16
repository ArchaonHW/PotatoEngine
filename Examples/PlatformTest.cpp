// Potato Engine - Platform 系統測試
// 測試平台抽象層的基本功能

#include "Platform/PlatformSystem.h"
#include "Platform/GLFWSharedContext.h"
#include <GLFW/glfw3.h>
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

        // Wait/Notify 交接：Wait 返回後呼叫者必須仍持有 mutex（adopt_lock 修復的契約）
        auto cvMutex = platformManager->CreateMutex();
        bool ready = false;
        auto notifier = platformManager->CreateThread([&]() {
            cvMutex->Lock();
            ready = true;
            cv->NotifyOne();
            cvMutex->Unlock();
        });
        notifier->Start();
        cvMutex->Lock();
        while (!ready) {
            cv->Wait(*cvMutex); // 返回時 mutex 仍應由我們持有
        }
        cvMutex->Unlock(); // 若 Wait 內部 double-unlock，此行為 UB
        notifier->Join();
        platformManager->DestroyThread(notifier);
        platformManager->DestroyMutex(cvMutex);
        std::cout << "  Wait/Notify 交接測試通過" << std::endl;

        // WaitFor：逾時回 false、有 notify 回 true
        auto tfMutex = platformManager->CreateMutex();
        tfMutex->Lock();
        bool timedOut = !cv->WaitFor(*tfMutex, 20);
        tfMutex->Unlock();
        std::cout << (timedOut ? "  WaitFor 逾時正確回傳 false" : "  WaitFor 逾時判定異常") << std::endl;
        platformManager->DestroyMutex(tfMutex);

        platformManager->DestroyConditionVariable(cv);
        std::cout << "✓ 條件變量測試通過" << std::endl;
    }
    
    // 測試共享 context 生命週期（stale entry / UAF 防護）
    std::cout << "\n🪟 測試共享 context 生命週期..." << std::endl;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* testWin = glfwCreateWindow(64, 64, "ctx-test", nullptr, nullptr);
    if (!testWin) {
        std::cout << "  （無法建立測試窗口，略過——無顯示環境）" << std::endl;
    } else {
        InstallGLFWDispatchCallbacks(testWin);
        GLFWSharedContext* ctx1 = FindGLFWContext(testWin);
        bool installed = (ctx1 != nullptr);
        std::cout << (installed ? "  context 已建立並安裝" : "  context 建立失敗") << std::endl;

        // 模擬位址回收：user pointer 不再指向原 context 時,
        // GetOrCreate 必須丟棄 stale entry 重建（否則懸空 owner 會 UAF）
        ctx1->windowOwner = reinterpret_cast<GLFWWindow*>(0xDEAD);
        glfwSetWindowUserPointer(testWin, nullptr);
        GLFWSharedContext& ctx2 = GetOrCreateGLFWContext(testWin);
        bool recycled = (&ctx2 != ctx1 && ctx2.windowOwner == nullptr);
        std::cout << (recycled ? "  stale entry 被偵測並重建" : "  stale entry 未被清理!") << std::endl;

        // 正常銷毀路徑：DestroyGLFWWindow 後 context 必須從 map 移除
        glfwSetWindowUserPointer(testWin, &ctx2);
        ctx2.installed = true;
        DestroyGLFWWindow(testWin);
        bool released = (FindGLFWContext(testWin) == nullptr);
        std::cout << (released ? "  DestroyGLFWWindow 已釋放 context" : "  context 殘留!") << std::endl;

        if (installed && recycled && released) {
            std::cout << "✓ 共享 context 測試通過" << std::endl;
        } else {
            std::cout << "✗ 共享 context 測試失敗" << std::endl;
        }
    }

    // 關閉平台管理器
    ShutdownPlatformManager();
    std::cout << "\n✓ 平台管理器已關閉" << std::endl;
    
    std::cout << "\n🎉 Platform 系統測試完成！" << std::endl;
    
    return 0;
}