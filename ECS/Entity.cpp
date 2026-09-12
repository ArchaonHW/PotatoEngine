#include "Entity.h"
#include <algorithm>

namespace Potato {
namespace ECS {

// ============================================================================
// EntityManager 實現
// ============================================================================

EntityManager::EntityManager()
    : entityCounter(1)
    , livingEntityCount(0)
{
}

EntityManager::~EntityManager() {
    // 清理所有實體
    entityValidity.clear();
    freeEntityIDs.clear();
}

Entity EntityManager::CreateEntity() {
    EntityID newID;
    
    if (!freeEntityIDs.empty()) {
        // 重用回收的ID
        newID = freeEntityIDs.back();
        freeEntityIDs.pop_back();
    } else {
        // 分配新ID
        newID = entityCounter++;
    }
    
    // 確保實體有效性數組足夠大
    if (newID >= entityValidity.size()) {
        entityValidity.resize(newID + 1, false);
    }
    
    entityValidity[newID] = true;
    livingEntityCount++;
    
    return Entity(newID);
}

void EntityManager::DestroyEntity(Entity entity) {
    EntityID entityID = entity.GetID();
    
    if (entityID >= entityValidity.size() || !entityValidity[entityID]) {
        return; // 實體無效
    }
    
    entityValidity[entityID] = false;
    freeEntityIDs.push_back(entityID);
    livingEntityCount--;
}

bool EntityManager::IsEntityValid(Entity entity) const {
    EntityID entityID = entity.GetID();
    
    if (entityID >= entityValidity.size()) {
        return false;
    }
    
    return entityValidity[entityID];
}

} // namespace ECS
} // namespace Potato
