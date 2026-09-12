#include "ECSCoordinator.h"

namespace Potato {
namespace ECS {

// 全局 ECS 協調器
ECSCoordinator* gECSCoordinator = nullptr;

// ============================================================================
// ECSCoordinator 實現
// ============================================================================

ECSCoordinator::ECSCoordinator()
    : entityManager(MakeUnique<EntityManager>())
    , componentManager(MakeUnique<ComponentManager>())
    , systemManager(MakeUnique<SystemManager>())
    , initialized(false)
{
}

ECSCoordinator::~ECSCoordinator() {
    Shutdown();
}

void ECSCoordinator::Initialize() {
    if (initialized) return;
    
    systemManager->InitializeAllSystems();
    initialized = true;
}

void ECSCoordinator::Shutdown() {
    if (!initialized) return;
    
    systemManager->ShutdownAllSystems();
    componentManager->Clear();
    
    initialized = false;
}

Entity ECSCoordinator::CreateEntity() {
    return entityManager->CreateEntity();
}

void ECSCoordinator::DestroyEntity(Entity entity) {
    if (!entityManager->IsEntityValid(entity)) {
        return;
    }
    
    // 移除實體的所有組件
    componentManager->RemoveEntityComponents(entity.GetID());
    
    // 銷毀實體
    entityManager->DestroyEntity(entity);
}

bool ECSCoordinator::IsEntityValid(Entity entity) const {
    return entityManager->IsEntityValid(entity);
}

void ECSCoordinator::AddSystem(SharedPtr<ISystem> system) {
    systemManager->AddSystem(system);
}

void ECSCoordinator::RemoveSystem(SharedPtr<ISystem> system) {
    systemManager->RemoveSystem(system);
}

void ECSCoordinator::Update(float deltaTime) {
    if (!initialized) return;
    
    systemManager->UpdateAllSystems(deltaTime);
}

size_t ECSCoordinator::GetEntityCount() const {
    return entityManager->GetEntityCount();
}

size_t ECSCoordinator::GetSystemCount() const {
    return systemManager->GetSystemCount();
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeECSCoordinator() {
    if (gECSCoordinator) {
        return false;
    }
    
    gECSCoordinator = new ECSCoordinator();
    gECSCoordinator->Initialize();
    
    return true;
}

void ShutdownECSCoordinator() {
    if (gECSCoordinator) {
        gECSCoordinator->Shutdown();
        delete gECSCoordinator;
        gECSCoordinator = nullptr;
    }
}

ECSCoordinator* GetECSCoordinator() {
    return gECSCoordinator;
}

} // namespace ECS
} // namespace Potato
