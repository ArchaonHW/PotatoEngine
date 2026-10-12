#include "Component.h"

namespace Potato {
namespace ECS {

// ============================================================================
// ComponentManager 實現
// ============================================================================

ComponentManager::ComponentManager() {
}

ComponentManager::~ComponentManager() {
    Clear();
}

void ComponentManager::RemoveEntityComponents(EntityID entityID) {
    for (auto& [typeID, array] : componentArrays) {
        array->RemoveComponent(entityID);
    }
}

void ComponentManager::Clear() {
    componentArrays.clear();
}

} // namespace ECS
} // namespace Potato
