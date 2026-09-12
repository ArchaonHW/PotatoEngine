#pragma once

#include "Core/CoreTypes.h"
#include "Entity.h"
#include "Component.h"
#include "System.h"
#include <memory>

namespace Potato {
namespace ECS {

/**
 * ECS 協調器
 * 協調實體、組件和系統的交互
 */
class ECSCoordinator {
public:
    ECSCoordinator();
    ~ECSCoordinator();
    
    // 初始化和關閉
    void Initialize();
    void Shutdown();
    
    // 實體管理
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsEntityValid(Entity entity) const;
    
    // 組件管理
    template<typename T>
    void AddComponent(Entity entity, T component) {
        EntityID entityID = entity.GetID();
        componentManager.AddComponent(entityID, component);
        
        // 更新實體的組件掩碼
        // 注意：這需要訪問EntityManager的內部狀態，實際實現可能需要調整
    }
    
    template<typename T>
    void RemoveComponent(Entity entity) {
        EntityID entityID = entity.GetID();
        componentManager.RemoveComponent<T>(entityID);
    }
    
    template<typename T>
    T& GetComponent(Entity entity) {
        return componentManager.GetComponent<T>(entity.GetID());
    }
    
    template<typename T>
    const T& GetComponent(Entity entity) const {
        return componentManager.GetComponent<T>(entity.GetID());
    }
    
    template<typename T>
    bool HasComponent(Entity entity) const {
        return componentManager.HasComponent<T>(entity.GetID());
    }
    
    // 系統管理
    void AddSystem(SharedPtr<ISystem> system);
    void RemoveSystem(SharedPtr<ISystem> system);
    
    template<typename T>
    SharedPtr<T> GetSystem() {
        return systemManager.GetSystem<T>();
    }
    
    // 主循環
    void Update(float deltaTime);
    
    // 統計信息
    size_t GetEntityCount() const;
    size_t GetSystemCount() const;
    
private:
    UniquePtr<EntityManager> entityManager;
    UniquePtr<ComponentManager> componentManager;
    UniquePtr<SystemManager> systemManager;
    
    bool initialized;
};

// 全局 ECS 協調器
extern ECSCoordinator* gECSCoordinator;

/**
 * 初始化全局 ECS 協調器
 */
bool InitializeECSCoordinator();

/**
 * 關閉全局 ECS 協調器
 */
void ShutdownECSCoordinator();

/**
 * 獲取全局 ECS 協調器
 */
ECSCoordinator* GetECSCoordinator();

} // namespace ECS
} // namespace Potato

// 便捷宏
#define GET_ECS() Potato::ECS::GetECSCoordinator()
