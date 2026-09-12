#include "System.h"
#include <algorithm>

namespace Potato {
namespace ECS {

// ============================================================================
// SystemManager 實現
// ============================================================================

SystemManager::SystemManager()
    : initialized(false)
{
}

SystemManager::~SystemManager() {
    ShutdownAllSystems();
}

void SystemManager::AddSystem(SharedPtr<ISystem> system) {
    if (system) {
        systems.push_back(system);
        
        // 如果已經初始化，立即初始化新系統
        if (initialized) {
            system->Initialize();
        }
        
        // 按優先級排序
        SortSystemsByPriority();
    }
}

void SystemManager::RemoveSystem(SharedPtr<ISystem> system) {
    auto it = std::find(systems.begin(), systems.end(), system);
    if (it != systems.end()) {
        (*it)->Shutdown();
        systems.erase(it);
    }
}

void SystemManager::RemoveSystem(const char* name) {
    for (auto it = systems.begin(); it != systems.end(); ++it) {
        if (strcmp((*it)->GetName(), name) == 0) {
            (*it)->Shutdown();
            systems.erase(it);
            break;
        }
    }
}

void SystemManager::InitializeAllSystems() {
    if (initialized) return;
    
    for (auto& system : systems) {
        system->Initialize();
    }
    
    initialized = true;
}

void SystemManager::UpdateAllSystems(float deltaTime) {
    if (!initialized) return;
    
    for (auto& system : systems) {
        system->Update(deltaTime);
    }
}

void SystemManager::ShutdownAllSystems() {
    for (auto& system : systems) {
        system->Shutdown();
    }
    
    systems.clear();
    initialized = false;
}

void SystemManager::SortSystemsByPriority() {
    std::sort(systems.begin(), systems.end(), 
        [](const SharedPtr<ISystem>& a, const SharedPtr<ISystem>& b) {
            return a->GetPriority() < b->GetPriority();
        });
}

} // namespace ECS
} // namespace Potato
