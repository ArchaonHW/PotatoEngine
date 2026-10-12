#pragma once

#include "Core/CoreTypes.h"
#include <cstdint>
#include <bitset>
#include <vector>

namespace Potato {
namespace ECS {

// 最大支持的組件類型數量
constexpr size_t MAX_COMPONENTS = 64;

// 實體ID類型
using EntityID = uint32;

// 無效實體ID
constexpr EntityID INVALID_ENTITY = 0;

/**
 * 實體類
 * 輕量級的實體表示，僅包含ID和組件掩碼
 */
class Entity {
public:
    Entity() : id(INVALID_ENTITY), componentMask(0) {}
    Entity(EntityID id) : id(id), componentMask(0) {}
    
    EntityID GetID() const { return id; }
    bool IsValid() const { return id != INVALID_ENTITY; }
    
    // 組件掩碼操作（bitset 對越界索引會拋出，先檢查範圍）
    bool HasComponent(size_t componentType) const {
        return componentType < MAX_COMPONENTS && componentMask.test(componentType);
    }
    
    void AddComponent(size_t componentType) {
        if (componentType < MAX_COMPONENTS) {
            componentMask.set(componentType);
        }
    }
    
    void RemoveComponent(size_t componentType) {
        if (componentType < MAX_COMPONENTS) {
            componentMask.reset(componentType);
        }
    }
    
    const std::bitset<MAX_COMPONENTS>& GetComponentMask() const {
        return componentMask;
    }
    
    void SetComponentMask(const std::bitset<MAX_COMPONENTS>& mask) {
        componentMask = mask;
    }
    
    void ClearComponentMask() {
        componentMask.reset();
    }
    
private:
    EntityID id;
    std::bitset<MAX_COMPONENTS> componentMask;
};

/**
 * 實體管理器
 * 管理實體的創建、銷毀和ID分配
 */
class EntityManager {
public:
    EntityManager();
    ~EntityManager();
    
    // 實體生命周期
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsEntityValid(Entity entity) const;
    
    // 實體查詢
    size_t GetEntityCount() const { return livingEntityCount; }
    size_t GetTotalEntityCount() const { return entityCounter; }
    
private:
    EntityID entityCounter;
    size_t livingEntityCount;
    std::vector<EntityID> freeEntityIDs;
    std::vector<bool> entityValidity;
};

} // namespace ECS
} // namespace Potato