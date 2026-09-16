#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Quaternion.h"
#include <vector>
#include <memory>
#include <string>

namespace Potato {

/**
 * 變點類
 * 場景圖中的基本節點，包含變換信息
 */
// enable_shared_from_this：SetParent 需要安全地取得自身的 SharedPtr
// （直接 SharedPtr<SceneNode>(this) 會建立第二個控制塊導致雙重釋放）
class SceneNode : public std::enable_shared_from_this<SceneNode> {
public:
    SceneNode();
    explicit SceneNode(const std::string& name);
    virtual ~SceneNode();
    
    // 變換操作
    void SetPosition(const Vector3& position);
    void SetRotation(const Quaternion& rotation);
    void SetScale(const Vector3& scale);
    
    Vector3 GetPosition() const { return localPosition; }
    Quaternion GetRotation() const { return localRotation; }
    Vector3 GetScale() const { return localScale; }
    
    void SetLocalPosition(const Vector3& position);
    void SetLocalRotation(const Quaternion& rotation);
    void SetLocalScale(const Vector3& scale);
    
    Vector3 GetLocalPosition() const { return localPosition; }
    Quaternion GetLocalRotation() const { return localRotation; }
    Vector3 GetLocalScale() const { return localScale; }
    
    // 世界變換
    Vector3 GetWorldPosition() const;
    Quaternion GetWorldRotation() const;
    Vector3 GetWorldScale() const;
    Matrix4 GetWorldMatrix() const;
    Matrix4 GetLocalMatrix() const;
    
    // 節點層次
    void SetParent(SceneNode* parent);
    SceneNode* GetParent() const { return parent; }
    
    void AddChild(SharedPtr<SceneNode> child);
    void RemoveChild(SceneNode* child);
    void RemoveAllChildren();
    
    const std::vector<SharedPtr<SceneNode>>& GetChildren() const { return children; }
    size_t GetChildCount() const { return children.size(); }
    
    // 節點信息
    void SetName(const std::string& name) { this->name = name; }
    const std::string& GetName() const { return name; }
    
    void SetActive(bool active) { this->active = active; }
    bool IsActive() const { return active; }
    
    // 更新
    virtual void Update(float deltaTime);
    virtual void UpdateWorldTransform();
    
    // 標記為需要更新
    void MarkDirty();
    bool IsDirty() const { return dirty; }
    
protected:
    void UpdateChildren(float deltaTime);
    
protected:
    std::string name;
    bool active;
    bool dirty;
    
    // 本地變換
    Vector3 localPosition;
    Quaternion localRotation;
    Vector3 localScale;
    
    // 世界變換
    Vector3 worldPosition;
    Quaternion worldRotation;
    Vector3 worldScale;
    Matrix4 worldMatrix;
    
    // 層次結構
    SceneNode* parent;
    std::vector<SharedPtr<SceneNode>> children;
};

/**
 * 場景圖類
 * 管理場景中的所有節點
 */
class SceneGraph {
public:
    SceneGraph();
    ~SceneGraph();
    
    // 根節點管理
    void SetRootNode(SharedPtr<SceneNode> root);
    SharedPtr<SceneNode> GetRootNode() const { return rootNode; }
    
    // 節點查找
    SceneNode* FindNode(const std::string& name);
    SceneNode* FindNodeByName(const std::string& name, SceneNode* startNode = nullptr);
    
    // 場景更新
    void Update(float deltaTime);
    
    // 場景操作
    void Clear();
    size_t GetNodeCount() const;
    
    // 場景信息
    const std::string& GetName() const { return name; }
    void SetName(const std::string& name) { this->name = name; }
    
private:
    void CountNodes(SceneNode* node, size_t& count) const;
    
private:
    SharedPtr<SceneNode> rootNode;
    std::string name;
};

// 全局場景圖
extern SceneGraph* gSceneGraph;

/**
 * 初始化全局場景圖
 */
bool InitializeSceneGraph();

/**
 * 關閉全局場景圖
 */
void ShutdownSceneGraph();

/**
 * 獲取全局場景圖
 */
SceneGraph* GetSceneGraph();

} // namespace Potato

// 便捷宏
#define GET_SCENE() Potato::GetSceneGraph()
