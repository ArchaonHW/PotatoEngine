#pragma once

#include "Core/CoreTypes.h"
#include "Entity.h"
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <vector>
#include <stdexcept>

namespace Potato {
namespace ECS {

/**
 * 組件基類
 * 所有組件都必須繼承此類
 */
class IComponent {
public:
    virtual ~IComponent() = default;
    virtual std::type_index GetType() const = 0;
};

/**
 * 組件類型特徵
 * 用於為每個組件類型分配唯一ID
 */
class ComponentTypeRegistry {
public:
    template<typename T>
    static size_t GetTypeID() {
        static size_t typeID = GetNextTypeID();
        return typeID;
    }
    
private:
    static size_t GetNextTypeID() {
        static size_t counter = 0;
        return counter++;
    }
};

/**
 * 具體組件基類
 * 提供類型安全的組件接口
 */
template<typename T>
class Component : public IComponent {
public:
    static size_t GetTypeID() {
        return ComponentTypeRegistry::GetTypeID<T>();
    }
    
    std::type_index GetType() const override {
        return std::type_index(typeid(T));
    }
};

/**
 * 組件數組基類
 * 存儲特定類型的所有組件
 */
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void RemoveComponent(EntityID entityID) = 0;
    virtual size_t GetTypeID() const = 0;
};

/**
 * 具體組件數組
 * 存儲特定類型的組件並提供快速訪問
 */
template<typename T>
class ComponentArray : public IComponentArray {
public:
    void AddComponent(EntityID entityID, T component) {
        components[entityID] = component;
    }
    
    void RemoveComponent(EntityID entityID) override {
        components.erase(entityID);
    }
    
    T& GetComponent(EntityID entityID) {
        // 用 at() 而非 operator[]：查詢不存在的組件應拋出，而非誤插入預設值
        return components.at(entityID);
    }
    
    const T& GetComponent(EntityID entityID) const {
        return components.at(entityID);
    }
    
    bool HasComponent(EntityID entityID) const {
        return components.find(entityID) != components.end();
    }
    
    size_t GetTypeID() const override {
        return ComponentTypeRegistry::GetTypeID<T>();
    }
    
    const std::unordered_map<EntityID, T>& GetAllComponents() const {
        return components;
    }
    
private:
    std::unordered_map<EntityID, T> components;
};

/**
 * 組件管理器
 * 管理所有組件數組的生命周期
 */
class ComponentManager {
public:
    ComponentManager();
    ~ComponentManager();
    
    // 組件操作
    template<typename T>
    void AddComponent(EntityID entityID, T component) {
        GetComponentArray<T>()->AddComponent(entityID, component);
    }
    
    template<typename T>
    void RemoveComponent(EntityID entityID) {
        GetComponentArray<T>()->RemoveComponent(entityID);
    }
    
    template<typename T>
    T& GetComponent(EntityID entityID) {
        return GetComponentArray<T>()->GetComponent(entityID);
    }
    
    template<typename T>
    const T& GetComponent(EntityID entityID) const {
        const ComponentArray<T>* array = GetComponentArrayConst<T>();
        if (!array) {
            throw std::out_of_range("Component array does not exist for this type");
        }
        return array->GetComponent(entityID);
    }
    
    template<typename T>
    bool HasComponent(EntityID entityID) const {
        // const 查詢不得建立新數組（誤插入）
        const ComponentArray<T>* array = GetComponentArrayConst<T>();
        return array ? array->HasComponent(entityID) : false;
    }
    
    // 組件數組管理
    template<typename T>
    SharedPtr<ComponentArray<T>> GetComponentArray() {
        size_t typeID = ComponentTypeRegistry::GetTypeID<T>();
        
        if (componentArrays.find(typeID) == componentArrays.end()) {
            auto array = MakeShared<ComponentArray<T>>();
            componentArrays[typeID] = array;
            return array;
        }
        
        return std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeID]);
    }
    
    void RemoveEntityComponents(EntityID entityID);
    void Clear();
    
private:
    // const 版本：不建立新數組，不存在回傳 nullptr
    template<typename T>
    const ComponentArray<T>* GetComponentArrayConst() const {
        size_t typeID = ComponentTypeRegistry::GetTypeID<T>();
        auto it = componentArrays.find(typeID);
        if (it == componentArrays.end()) {
            return nullptr;
        }
        return static_cast<const ComponentArray<T>*>(it->second.get());
    }
    
private:
    std::unordered_map<size_t, SharedPtr<IComponentArray>> componentArrays;
};

} // namespace ECS
} // namespace Potato
