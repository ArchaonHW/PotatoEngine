#include "Core/PotatoEngine.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine Example ===" << std::endl;
    
    // Create engine instance
    PotatoEngine engine;
    
    // Configure engine
    EngineConfig config;
    config.applicationName = "Potato Engine Example";
    config.windowWidth = 1280;
    config.windowHeight = 720;
    config.targetFPS = 60;
    
    std::cout << "Initializing engine..." << std::endl;
    if (!engine.Initialize(config)) {
        std::cerr << "Failed to initialize engine!" << std::endl;
        return 1;
    }
    
    std::cout << "Engine initialized successfully!" << std::endl;
    std::cout << "State: " << (engine.IsRunning() ? "Running" : "Not Running") << std::endl;
    
    // Set update callback
    engine.SetUpdateCallback([](float deltaTime) {
        std::cout << "Update: deltaTime = " << deltaTime << "s" << std::endl;
    });
    
    // Run main loop for a few iterations (simulated)
    std::cout << "\nRunning main loop (5 iterations)..." << std::endl;
    
    // Note: The actual RunMainLoop() would run indefinitely
    // For this example, we'll manually call update a few times
    for (int i = 0; i < 5; ++i) {
        if (!engine.IsRunning()) {
            break;
        }
        std::cout << "Frame " << (i + 1) << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\nShutting down engine..." << std::endl;
    engine.Shutdown();
    
    std::cout << "Engine shutdown complete!" << std::endl;
    std::cout << "State: " << (engine.IsRunning() ? "Running" : "Not Running") << std::endl;
    
    return 0;
}
