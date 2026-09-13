#pragma once

#include <memory>
#include <string>
#include <functional>

// Forward declarations for all subsystem interfaces
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
 * Engine configuration structure
 */
struct EngineConfig {
    std::string applicationName = "Potato Engine Application";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool fullscreen = false;
    bool vsync = true;
    int targetFPS = 60;
    bool enableValidation = true;
    std::string logLevel = "Info";
    size_t maxMemoryMB = 4096;
};

/**
 * Engine state enumeration
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
 * Potato Engine core class
 * Manages all subsystem lifecycle and coordination
 */
class PotatoEngine {
public:
    PotatoEngine();
    ~PotatoEngine();

    // Engine lifecycle
    bool Initialize(const EngineConfig& config);
    void RunMainLoop();
    void Shutdown();
    
    // State management
    EngineState GetState() const { return state; }
    bool IsRunning() const { return state == EngineState::Running; }
    
    // Subsystem accessors (temporarily disabled to avoid compilation errors)
    // IRenderer* GetRenderer() { return renderer ? renderer.get() : nullptr; }
    // IPhysics* GetPhysics() { return physics ? physics.get() : nullptr; }
    // IAudio* GetAudio() { return audio ? audio.get() : nullptr; }
    // IInput* GetInput() { return input ? input.get() : nullptr; }
    // IResourceManager* GetResourceManager() { return resourceManager ? resourceManager.get() : nullptr; }
    // ILogger* GetLogger() { return logger ? logger.get() : nullptr; }
    // IMemoryManager* GetMemoryManager() { return memoryManager ? memoryManager.get() : nullptr; }
    // IFileSystem* GetFileSystem() { return fileSystem ? fileSystem.get() : nullptr; }
    
    // Configuration access
    const EngineConfig& GetConfig() const { return config; }
    
    // Callback functions
    using UpdateCallback = std::function<void(float)>;
    void SetUpdateCallback(UpdateCallback callback) { updateCallback = callback; }
    
    using RenderCallback = std::function<void()>;
    void SetRenderCallback(RenderCallback callback) { renderCallback = callback; }

private:
    // Subsystem initialization
    bool InitializeSubsystems();
    void ShutdownSubsystems();
    
    // Main loop processing
    void ProcessInput();
    void Update(float deltaTime);
    void Render();
    
    // State management
    void SetState(EngineState newState);
    
    EngineState state;
    EngineConfig config;
    
    // Subsystem pointers
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<IPhysics> physics;
    std::unique_ptr<IAudio> audio;
    std::unique_ptr<IInput> input;
    std::unique_ptr<IResourceManager> resourceManager;
    std::unique_ptr<ILogger> logger;
    std::unique_ptr<IMemoryManager> memoryManager;
    std::unique_ptr<IFileSystem> fileSystem;
    
    // Callback functions
    UpdateCallback updateCallback;
    RenderCallback renderCallback;
    
    // Time management
    float deltaTime;
    float totalTime;
};

} // namespace Potato
