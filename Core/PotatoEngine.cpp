#include "PotatoEngine.h"
#include "Interfaces/IRenderer.h"
#include "Interfaces/IPhysics.h"
#include "Interfaces/IAudio.h"
#include "Interfaces/IInput.h"
#include "Interfaces/IResourceManager.h"
#include "Interfaces/ILogger.h"
#include "Interfaces/IMemoryManager.h"
#include "Interfaces/IFileSystem.h"

namespace Potato {

PotatoEngine::PotatoEngine()
    : state(EngineState::Uninitialized)
    , deltaTime(0.0f)
    , totalTime(0.0f)
{
}

PotatoEngine::~PotatoEngine()
{
    if (state != EngineState::Uninitialized && state != EngineState::ShuttingDown) {
        Shutdown();
    }
}

bool PotatoEngine::Initialize(const EngineConfig& config)
{
    SetState(EngineState::Initializing);
    this->config = config;
    
    // 初始化日誌系統（首先初始化，以便記錄其他系統的初始化）
    // logger = std::make_unique<Logger>();
    // if (!logger->Initialize(config.logLevel)) {
    //     return false;
    // }
    
    // 初始化內存管理系統
    // memoryManager = std::make_unique<MemoryManager>();
    // if (!memoryManager->Initialize(config.maxMemoryMB)) {
    //     return false;
    // }
    
    // 初始化文件系統
    // fileSystem = std::make_unique<FileSystem>();
    // if (!fileSystem->Initialize()) {
    //     return false;
    // }
    
    // 初始化所有子系統
    if (!InitializeSubsystems()) {
        SetState(EngineState::Error);
        return false;
    }
    
    SetState(EngineState::Running);
    return true;
}

void PotatoEngine::RunMainLoop()
{
    if (state != EngineState::Running) {
        return;
    }
    
    // 主遊戲循環
    while (state == EngineState::Running) {
        // 處理輸入
        ProcessInput();
        
        // 更新遊戲邏輯
        Update(deltaTime);
        
        // 渲染幀
        Render();
        
        // TODO: 實現正確的幀時間計算
        deltaTime = 0.016f; // 暫時假設60FPS
        totalTime += deltaTime;
    }
}

void PotatoEngine::Shutdown()
{
    SetState(EngineState::ShuttingDown);
    ShutdownSubsystems();
    SetState(EngineState::Uninitialized);
}

bool PotatoEngine::InitializeSubsystems()
{
    // 初始化渲染系統
    // renderer = std::make_unique<VulkanRenderer>();
    // if (!renderer->Initialize(config)) {
    //     return false;
    // }
    
    // 初始化物理系統
    // physics = std::make_unique<PhysicsEngine>();
    // if (!physics->Initialize()) {
    //     return false;
    // }
    
    // 初始化音頻系統
    // audio = std::make_unique<AudioEngine>();
    // if (!audio->Initialize()) {
    //     return false;
    // }
    
    // 初始化輸入系統
    // input = std::make_unique<InputManager>();
    // if (!input->Initialize()) {
    //     return false;
    // }
    
    // 初始化資源管理器
    // resourceManager = std::make_unique<ResourceManager>();
    // if (!resourceManager->Initialize(fileSystem.get())) {
    //     return false;
    // }
    
    return true;
}

void PotatoEngine::ShutdownSubsystems()
{
    // 按相反順序關閉子系統
    if (resourceManager) {
        resourceManager->Shutdown();
        resourceManager.reset();
    }
    
    if (input) {
        input->Shutdown();
        input.reset();
    }
    
    if (audio) {
        audio->Shutdown();
        audio.reset();
    }
    
    if (physics) {
        physics->Shutdown();
        physics.reset();
    }
    
    if (renderer) {
        renderer->Shutdown();
        renderer.reset();
    }
    
    if (fileSystem) {
        fileSystem->Shutdown();
        fileSystem.reset();
    }
    
    if (memoryManager) {
        memoryManager->Shutdown();
        memoryManager.reset();
    }
    
    if (logger) {
        logger->Shutdown();
        logger.reset();
    }
}

void PotatoEngine::ProcessInput()
{
    if (input) {
        input->Update();
    }
}

void PotatoEngine::Update(float deltaTime)
{
    // 更新物理系統
    if (physics) {
        physics->Simulate(deltaTime);
    }
    
    // 更新音頻系統
    if (audio) {
        audio->Update(deltaTime);
    }
    
    // 調用用戶定義的更新回調
    if (updateCallback) {
        updateCallback(deltaTime);
    }
}

void PotatoEngine::Render()
{
    if (renderer) {
        renderer->BeginFrame();
        
        // 調用用戶定義的渲染回調
        if (renderCallback) {
            renderCallback();
        }
        
        renderer->EndFrame();
        renderer->Present();
    }
}

void PotatoEngine::SetState(EngineState newState)
{
    state = newState;
}

} // namespace Potato