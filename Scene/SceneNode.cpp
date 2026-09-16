#include "SceneNode.h"
#include <algorithm>

namespace Potato {

// 全局場景圖
SceneGraph* gSceneGraph = nullptr;

// ============================================================================
// SceneNode 實現
// ============================================================================

SceneNode::SceneNode()
    : name("Node")
    , active(true)
    , dirty(true)
    , localPosition(Vector3::Zero())
    , localRotation(Quaternion::Identity())
    , localScale(Vector3::One())
    , worldPosition(Vector3::Zero())
    , worldRotation(Quaternion::Identity())
    , worldScale(Vector3::One())
    , worldMatrix(Matrix4::Identity())
    , parent(nullptr)
{
}

SceneNode::SceneNode(const std::string& name)
    : name(name)
    , active(true)
    , dirty(true)
    , localPosition(Vector3::Zero())
    , localRotation(Quaternion::Identity())
    , localScale(Vector3::One())
    , worldPosition(Vector3::Zero())
    , worldRotation(Quaternion::Identity())
    , worldScale(Vector3::One())
    , worldMatrix(Matrix4::Identity())
    , parent(nullptr)
{
}

SceneNode::~SceneNode() {
    RemoveAllChildren();
}

void SceneNode::SetPosition(const Vector3& position) {
    localPosition = position;
    MarkDirty();
}

void SceneNode::SetRotation(const Quaternion& rotation) {
    localRotation = rotation;
    MarkDirty();
}

void SceneNode::SetScale(const Vector3& scale) {
    localScale = scale;
    MarkDirty();
}

void SceneNode::SetLocalPosition(const Vector3& position) {
    localPosition = position;
    MarkDirty();
}

void SceneNode::SetLocalRotation(const Quaternion& rotation) {
    localRotation = rotation;
    MarkDirty();
}

void SceneNode::SetLocalScale(const Vector3& scale) {
    localScale = scale;
    MarkDirty();
}

Vector3 SceneNode::GetWorldPosition() const {
    if (dirty) {
        const_cast<SceneNode*>(this)->UpdateWorldTransform();
    }
    return worldPosition;
}

Quaternion SceneNode::GetWorldRotation() const {
    if (dirty) {
        const_cast<SceneNode*>(this)->UpdateWorldTransform();
    }
    return worldRotation;
}

Vector3 SceneNode::GetWorldScale() const {
    if (dirty) {
        const_cast<SceneNode*>(this)->UpdateWorldTransform();
    }
    return worldScale;
}

Matrix4 SceneNode::GetWorldMatrix() const {
    if (dirty) {
        const_cast<SceneNode*>(this)->UpdateWorldTransform();
    }
    return worldMatrix;
}

Matrix4 SceneNode::GetLocalMatrix() const {
    Matrix4 translation = Matrix4::Translation(localPosition);
    Matrix4 rotation = localRotation.ToMatrix();
    Matrix4 scale = Matrix4::Scale(localScale);
    
    return translation * rotation * scale;
}

void SceneNode::SetParent(SceneNode* newParent) {
    if (parent == newParent) return;
    if (newParent == this) return; // 防止自我循環
    
    // 防止循環：newParent 不能是 this 的子孫（沿 parent 鏈往上找）
    for (SceneNode* p = newParent; p; p = p->parent) {
        if (p == this) return;
    }
    
    // 先取得自身的共享引用：從舊父節點移除時若其為唯一擁有者，
    // 沒有 self 的話 this 會在成員函式中途被刪除
    SharedPtr<SceneNode> self;
    try {
        self = shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        // 節點不由 shared_ptr 管理：只能更新 parent 指標，無法進入 children
    }
    
    // 從舊父節點移除
    if (parent) {
        auto& siblings = parent->children;
        siblings.erase(
            std::remove_if(siblings.begin(), siblings.end(),
                [this](const SharedPtr<SceneNode>& child) {
                    return child.get() == this;
                }),
            siblings.end());
    }
    
    // 設置新父節點
    parent = newParent;
    
    // 添加到新父節點（僅當能取得共享所有權時）
    if (parent && self) {
        parent->children.push_back(std::move(self));
    }
    
    MarkDirty();
}

void SceneNode::AddChild(SharedPtr<SceneNode> child) {
    if (!child || child.get() == this) return;
    
    // 防止循環：this 不能是 child 的子孫（沿 this 的 parent 鏈往上找）
    for (SceneNode* p = this; p; p = p->parent) {
        if (p == child.get()) return;
    }
    
    // 如果子節點已有父節點，先移除
    if (child->parent) {
        child->SetParent(nullptr);
    }
    
    child->parent = this;
    children.push_back(child);
    child->MarkDirty();
}

void SceneNode::RemoveChild(SceneNode* child) {
    if (!child) return;
    
    auto it = std::find_if(children.begin(), children.end(),
        [child](const SharedPtr<SceneNode>& node) {
            return node.get() == child;
        });
    
    if (it != children.end()) {
        (*it)->parent = nullptr;
        children.erase(it);
    }
}

void SceneNode::RemoveAllChildren() {
    for (auto& child : children) {
        child->parent = nullptr;
    }
    children.clear();
}

void SceneNode::Update(float deltaTime) {
    if (!active) return;
    
    // 更新世界變換
    UpdateWorldTransform();
    
    // 更新子節點
    UpdateChildren(deltaTime);
}

void SceneNode::UpdateWorldTransform() {
    // 計算本地變換矩陣
    Matrix4 localMatrix = GetLocalMatrix();
    
    if (parent) {
        // 從父節點繼承變換
        Matrix4 parentMatrix = parent->GetWorldMatrix();
        worldMatrix = parentMatrix * localMatrix;
        
        // 繼承世界變換（簡化版本）
        worldPosition = parent->GetWorldPosition() + localPosition;
        worldRotation = parent->GetWorldRotation() * localRotation;
        worldScale = parent->GetWorldScale() * localScale;
    } else {
        worldMatrix = localMatrix;
        worldPosition = localPosition;
        worldRotation = localRotation;
        worldScale = localScale;
    }
    
    dirty = false;
}

void SceneNode::UpdateChildren(float deltaTime) {
    for (auto& child : children) {
        if (child->IsActive()) {
            child->Update(deltaTime);
        }
    }
}

void SceneNode::MarkDirty() {
    dirty = true;
    
    // 標記所有子節點為dirty
    for (auto& child : children) {
        child->MarkDirty();
    }
}

// ============================================================================
// SceneGraph 實現
// ============================================================================

SceneGraph::SceneGraph()
    : rootNode(MakeShared<SceneNode>("Root"))
    , name("Default Scene")
{
}

SceneGraph::~SceneGraph() {
    Clear();
}

void SceneGraph::SetRootNode(SharedPtr<SceneNode> root) {
    rootNode = root;
}

SceneNode* SceneGraph::FindNode(const std::string& name) {
    return FindNodeByName(name, rootNode.get());
}

SceneNode* SceneGraph::FindNodeByName(const std::string& name, SceneNode* startNode) {
    if (!startNode) {
        startNode = rootNode.get();
    }
    if (!startNode) {
        return nullptr; // rootNode 也為空時直接返回，避免空指標解引用
    }
    
    if (startNode->GetName() == name) {
        return startNode;
    }
    
    for (auto& child : startNode->GetChildren()) {
        SceneNode* found = FindNodeByName(name, child.get());
        if (found) {
            return found;
        }
    }
    
    return nullptr;
}

void SceneGraph::Update(float deltaTime) {
    if (rootNode) {
        rootNode->Update(deltaTime);
    }
}

void SceneGraph::Clear() {
    if (rootNode) {
        rootNode->RemoveAllChildren();
    }
}

size_t SceneGraph::GetNodeCount() const {
    size_t count = 0;
    CountNodes(rootNode.get(), count);
    return count;
}

void SceneGraph::CountNodes(SceneNode* node, size_t& count) const {
    if (!node) return;
    
    count++;
    
    for (auto& child : node->GetChildren()) {
        CountNodes(child.get(), count);
    }
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeSceneGraph() {
    if (gSceneGraph) {
        return false;
    }
    
    gSceneGraph = new SceneGraph();
    return true;
}

void ShutdownSceneGraph() {
    if (gSceneGraph) {
        delete gSceneGraph;
        gSceneGraph = nullptr;
    }
}

SceneGraph* GetSceneGraph() {
    return gSceneGraph;
}

} // namespace Potato
