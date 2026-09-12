#include "GameObject.h"
#include <algorithm>

namespace Potato {

// 全局遊戲對象管理器
GameObjectManager* gGameObjectManager = nullptr;

// ============================================================================
// GameObject 實現
// ============================================================================

GameObject::GameObject()
    : name("GameObject")
    , tag("Untagged")
    , layer(0)
    , active(true)
{
}

GameObject::GameObject(const std::string& name)
    : name(name)
    , tag("Untagged")
    , layer(0)
    , active(true)
{
}

GameObject::~GameObject() {
    OnDestroy();
}

void GameObject::SetPosition(const Vector3& position) {
    if (sceneNode) {
        sceneNode->SetPosition(position);
    }
}

Vector3 GameObject::GetPosition() const {
    if (sceneNode) {
        return sceneNode->GetWorldPosition();
    }
    return Vector3::Zero();
}

void GameObject::SetRotation(const Quaternion& rotation) {
    if (sceneNode) {
        sceneNode->SetRotation(rotation);
    }
}

Quaternion GameObject::GetRotation() const {
    if (sceneNode) {
        return sceneNode->GetWorldRotation();
    }
    return Quaternion::Identity();
}

void GameObject::SetScale(const Vector3& scale) {
    if (sceneNode) {
        sceneNode->SetScale(scale);
    }
}

Vector3 GameObject::GetScale() const {
    if (sceneNode) {
        return sceneNode->GetWorldScale();
    }
    return Vector3::One();
}

void GameObject::Awake() {
    // 創建時調用
}

void GameObject::Start() {
    // 第一幀更新前調用
}

void GameObject::Update(float deltaTime) {
    // 每幀調用
}

void GameObject::OnDestroy() {
    // 銷毀時調用
}

void GameObject::RegisterMessageCallback(const std::string& messageType, MessageCallback callback) {
    messageCallbacks[messageType] = callback;
}

void GameObject::SendMessage(const std::string& messageType, void* data) {
    // 處理本地消息
    OnMessage(messageType, data);
    
    // 調用註冊的回調
    auto it = messageCallbacks.find(messageType);
    if (it != messageCallbacks.end()) {
        it->second(messageType, data);
    }
}

void GameObject::OnMessage(const std::string& messageType, void* data) {
    // 子類可以重寫此方法
}

GameObject* GameObject::FindByName(const std::string& name) {
    if (gGameObjectManager) {
        return gGameObjectManager->FindObject(name);
    }
    return nullptr;
}

GameObject* GameObject::FindByTag(const std::string& tag) {
    if (gGameObjectManager) {
        return gGameObjectManager->FindObjectByTag(tag);
    }
    return nullptr;
}

std::vector<GameObject*> GameObject::FindObjectsByTag(const std::string& tag) {
    if (gGameObjectManager) {
        return gGameObjectManager->FindObjectsByTag(tag);
    }
    return std::vector<GameObject*>();
}

// ============================================================================
// GameObjectManager 實現
// ============================================================================

GameObjectManager::GameObjectManager() {
}

GameObjectManager::~GameObjectManager() {
    Clear();
}

GameObject* GameObjectManager::CreateObject(const std::string& name) {
    auto object = MakeUnique<GameObject>(name);
    GameObject* objectPtr = object.get();
    
    objects.push_back(std::move(object));
    nameMap[name] = objectPtr;
    tagMap[objectPtr->GetTag()].push_back(objectPtr);
    
    objectPtr->Awake();
    
    std::cout << "Created GameObject: " << name << std::endl;
    
    return objectPtr;
}

void GameObjectManager::DestroyObject(GameObject* object) {
    if (!object) return;
    
    // 檢查是否已經在待銷毀列表中
    auto it = std::find(pendingDestruction.begin(), pendingDestruction.end(), object);
    if (it != pendingDestruction.end()) {
        return;
    }
    
    pendingDestruction.push_back(object);
}

void GameObjectManager::DestroyObject(const std::string& name) {
    GameObject* object = FindObject(name);
    if (object) {
        DestroyObject(object);
    }
}

GameObject* GameObjectManager::FindObject(const std::string& name) {
    auto it = nameMap.find(name);
    if (it != nameMap.end()) {
        return it->second;
    }
    return nullptr;
}

GameObject* GameObjectManager::FindObjectByTag(const std::string& tag) {
    auto it = tagMap.find(tag);
    if (it != tagMap.end() && !it->second.empty()) {
        return it->second[0];
    }
    return nullptr;
}

std::vector<GameObject*> GameObjectManager::FindObjectsByTag(const std::string& tag) {
    auto it = tagMap.find(tag);
    if (it != tagMap.end()) {
        return it->second;
    }
    return std::vector<GameObject*>();
}

std::vector<GameObject*> GameObjectManager::FindObjectsByLayer(int layer) {
    std::vector<GameObject*> result;
    
    for (auto& object : objects) {
        if (object->GetLayer() == layer) {
            result.push_back(object.get());
        }
    }
    
    return result;
}

void GameObjectManager::UpdateAll(float deltaTime) {
    // 更新所有活動對象
    for (auto& object : objects) {
        if (object->IsActive()) {
            object->Update(deltaTime);
        }
    }
    
    // 銷毀待銷毀的對象
    DestroyPendingObjects();
}

void GameObjectManager::ActivateObject(GameObject* object) {
    if (object) {
        object->SetActive(true);
    }
}

void GameObjectManager::DeactivateObject(GameObject* object) {
    if (object) {
        object->SetActive(false);
    }
}

void GameObjectManager::ActivateAllObjects() {
    for (auto& object : objects) {
        object->SetActive(true);
    }
}

void GameObjectManager::DeactivateAllObjects() {
    for (auto& object : objects) {
        object->SetActive(false);
    }
}

size_t GameObjectManager::GetObjectCount() const {
    return objects.size();
}

size_t GameObjectManager::GetActiveObjectCount() const {
    size_t count = 0;
    for (const auto& object : objects) {
        if (object->IsActive()) {
            count++;
        }
    }
    return count;
}

void GameObjectManager::Clear() {
    nameMap.clear();
    tagMap.clear();
    pendingDestruction.clear();
    objects.clear();
}

void GameObjectManager::DestroyPendingObjects() {
    for (GameObject* object : pendingDestruction) {
        // 從地圖中移除
        nameMap.erase(object->GetName());
        
        // 從標籤地圖中移除
        for (auto& [tag, objects] : tagMap) {
            auto it = std::find(objects.begin(), objects.end(), object);
            if (it != objects.end()) {
                objects.erase(it);
            }
        }
        
        // 從對象列表中移除
        auto it = std::find_if(objects.begin(), objects.end(),
            [object](const UniquePtr<GameObject>& obj) {
                return obj.get() == object;
            });
        
        if (it != objects.end()) {
            std::cout << "Destroyed GameObject: " << object->GetName() << std::endl;
            objects.erase(it);
        }
    }
    
    pendingDestruction.clear();
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeGameObjectManager() {
    if (gGameObjectManager) {
        return false;
    }
    
    gGameObjectManager = new GameObjectManager();
    std::cout << "GameObject Manager initialized" << std::endl;
    
    return true;
}

void ShutdownGameObjectManager() {
    if (gGameObjectManager) {
        delete gGameObjectManager;
        gGameObjectManager = nullptr;
    }
}

GameObjectManager* GetGameObjectManager() {
    return gGameObjectManager;
}

} // namespace Potato