#include "EventBus.h"
#include <iostream>
#include <algorithm>

namespace Potato {

// 全局事件管理器
EventManager* gEventManager = nullptr;

// ============================================================================
// EventBus 實現
// ============================================================================

EventBus::EventBus() {
}

EventBus::~EventBus() {
    ClearEventQueue();
}

void EventBus::SubscribeListener(std::type_index eventType, IEventListener* listener) {
    std::lock_guard<std::mutex> lock(mutex);
    listeners[eventType].push_back(listener);
}

void EventBus::UnsubscribeListener(std::type_index eventType, IEventListener* listener) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = listeners.find(eventType);
    if (it != listeners.end()) {
        auto& listenerList = it->second;
        auto lit = std::find(listenerList.begin(), listenerList.end(), listener);
        if (lit != listenerList.end()) {
            listenerList.erase(lit);
        }
    }
}

void EventBus::ProcessEventQueue() {
    // 換出整個佇列再派發：handler 可安全 Publish/Subscribe 而不會死結或迭代器失效
    std::vector<std::pair<std::type_index, std::unique_ptr<IEvent>>> queueSnapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        queueSnapshot.swap(eventQueue);
    }
    
    for (auto& [typeIndex, event] : queueSnapshot) {
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
            handler(*event);
        }
        for (auto* listener : listenerSnapshot) {
            listener->OnEvent(*event);
        }
    }
}

void EventBus::ClearEventQueue() {
    std::lock_guard<std::mutex> lock(mutex);
    eventQueue.clear();
}

size_t EventBus::GetHandlerCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    size_t count = 0;
    for (const auto& [type, handlerList] : this->handlers) {
        count += handlerList.size();
    }
    return count;
}

size_t EventBus::GetListenerCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    size_t count = 0;
    for (const auto& [type, listenerList] : this->listeners) {
        count += listenerList.size();
    }
    return count;
}

size_t EventBus::GetQueuedEventCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return eventQueue.size();
}

// ============================================================================
// EventManager 實現
// ============================================================================

EventManager::EventManager()
    : defaultBus(nullptr)
    , initialized(false)
{
}

EventManager::~EventManager() {
    Shutdown();
}

void EventManager::Initialize() {
    if (initialized) return;
    
    // 創建默認事件總線
    defaultBus = new EventBus();
    
    initialized = true;
    std::cout << "Event Manager initialized" << std::endl;
}

void EventManager::Shutdown() {
    if (!initialized) return;
    
    buses.clear();
    delete defaultBus;
    defaultBus = nullptr;
    
    initialized = false;
    std::cout << "Event Manager shutdown complete" << std::endl;
}

EventBus* EventManager::GetBus(const std::string& busName) {
    auto it = buses.find(busName);
    if (it != buses.end()) {
        return it->second.get();
    }
    return nullptr;
}

void EventManager::CreateBus(const std::string& busName) {
    if (buses.find(busName) == buses.end()) {
        buses[busName] = MakeUnique<EventBus>();
        std::cout << "Created event bus: " << busName << std::endl;
    }
}

void EventManager::DestroyBus(const std::string& busName) {
    auto it = buses.find(busName);
    if (it != buses.end()) {
        std::cout << "Destroyed event bus: " << busName << std::endl;
        buses.erase(it);
    }
}

void EventManager::Update() {
    // 處理所有總線的事件隊列
    for (auto& [name, bus] : buses) {
        bus->ProcessEventQueue();
    }
    
    if (defaultBus) {
        defaultBus->ProcessEventQueue();
    }
}

void EventManager::PrintStatistics() const {
    std::cout << "=== Event Manager Statistics ===" << std::endl;
    std::cout << "Total Buses: " << buses.size() << std::endl;
    
    size_t totalHandlers = 0;
    size_t totalListeners = 0;
    size_t totalQueued = 0;
    
    for (const auto& [name, bus] : buses) {
        totalHandlers += bus->GetHandlerCount();
        totalListeners += bus->GetListenerCount();
        totalQueued += bus->GetQueuedEventCount();
    }
    
    if (defaultBus) {
        totalHandlers += defaultBus->GetHandlerCount();
        totalListeners += defaultBus->GetListenerCount();
        totalQueued += defaultBus->GetQueuedEventCount();
    }
    
    std::cout << "Total Handlers: " << totalHandlers << std::endl;
    std::cout << "Total Listeners: " << totalListeners << std::endl;
    std::cout << "Queued Events: " << totalQueued << std::endl;
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeEventManager() {
    if (gEventManager) {
        return false;
    }
    
    gEventManager = new EventManager();
    gEventManager->Initialize();
    
    return true;
}

void ShutdownEventManager() {
    if (gEventManager) {
        delete gEventManager;
        gEventManager = nullptr;
    }
}

EventManager* GetEventManager() {
    return gEventManager;
}

} // namespace Potato