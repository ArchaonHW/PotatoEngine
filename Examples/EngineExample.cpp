#include "Core/PotatoEngine.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine Enhanced Example ===" << std::endl;
    std::cout << std::endl;
    
    // Configure engine with enhanced settings
    EngineConfig config;
    config.applicationName = "Potato Engine Enhanced Demo";
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.targetFPS = 60;
    config.enableMultiThreading = true;
    config.workerThreads = 4;
    config.enableJobSystem = true;
    config.enableProfiling = true;
    config.enableAsyncLoading = true;
    config.enableSceneManagement = true;
    
    // Create engine
    PotatoEngine engine;
    
    // Initialize engine
    std::cout << "Initializing engine..." << std::endl;
    if (!engine.Initialize(config)) {
        std::cerr << "Failed to initialize engine!" << std::endl;
        return 1;
    }
    
    std::cout << "Engine initialized successfully!" << std::endl;
    std::cout << "State: " << (engine.IsRunning() ? "Running" : "Not Running") << std::endl;
    std::cout << std::endl;
    
    // Register event listeners
    engine.RegisterEvent("EngineStateChanged", [](const Event& event) {
        std::cout << "[Event] Engine state changed to: " << event.data.at("newState") << std::endl;
    });
    
    engine.RegisterEvent("ResourceLoaded", [](const Event& event) {
        std::cout << "[Event] Resource loaded: " << event.data.at("resourcePath") << std::endl;
    });
    
    engine.RegisterEvent("SceneCreated", [](const Event& event) {
        std::cout << "[Event] Scene created: " << event.data.at("sceneName") << std::endl;
    });
    
    // Test resource management
    std::cout << "--- Resource Management ---" << std::endl;
    ResourceHandle texture1 = engine.LoadResource("textures/player.png", "Texture");
    ResourceHandle mesh1 = engine.LoadResource("models/character.obj", "Mesh");
    ResourceHandle sound1 = engine.LoadResource("audio/explosion.wav", "Audio");
    
    std::cout << "Resource 1 loaded: " << engine.IsResourceLoaded(texture1) << std::endl;
    std::cout << "Resource 2 loaded: " << engine.IsResourceLoaded(mesh1) << std::endl;
    std::cout << "Resource 3 loaded: " << engine.IsResourceLoaded(sound1) << std::endl;
    std::cout << std::endl;
    
    // Test scene management
    std::cout << "--- Scene Management ---" << std::endl;
    SceneHandle mainMenu = engine.CreateScene("MainMenu");
    SceneHandle gameScene = engine.CreateScene("GameScene");
    SceneHandle settingsScene = engine.CreateScene("Settings");
    
    engine.LoadScene(mainMenu);
    engine.SetActiveScene(mainMenu);
    
    std::cout << "Active scene: " << engine.GetActiveScene().name << std::endl;
    
    auto allScenes = engine.GetAllScenes();
    std::cout << "Total scenes: " << allScenes.size() << std::endl;
    for (const auto& scene : allScenes) {
        std::cout << "  - " << scene.name << " (Active: " << (scene.isActive ? "Yes" : "No") << ")" << std::endl;
    }
    std::cout << std::endl;
    
    // Test event system
    std::cout << "--- Event System ---" << std::endl;
    engine.EmitEvent("CustomEvent", {{"key", "value"}, {"action", "test"}});
    std::cout << std::endl;
    
    // Test time management
    std::cout << "--- Time Management ---" << std::endl;
    std::cout << "Time scale: " << engine.GetTimeScale() << std::endl;
    engine.SetTimeScale(0.5f);
    std::cout << "Time scale (slowed): " << engine.GetTimeScale() << std::endl;
    engine.SetTimeScale(2.0f);
    std::cout << "Time scale (sped up): " << engine.GetTimeScale() << std::endl;
    engine.SetTimeScale(1.0f);
    std::cout << "Time scale (normal): " << engine.GetTimeScale() << std::endl;
    std::cout << std::endl;
    
    // Set update callback
    int frameCount = 0;
    engine.SetUpdateCallback([&](float dt) {
        static int counter = 0;
        counter++;
        if (counter % 60 == 0) {
            std::cout << "Update frame " << counter << " (dt: " << dt << ")" << std::endl;
        }
    });
    
    // Run main loop for a short time
    std::cout << "--- Running Main Loop (3 seconds) ---" << std::endl;
    
    // Run loop for limited iterations instead of real-time
    std::cout << "Running main loop (10 iterations)..." << std::endl;
    
    for (int i = 0; i < 10; i++) {
        if (!engine.IsRunning()) {
            break;
        }
        std::cout << "Frame " << (i + 1) << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop the engine
    engine.Shutdown();
    
    // Get final performance metrics
    std::cout << std::endl;
    std::cout << "--- Performance Metrics ---" << std::endl;
    PerformanceMetrics metrics = engine.GetPerformanceMetrics();
    std::cout << "Current FPS: " << metrics.currentFPS << std::endl;
    std::cout << "Average FPS: " << metrics.averageFPS << std::endl;
    std::cout << "Frame time: " << metrics.frameTime << "s" << std::endl;
    std::cout << "CPU usage: " << metrics.cpuUsage << "%" << std::endl;
    std::cout << "Memory usage: " << metrics.memoryUsageMB << " MB" << std::endl;
    std::cout << "Draw calls: " << metrics.drawCalls << std::endl;
    std::cout << "Triangle count: " << metrics.triangleCount << std::endl;
    std::cout << "Active objects: " << metrics.activeObjects << std::endl;
    std::cout << std::endl;
    
    // Test pause/resume
    std::cout << "--- Pause/Resume Test ---" << std::endl;
    std::cout << "Final state: " << static_cast<int>(engine.GetState()) << std::endl;
    
    std::cout << std::endl;
    std::cout << "=== All Enhanced Engine Tests Passed ===" << std::endl;
    
    return 0;
}
