#pragma once

#include "Core/CoreTypes.h"
#include "Entity.h"
#include "Component.h"
#include <vector>
#include <functional>

namespace Potato {
namespace ECS {

/**
 * 系統基類
 * 定義系統的基本接口
 */
class ISystem {
public:
    virtual ~ISystem() = default;
    
    // 系統生命周期
    virtual void Initialize() {}
    virtual void Update(float deltaTime) = 0;
    virtual void Shutdown() {}
    
    // 系統優先級
    virtual int GetPriority() const { return 0; }
    virtual const char* GetName() const { return "Unknown System"; }
};

/**
 * 系統管理器
 * 管理所有系統的更新順序和執行
 */
class SystemManager {
public:
    SystemManager();
    ~SystemManager();
    
    // 系統管理
    void AddSystem(SharedPtr<ISystem> system);
    void RemoveSystem(SharedPtr<ISystem> system);
    void RemoveSystem(const char* name);
    
    // 系統執行
    void InitializeAllSystems();
    void UpdateAllSystems(float deltaTime);
    void ShutdownAllSystems();
    
    // 系統查詢
    template<typename T>
    SharedPtr<T> GetSystem() {
        for (auto& system : systems) {
            auto casted = std::dynamic_pointer_cast<T>(system);
            if (casted) {
                return casted;
            }
        }
        return nullptr;
    }
    
    size_t GetSystemCount() const { return systems.size(); }
    
private:
    void SortSystemsByPriority();
    
private:
    std::vector<SharedPtr<ISystem>> systems;
    bool initialized;
};

/**
 * 系統特徵
 * 用於自動註冊系統
 */
template<typename T>
class SystemTraits {
public:
    static const char* GetName() {
        return T::StaticName();
    }
    
    static int GetPriority() {
        return T::StaticPriority();
    }
};

} // namespace ECS
} // namespace Potato
