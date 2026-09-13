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

bool PotatoEngine::Initialize(const EngineConfig& engineConfig)
{
    SetState(EngineState::Initializing);
    config = engineConfig;
    
    // Skip subsystem initialization for now
    SetState(EngineState::Running);
    return true;
}

void PotatoEngine::RunMainLoop()
{
    if (state != EngineState::Running) {
        return;
    }
    
    // Main game loop
    while (state == EngineState::Running) {
        ProcessInput();
        Update(deltaTime);
        Render();
        
        deltaTime = 0.016f;
        totalTime += deltaTime;
    }
}

void PotatoEngine::Shutdown()
{
    SetState(EngineState::ShuttingDown);
    SetState(EngineState::Uninitialized);
}

bool PotatoEngine::InitializeSubsystems()
{
    return true;
}

void PotatoEngine::ShutdownSubsystems()
{
}

void PotatoEngine::ProcessInput()
{
}

void PotatoEngine::Update(float dt)
{
    if (updateCallback) {
        updateCallback(dt);
    }
}

void PotatoEngine::Render()
{
}

void PotatoEngine::SetState(EngineState newState)
{
    state = newState;
}

} // namespace Potato
