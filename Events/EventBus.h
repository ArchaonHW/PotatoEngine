#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <typeindex>

namespace Potato {

/**
 * 事件基類
 */
class IEvent {
public:
    virtual ~IEvent() = default;
    virtual std::type_index GetType() const = 0;
};

/**
 * 具體事件基類
 */
template<typename T>
class Event : public IEvent {
public:
    static std::type_index GetTypeStatic() {
        return std::type_index(typeid(T));
    }
    
    std::type_index GetType() const override {
        return GetTypeStatic();
    }
};

/**
 * 事件處理器類型
 */
template<typename T>
using EventHandler = std::function<void(const T&)>;

/**
 * 事件監聽器接口
 */
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void OnEvent(const IEvent& event) = 0;
};

/**
 * 事件總線類
 * 管理事件的訂閱和分發
 */
class EventBus {
public:
    EventBus();
    ~EventBus();
    
    // 事件訂閱
    template<typename T>
    void Subscribe(EventHandler<T> handler) {
        std::lock_guard<std::mutex> lock(mutex);
        std::type_index typeIndex = std::type_index(typeid(T));
        handlers[typeIndex].push_back([handler](const IEvent& event) {
            handler(static_cast<const T&>(event));
        });
    }
    
    void SubscribeListener(std::type_index eventType, IEventListener* listener);
    void UnsubscribeListener(std::type_index eventType, IEventListener* listener);
    
    // 事件發布
    template<typename T>
    void Publish(const T& event) {
        std::type_index typeIndex = std::type_index(typeid(T));
        
        // 在鎖內複製快照、鎖外派發：
        // handler 內可安全呼叫 Subscribe/Unsubscribe/Publish 而不會死結
        std::vector<std::function<void(const IEvent&)>> handlerSnapshot;
        std::vector<IEventListener*> listenerSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            
            auto it = handlers.find(typeIndex);
            if (it != handlers.end()) {
                handlerSnapshot = it->second;
            }
            
            auto listenerIt = listeners.find(typeIndex);
            if (listenerIt != listeners.end()) {
                listenerSnapshot = listenerIt->second;
            }
        }
        
        for (auto& handler : handlerSnapshot) {
            handler(event);
        }
        for (auto* listener : listenerSnapshot) {
            listener->OnEvent(event);
        }
    }
    
    // 事件隊列
    void ProcessEventQueue();
    void ClearEventQueue();
    
    // 統計信息
    size_t GetHandlerCount() const;
    size_t GetListenerCount() const;
    size_t GetQueuedEventCount() const;
    
private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const IEvent&)>>> handlers;
    std::unordered_map<std::type_index, std::vector<IEventListener*>> listeners;
    
    std::vector<std::pair<std::type_index, std::unique_ptr<IEvent>>> eventQueue;
    
    mutable std::mutex mutex;
};

/**
 * 事件管理器
 * 管理多個事件總線
 */
class EventManager {
public:
    EventManager();
    ~EventManager();
    
    // 初始化和關閉
    void Initialize();
    void Shutdown();
    
    // 總線管理
    EventBus* GetBus(const std::string& busName);
    void CreateBus(const std::string& busName);
    void DestroyBus(const std::string& busName);
    
    // 默認總線
    EventBus* GetDefaultBus() { return defaultBus; }
    
    // 全局事件發布
    template<typename T>
    void Publish(const T& event) {
        if (defaultBus) {
            defaultBus->Publish(event);
        }
    }
    
    // 更新處理
    void Update();
    
    // 統計信息
    void PrintStatistics() const;
    
private:
    std::unordered_map<std::string, UniquePtr<EventBus>> buses;
    EventBus* defaultBus;
    
    bool initialized;
};

// 全局事件管理器
extern EventManager* gEventManager;

/**
 * 初始化全局事件管理器
 */
bool InitializeEventManager();

/**
 * 關閉全局事件管理器
 */
void ShutdownEventManager();

/**
 * 獲取全局事件管理器
 */
EventManager* GetEventManager();

// 便捷宏
#define GET_EVENTS() Potato::GetEventManager()

// 預定義義常用事件
namespace GameEvents {
    
// 游戏狀態事件
struct GameStateChanged : public Event<GameStateChanged> {
    enum class State {
        Start,
        Pause,
        Resume,
        GameOver,
        Victory
    };
    
    State oldState;
    State newState;
};

// 實體事件
struct EntityCreated : public Event<EntityCreated> {
    uint32 entityID;
};

struct EntityDestroyed : public Event<EntityDestroyed> {
    uint32 entityID;
};

// 場景事件
struct SceneLoaded : public Event<SceneLoaded> {
    std::string sceneName;
};

struct SceneUnloaded : public Event<SceneUnloaded> {
    std::string sceneName;
};

// 輸入事件
struct InputEvent : public Event<InputEvent> {
    enum class InputType {
        KeyDown,
        KeyUp,
        MouseDown,
        MouseUp,
        MouseMove,
        GamepadButton
    };
    
    InputType inputType;
    int key;
    float x, y;
};

// 碰撞事件
struct CollisionEvent : public Event<CollisionEvent> {
    uint32 entityA;
    uint32 entityB;
    Vector3 collisionPoint;
    Vector3 collisionNormal;
};

// 自定義數據事件
struct CustomDataEvent : public Event<CustomDataEvent> {
    std::string dataType;
    void* data;
    size_t dataSize;
};

} // namespace GameEvents

} // namespace Potato