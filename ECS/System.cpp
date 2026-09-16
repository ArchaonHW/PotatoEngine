#include "System.h"
#include <algorithm>
#include <cstring>

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
    if (!name) return;
    for (auto it = systems.begin(); it != systems.end(); ++it) {
        if ((*it)->GetName() && strcmp((*it)->GetName(), name) == 0) {
            (*it)->Shutdown();
            systems.erase(it);
            break;
        }
    }
}

void SystemManager::InitializeAllSystems() {
    if (initialized) return;
    
    // 快照迭代：Initialize 內若 AddSystem 不會造成迭代器失效
    auto snapshot = systems;
    for (auto& system : snapshot) {
        system->Initialize();
    }
    
    initialized = true;
}

void SystemManager::UpdateAllSystems(float deltaTime) {
    if (!initialized) return;
    
    // 快照迭代：Update 內若 Add/RemoveSystem 不會造成迭代器失效
    auto snapshot = systems;
    for (auto& system : snapshot) {
        system->Update(deltaTime);
    }
}

void SystemManager::ShutdownAllSystems() {
    // 快照迭代：Shutdown 內若操作 systems 不會造成迭代器失效
    auto snapshot = systems;
    for (auto& system : snapshot) {
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
