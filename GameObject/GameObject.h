#pragma once

#include "Core/CoreTypes.h"
#include "ECS/Entity.h"
#include "Scene/SceneNode.h"
#include <string>
#include <memory>
#include <functional>

namespace Potato {

/**
 * 遊戲對象基類
 * 結合 ECS 實體和場景節點
 */
class GameObject {
public:
    GameObject();
    explicit GameObject(const std::string& name);
    virtual ~GameObject();
    
    // 對象信息
    void SetName(const std::string& name) { this->name = name; }
    const std::string& GetName() const { return name; }
    
    void SetTag(const std::string& tag) { this->tag = tag; }
    const std::string& GetTag() const { return tag; }
    
    void SetLayer(int layer) { this->layer = layer; }
    int GetLayer() const { return layer; }
    
    // 活動狀態
    void SetActive(bool active) { this->active = active; }
    bool IsActive() const { return active; }
    
    // ECS 實體集成
    void SetEntity(ECS::Entity entity) { this->entity = entity; }
    ECS::Entity GetEntity() const { return entity; }
    bool HasEntity() const { return entity.IsValid(); }
    
    // 場景節點集成
    void SetSceneNode(SharedPtr<SceneNode> node) { this->sceneNode = node; }
    SharedPtr<SceneNode> GetSceneNode() const { return sceneNode; }
    
    // 變換
    void SetPosition(const Vector3& position);
    Vector3 GetPosition() const;
    
    void SetRotation(const Quaternion& rotation);
    Quaternion GetRotation() const;
    
    void SetScale(const Vector3& scale);
    Vector3 GetScale() const;
    
    // 生命周期
    virtual void Awake();
    virtual void Start();
    virtual void Update(float deltaTime);
    virtual void OnDestroy();
    
    // 組件訪問
    template<typename T>
    T* GetComponent() {
        if (!HasEntity()) return nullptr;
        // 需要訪問 ECS 協調器
        return nullptr;
    }
    
    template<typename T>
    void AddComponent(T component) {
        if (!HasEntity()) return;
        // 需要訪問 ECS 協調器
    }
    
    template<typename T>
    void RemoveComponent() {
        if (!HasEntity()) return;
        // 需要訪問 ECS 協調器
    }
    
    // 對象間通信
    using MessageCallback = std::function<void(const std::string&, void*)>;
    void RegisterMessageCallback(const std::string& messageType, MessageCallback callback);
    void SendMessage(const std::string& messageType, void* data = nullptr);
    
    // 對象查詢
    static GameObject* FindByName(const std::string& name);
    static GameObject* FindByTag(const std::string& tag);
    static std::vector<GameObject*> FindObjectsByTag(const std::string& tag);
    
protected:
    // 處理消息
    virtual void OnMessage(const std::string& messageType, void* data);
    
private:
    std::string name;
    std::string tag;
    int layer;
    bool active;
    
    ECS::Entity entity;
    SharedPtr<SceneNode> sceneNode;
    
    std::unordered_map<std::string, MessageCallback> messageCallbacks;
};

/**
 * 遊戲對象管理器
 * 管理所有遊戲對象的生命周期
 */
class GameObjectManager {
public:
    GameObjectManager();
    ~GameObjectManager();
    
    // 對象管理
    GameObject* CreateObject(const std::string& name);
    void DestroyObject(GameObject* object);
    void DestroyObject(const std::string& name);
    
    // 對象查詢
    GameObject* FindObject(const std::string& name);
    GameObject* FindObjectByTag(const std::string& tag);
    std::vector<GameObject*> FindObjectsByTag(const std::string& tag);
    std::vector<GameObject*> FindObjectsByLayer(int layer);
    
    // 對象更新
    void UpdateAll(float deltaTime);
    
    // 對象操作
    void ActivateObject(GameObject* object);
    void DeactivateObject(GameObject* object);
    void ActivateAllObjects();
    void DeactivateAllObjects();
    
    // 統計信息
    size_t GetObjectCount() const;
    size_t GetActiveObjectCount() const;
    
    // 清理
    void Clear();
    
private:
    void DestroyPendingObjects();
    
private:
    std::vector<UniquePtr<GameObject>> objects;
    std::vector<GameObject*> pendingDestruction;
    std::unordered_map<std::string, GameObject*> nameMap;
    std::unordered_map<std::string, std::vector<GameObject*>> tagMap;
};

// 全局遊戲對象管理器
extern GameObjectManager* gGameObjectManager;

/**
 * 初始化全局遊戲對象管理器
 */
bool InitializeGameObjectManager();

/**
 * 關閉全局遊戲對象管理器
 */
void ShutdownGameObjectManager();

/**
 * 獲取全局遊戲對象管理器
 */
GameObjectManager* GetGameObjectManager();

} // namespace Potato

// 便捷宏
#define GET_GAME_OBJECT_MANAGER() Potato::GetGameObjectManager()