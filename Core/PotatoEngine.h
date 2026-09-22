#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

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
    class JobSystem;
    class Profiler;
}

namespace Potato {

/**
 * Engine configuration structure (Enhanced)
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
    
    // Performance settings
    bool enableMultiThreading = true;
    int workerThreads = 4;
    bool enableJobSystem = true;
    bool enableProfiling = false;
    
    // Resource settings
    size_t textureCacheSizeMB = 512;
    size_t meshCacheSizeMB = 256;
    bool enableAsyncLoading = true;
    
    // Scene settings
    bool enableSceneManagement = true;
    int maxActiveScenes = 1;
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
 * Performance metrics structure
 */
struct PerformanceMetrics {
    float currentFPS = 0.0f;
    float averageFPS = 0.0f;
    float frameTime = 0.0f;
    float cpuUsage = 0.0f;
    float memoryUsageMB = 0.0f;
    int drawCalls = 0;
    int triangleCount = 0;
    int activeObjects = 0;
};

/**
 * Event structure
 */
struct Event {
    std::string type;
    std::unordered_map<std::string, std::string> data;
    float timestamp = 0.0f;
};

/**
 * Event callback type
 */
using EventCallback = std::function<void(const Event&)>;

/**
 * Resource handle
 */
struct ResourceHandle {
    uint64_t id = 0;
    std::string type;
    std::string path;
    bool isValid = false;
};

/**
 * Scene handle
 */
struct SceneHandle {
    uint64_t id = 0;
    std::string name;
    bool isActive = false;
};

/**
 * Potato Engine core class (Enhanced)
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
    void Pause();
    void Resume();
    
    // Performance metrics
    PerformanceMetrics GetPerformanceMetrics() const { return performanceMetrics; }
    void UpdatePerformanceMetrics();
    
    // Configuration access
    const EngineConfig& GetConfig() const { return config; }

    // Job system（EngineConfig::enableJobSystem=true 時由 Initialize 建立,
    // workerThreads 指定工人數;關閉或停用時回 nullptr）
    JobSystem* GetJobSystem() const { return jobSystem.get(); }

    // Profiler（enableProfiling=true 時建立並設為全域啟用點;
    // POTATO_PROFILE_SCOPE 巨集才有作用對象）
    Profiler* GetProfiler() const { return profiler.get(); }
    
    // Callback functions
    using UpdateCallback = std::function<void(float)>;
    void SetUpdateCallback(UpdateCallback callback) { updateCallback = callback; }
    
    using RenderCallback = std::function<void()>;
    void SetRenderCallback(RenderCallback callback) { renderCallback = callback; }
    
    // Event system
    void RegisterEvent(const std::string& eventType, EventCallback callback);
    void UnregisterEvent(const std::string& eventType);
    void EmitEvent(const Event& event);
    void EmitEvent(const std::string& type, const std::unordered_map<std::string, std::string>& data);
    
    // Resource management
    ResourceHandle LoadResource(const std::string& path, const std::string& type);
    void UnloadResource(ResourceHandle handle);
    bool IsResourceLoaded(ResourceHandle handle);
    void ClearResourceCache();
    
    // Scene management
    SceneHandle CreateScene(const std::string& name);
    void LoadScene(SceneHandle handle);
    void UnloadScene(SceneHandle handle);
    void SetActiveScene(SceneHandle handle);
    SceneHandle GetActiveScene();
    std::vector<SceneHandle> GetAllScenes();
    
    // Time management
    float GetDeltaTime() const { return deltaTime; }
    float GetTotalTime() const { return totalTime; }
    float GetTimeScale() const { return timeScale; }
    void SetTimeScale(float scale) { timeScale = scale; }
    
    // Subsystem accessors (temporarily disabled to avoid compilation errors)
    // IRenderer* GetRenderer() { return renderer ? renderer.get() : nullptr; }
    // IPhysics* GetPhysics() { return physics ? physics.get() : nullptr; }
    // IAudio* GetAudio() { return audio ? audio.get() : nullptr; }
    // IInput* GetInput() { return input ? input.get() : nullptr; }
    // IResourceManager* GetResourceManager() { return resourceManager ? resourceManager.get() : nullptr; }
    // ILogger* GetLogger() { return logger ? logger.get() : nullptr; }
    // IMemoryManager* GetMemoryManager() { return memoryManager ? memory.get() : nullptr; }
    // IFileSystem* GetFileSystem() { return fileSystem ? fileSystem.get() : nullptr; }

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
    
    // Performance tracking
    void CalculateFPS();
    void UpdateCPUUsage();
    
    // Event system implementation
    std::unordered_map<std::string, std::vector<EventCallback>> eventListeners;
    mutable std::mutex eventMutex;
    
    // Resource management implementation
    std::unordered_map<uint64_t, ResourceHandle> loadedResources;
    uint64_t nextResourceId = 1;
    mutable std::mutex resourceMutex;
    
    // Scene management implementation
    std::unordered_map<uint64_t, SceneHandle> scenes;
    SceneHandle activeScene;
    uint64_t nextSceneId = 1;
    mutable std::mutex sceneMutex;
    
    EngineState state;
    EngineConfig config;
    PerformanceMetrics performanceMetrics;
    
    // Subsystem pointers
    std::unique_ptr<IRenderer> renderer;
    std::unique_ptr<IPhysics> physics;
    std::unique_ptr<IAudio> audio;
    std::unique_ptr<IInput> input;
    std::unique_ptr<IResourceManager> resourceManager;
    std::unique_ptr<ILogger> logger;
    std::unique_ptr<IMemoryManager> memoryManager;
    std::unique_ptr<IFileSystem> fileSystem;
    std::unique_ptr<JobSystem> jobSystem;
    std::unique_ptr<Profiler> profiler;
    
    // Callback functions
    UpdateCallback updateCallback;
    RenderCallback renderCallback;
    
    // Time management
    float deltaTime;
    float totalTime;
    float timeScale;
    
    // Performance tracking
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    int frameCount;
    float fpsAccumulator;
    std::chrono::high_resolution_clock::time_point fpsTimer;
};

} // namespace Potato
