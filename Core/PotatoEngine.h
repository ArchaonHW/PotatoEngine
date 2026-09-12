#pragma once

#include <memory>
#include <string>
#include <functional>

// 前向聲明所有子系統接口
namespace Potato {
    class IRenderer;
    class IPhysics;
    class IAudio;
    class IInput;
    class IResourceManager;
    class ILogger;
    class IMemoryManager;
    class IFileSystem;
}

namespace Potato {

/**
 * 引擎配置結構
 */
struct EngineConfig {
    std::string applicationName = "Potato Engine Application";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    int targetFPS = 60;
    bool enableValidation = true; // 用於調試
    std::string logLevel = "Info";
    size_t maxMemoryMB = 4096; // 4GB
};

/**
 * 引擎狀態枚舉
 */
enum class EngineState {
    Uninitialized,
    Initializing,
    Running,
    Paused,
    ShuttingDown,
    Error
};

/**
 * Potato Engine 核心類
 * 管理所有子系統的生命週期和協調
 */
class PotatoEngine {
public:
    PotatoEngine();
    ~PotatoEngine();

    // 引擎生命周期
    bool Initialize(const EngineConfig& config);
    void RunMainLoop();
    void Shutdown();
    
    // 狀態管理
    EngineState GetState() const { return state; }
    bool IsRunning() const { return state == EngineState::Running; }
    
    // 子系統訪問器
    IRenderer* GetRenderer() { return renderer.get(); }
    IPhysics* GetPhysics() { return physics.get(); }
    IAudio* GetAudio() { return audio.get(); }
    IInput* GetInput() { return input.get(); }
    IResourceManager* GetResourceManager() { return resourceManager.get(); }
    ILogger* GetLogger() { return logger.get(); }
    IMemoryManager* GetMemoryManager() { return memoryManager.get(); }
    IFileSystem* GetFileSystem() { return fileSystem.get(); }
    
    // 配置訪問
    const EngineConfig& GetConfig() const { return config; }
    
    // 回調函數
    using UpdateCallback = std::function<void(float)>;
    void SetUpdateCallback(UpdateCallback callback) { updateCallback = callback; }
    
    using RenderCallback = std::function<void()>;
    void SetRenderCallback(RenderCallback callback) { renderCallback = callback; }

private:
    // 子系統初始化
    bool InitializeSubsystems();
    void ShutdownSubsystems();
    
    // 主循環處理
    void ProcessInput();
    void Update(float deltaTime);
    void Render();
    
    // 狀態管理
    void SetState(EngineState newState);
    
private:
    EngineState state;
    EngineConfig config;
    
    // 子系統指針
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<IPhysics> physics;
    std::unique_ptr<IAudio> audio;
    std::unique_ptr<IInput> input;
    std::unique_ptr<IResourceManager> resourceManager;
    std::unique_ptr<ILogger> logger;
    std::unique_ptr<IMemoryManager> memoryManager;
    std::unique_ptr<IFileSystem> fileSystem;
    
    // 回調函數
    UpdateCallback updateCallback;
    RenderCallback renderCallback;
    
    // 時間管理
    float deltaTime;
    float totalTime;
};

} // namespace Potato