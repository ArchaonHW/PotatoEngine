#include "PotatoEngine.h"
#include "JobSystem.h"
#include "Interfaces/IRenderer.h"
#include "Interfaces/IPhysics.h"
#include "Interfaces/IAudio.h"
#include "Interfaces/IInput.h"
#include "Interfaces/IResourceManager.h"
#include "Interfaces/ILogger.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IFileSystem.h"
#include <algorithm>
#include <iostream>

namespace Potato {

PotatoEngine::PotatoEngine()
    : state(EngineState::Uninitialized)
    , deltaTime(0.0f)
    , totalTime(0.0f)
    , timeScale(1.0f)
    , frameCount(0)
    , fpsAccumulator(0.0f)
{
    lastFrameTime = std::chrono::high_resolution_clock::now();
    fpsTimer = lastFrameTime;
}

PotatoEngine::~PotatoEngine()
{
    if (state != EngineState::Uninitialized && state != EngineState::ShuttingDown) {
        Shutdown();
    }
}

bool PotatoEngine::Initialize(const EngineConfig& engineConfig)
{
    SetState(EngineState::Initializing);
    config = engineConfig;
    
    std::cout << "Initializing Potato Engine..." << std::endl;
    std::cout << "Application: " << config.applicationName << std::endl;
    std::cout << "Resolution: " << config.windowWidth << "x" << config.windowHeight << std::endl;
    std::cout << "Target FPS: " << config.targetFPS << std::endl;
    std::cout << "Multi-threading: " << (config.enableMultiThreading ? "Enabled" : "Disabled") << std::endl;
    std::cout << "Worker threads: " << config.workerThreads << std::endl;
    
    // Initialize subsystems
    if (!InitializeSubsystems()) {
        std::cerr << "Failed to initialize subsystems" << std::endl;
        SetState(EngineState::Error);
        return false;
    }
    
    SetState(EngineState::Running);
    std::cout << "Potato Engine initialized successfully" << std::endl;
    return true;
}

void PotatoEngine::RunMainLoop()
{
    if (state != EngineState::Running) {
        return;
    }
    
    std::cout << "Starting main loop..." << std::endl;
    
    // Main game loop
    while (state == EngineState::Running) {
        // Calculate delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto frameDuration = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;
        
        deltaTime = frameDuration * timeScale;
        totalTime += deltaTime;
        
        // Process main loop
        ProcessInput();
        Update(deltaTime);
        Render();
        
        // Update performance metrics
        UpdatePerformanceMetrics();
        
        // Frame throttling
        if (config.vsync && config.targetFPS > 0) {
            float targetFrameTime = 1.0f / config.targetFPS;
            if (deltaTime < targetFrameTime) {
                // In a real implementation, this would sleep for the remaining time
            }
        }
    }
    
    std::cout << "Main loop ended" << std::endl;
}

void PotatoEngine::Shutdown()
{
    std::cout << "Shutting down Potato Engine..." << std::endl;
    
    SetState(EngineState::ShuttingDown);
    
    // Clear resources
    ClearResourceCache();
    
    // Unload scenes
    for (auto& pair : scenes) {
        UnloadScene(pair.second);
    }
    scenes.clear();
    
    // Shutdown subsystems
    ShutdownSubsystems();
    
    SetState(EngineState::Uninitialized);
    std::cout << "Potato Engine shutdown complete" << std::endl;
}

void PotatoEngine::Pause()
{
    if (state == EngineState::Running) {
        SetState(EngineState::Paused);
        std::cout << "Engine paused" << std::endl;
    }
}

void PotatoEngine::Resume()
{
    if (state == EngineState::Paused) {
        SetState(EngineState::Running);
        lastFrameTime = std::chrono::high_resolution_clock::now();
        std::cout << "Engine resumed" << std::endl;
    }
}

void PotatoEngine::UpdatePerformanceMetrics()
{
    CalculateFPS();
    
    // Update other metrics
    performanceMetrics.frameTime = deltaTime;
    performanceMetrics.activeObjects = static_cast<int>(scenes.size());
    
    // In a real implementation, this would get actual memory usage
    performanceMetrics.memoryUsageMB = static_cast<float>(loadedResources.size() * 10); // Simulated
}

void PotatoEngine::CalculateFPS()
{
    frameCount++;
    fpsAccumulator += deltaTime;
    
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto fpsDuration = std::chrono::duration<float>(currentTime - fpsTimer).count();
    
    if (fpsDuration >= 1.0f) {
        performanceMetrics.currentFPS = frameCount / fpsDuration;
        performanceMetrics.averageFPS = fpsAccumulator / fpsDuration;
        
        frameCount = 0;
        fpsAccumulator = 0.0f;
        fpsTimer = currentTime;
    }
}

void PotatoEngine::UpdateCPUUsage()
{
    // In a real implementation, this would measure actual CPU usage
    // For now, we'll simulate it based on frame time
    if (deltaTime > 0.016f) {
        performanceMetrics.cpuUsage = 100.0f;
    } else {
        performanceMetrics.cpuUsage = (deltaTime / 0.016f) * 100.0f;
    }
}

bool PotatoEngine::InitializeSubsystems()
{
    std::cout << "Initializing subsystems..." << std::endl;
    
    // In a real implementation, this would initialize all subsystems
    // For now, we'll just log the initialization
    
    if (config.enableMultiThreading) {
        std::cout << "  Multi-threading enabled with " << config.workerThreads << " worker threads" << std::endl;
    }
    
    if (config.enableJobSystem) {
        jobSystem = std::make_unique<JobSystem>(
            static_cast<uint32_t>(std::max(0, config.workerThreads)));
        std::cout << "  Job system enabled (" << jobSystem->WorkerCount()
                  << " workers)" << std::endl;
    }
    
    if (config.enableProfiling) {
        std::cout << "  Profiling enabled" << std::endl;
    }
    
    std::cout << "Subsystems initialized" << std::endl;
    return true;
}

void PotatoEngine::ShutdownSubsystems()
{
    std::cout << "Shutting down subsystems..." << std::endl;
    // Job system 先收：排空佇列並 join workers，避免工作中的
    // 回呼觸碰已拆解的子系統
    jobSystem.reset();
    std::cout << "Subsystems shutdown complete" << std::endl;
}

void PotatoEngine::ProcessInput()
{
    // In a real implementation, this would process input events
}

void PotatoEngine::Update(float dt)
{
    if (updateCallback) {
        updateCallback(dt);
    }
}

void PotatoEngine::Render()
{
    if (renderCallback) {
        renderCallback();
    }
    
    performanceMetrics.drawCalls = 1; // Simulated
    performanceMetrics.triangleCount = 100; // Simulated
}

void PotatoEngine::SetState(EngineState newState)
{
    EngineState oldState = state;
    state = newState;
    
    // Emit state change event
    Event event;
    event.type = "EngineStateChanged";
    event.data["oldState"] = std::to_string(static_cast<int>(oldState));
    event.data["newState"] = std::to_string(static_cast<int>(newState));
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

// ============================================================================
// Event System Implementation
// ============================================================================

void PotatoEngine::RegisterEvent(const std::string& eventType, EventCallback callback)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    eventListeners[eventType].push_back(callback);
    std::cout << "Registered event listener for: " << eventType << std::endl;
}

void PotatoEngine::UnregisterEvent(const std::string& eventType)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    eventListeners.erase(eventType);
    std::cout << "Unregistered event listeners for: " << eventType << std::endl;
}

void PotatoEngine::EmitEvent(const Event& event)
{
    // 鎖內複製回調快照、鎖外執行：回調可安全 Register/Unregister/EmitEvent
    // 也避免 callback 觸碰 scene/resource 鎖時形成鎖順序死結
    std::vector<EventCallback> snapshot;
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        auto it = eventListeners.find(event.type);
        if (it != eventListeners.end()) {
            snapshot = it->second;
        }
    }
    
    for (const auto& callback : snapshot) {
        callback(event);
    }
    
    std::cout << "Emitted event: " << event.type << std::endl;
}

void PotatoEngine::EmitEvent(const std::string& type, const std::unordered_map<std::string, std::string>& data)
{
    Event event;
    event.type = type;
    event.data = data;
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

// ============================================================================
// Resource Management Implementation
// ============================================================================

ResourceHandle PotatoEngine::LoadResource(const std::string& path, const std::string& type)
{
    ResourceHandle handle;
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        
        handle.id = nextResourceId++;
        handle.type = type;
        handle.path = path;
        handle.isValid = true;
        
        loadedResources[handle.id] = handle;
    }
    
    std::cout << "Loaded resource: " << path << " (ID: " << handle.id << ")" << std::endl;
    
    // Emit resource loaded event（釋放 resourceMutex 後才派發，避免回調重入死結）
    Event event;
    event.type = "ResourceLoaded";
    event.data["resourceId"] = std::to_string(handle.id);
    event.data["resourceType"] = type;
    event.data["resourcePath"] = path;
    event.timestamp = totalTime;
    
    EmitEvent(event);
    
    return handle;
}

void PotatoEngine::UnloadResource(ResourceHandle handle)
{
    bool unloaded = false;
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        
        auto it = loadedResources.find(handle.id);
        if (it != loadedResources.end()) {
            std::cout << "Unloaded resource: " << it->second.path << " (ID: " << handle.id << ")" << std::endl;
            loadedResources.erase(it);
            unloaded = true;
        }
    }
    
    if (unloaded) {
        // Emit resource unloaded event
        Event event;
        event.type = "ResourceUnloaded";
        event.data["resourceId"] = std::to_string(handle.id);
        event.timestamp = totalTime;
        
        EmitEvent(event);
    }
}

bool PotatoEngine::IsResourceLoaded(ResourceHandle handle) {
    std::lock_guard<std::mutex> lock(resourceMutex);
    return loadedResources.find(handle.id) != loadedResources.end();
}

void PotatoEngine::ClearResourceCache()
{
    {
        std::lock_guard<std::mutex> lock(resourceMutex);
        
        std::cout << "Clearing resource cache (" << loadedResources.size() << " resources)" << std::endl;
        loadedResources.clear();
    }
    
    // Emit cache cleared event
    Event event;
    event.type = "ResourceCacheCleared";
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

// ============================================================================
// Scene Management Implementation
// ============================================================================

SceneHandle PotatoEngine::CreateScene(const std::string& name)
{
    SceneHandle handle;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        
        handle.id = nextSceneId++;
        handle.name = name;
        handle.isActive = false;
        
        scenes[handle.id] = handle;
    }
    
    std::cout << "Created scene: " << name << " (ID: " << handle.id << ")" << std::endl;
    
    // Emit scene created event
    Event event;
    event.type = "SceneCreated";
    event.data["sceneId"] = std::to_string(handle.id);
    event.data["sceneName"] = name;
    event.timestamp = totalTime;
    
    EmitEvent(event);
    
    return handle;
}

void PotatoEngine::LoadScene(SceneHandle handle)
{
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        
        auto it = scenes.find(handle.id);
        if (it == scenes.end()) {
            return;
        }
        std::cout << "Loading scene: " << it->second.name << std::endl;
        // In a real implementation, this would load scene data
    }
    
    // Emit scene loaded event
    Event event;
    event.type = "SceneLoaded";
    event.data["sceneId"] = std::to_string(handle.id);
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

void PotatoEngine::UnloadScene(SceneHandle handle)
{
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        
        auto it = scenes.find(handle.id);
        if (it == scenes.end()) {
            return;
        }
        std::cout << "Unloading scene: " << it->second.name << std::endl;
        // In a real implementation, this would unload scene data
    }
    
    // Emit scene unloaded event
    Event event;
    event.type = "SceneUnloaded";
    event.data["sceneId"] = std::to_string(handle.id);
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

void PotatoEngine::SetActiveScene(SceneHandle handle)
{
    std::string sceneName;
    {
        std::lock_guard<std::mutex> lock(sceneMutex);
        
        // Activate new scene
        auto it = scenes.find(handle.id);
        if (it == scenes.end()) {
            return;
        }
        
        // Deactivate current active scene
        if (activeScene.id != 0) {
            auto prev = scenes.find(activeScene.id);
            if (prev != scenes.end()) {
                prev->second.isActive = false;
            }
        }
        
        it->second.isActive = true;
        activeScene = it->second;
        sceneName = it->second.name;
    }
    
    std::cout << "Active scene set to: " << sceneName << std::endl;
    
    // Emit active scene changed event
    Event event;
    event.type = "ActiveSceneChanged";
    event.data["sceneId"] = std::to_string(handle.id);
    event.data["sceneName"] = sceneName;
    event.timestamp = totalTime;
    
    EmitEvent(event);
}

SceneHandle PotatoEngine::GetActiveScene() {
    std::lock_guard<std::mutex> lock(sceneMutex);
    return activeScene;
}

std::vector<SceneHandle> PotatoEngine::GetAllScenes() {
    std::lock_guard<std::mutex> lock(sceneMutex);
    
    std::vector<SceneHandle> result;
    for (const auto& pair : scenes) {
        result.push_back(pair.second);
    }
    
    return result;
}

} // namespace Potato
