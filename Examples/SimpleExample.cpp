#include "Core/PotatoEngine.h"
#include <iostream>

using namespace Potato;

/**
 * 簡單示例程序
 * 演示如何使用Potato Engine核心框架
 */
int main() {
    std::cout << "Potato Engine Simple Example" << std::endl;
    std::cout << "=============================" << std::endl;
    
    // 創建引擎實例
    PotatoEngine engine;
    
    // 配置引擎
    EngineConfig config;
    config.applicationName = "Potato Engine Simple Example";
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.fullscreen = false;
    config.vsync = true;
    config.targetFPS = 60;
    config.logLevel = "Info";
    
    std::cout << "Initializing Potato Engine..." << std::endl;
    
    // 初始化引擎
    if (!engine.Initialize(config)) {
        std::cerr << "Failed to initialize Potato Engine!" << std::endl;
        return -1;
    }
    
    std::cout << "Potato Engine initialized successfully!" << std::endl;
    
    // 設置更新回調
    int frameCount = 0;
    engine.SetUpdateCallback([&frameCount](float deltaTime) {
        frameCount++;
        std::cout << "Frame " << frameCount << " - DeltaTime: " << deltaTime << "s" << std::endl;
        
        // 運行10幀後退出
        if (frameCount >= 10) {
            std::cout << "Demo completed after 10 frames." << std::endl;
        }
    });
    
    // 設置渲染回調
    engine.SetRenderCallback([]() {
        std::cout << "Rendering frame..." << std::endl;
    });
    
    std::cout << "Starting main loop..." << std::endl;
    
    // 運行主循環（在實際實現中，這會是一個持續的循環）
    // 這裡只是演示框架的使用
    for (int i = 0; i < 10; i++) {
        if (engine.IsRunning()) {
            // 在實際實現中，這會由 RunMainLoop() 處理
            // 這裡手動調用來演示
            std::cout << "Simulating frame " << (i + 1) << std::endl;
        }
    }
    
    std::cout << "Shutting down Potato Engine..." << std::endl;
    
    // 關閉引擎
    engine.Shutdown();
    
    std::cout << "Potato Engine shutdown complete!" << std::endl;
    
    return 0;
}