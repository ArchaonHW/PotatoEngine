#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Quaternion.h"
#include "MathUtils/Matrix4.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace Potato {

/**
 * 物理物體類型
 */
enum class PhysicsBodyType {
    Static,      // 靜態物體（不受重力影響，不會移動）
    Kinematic,   // 運動學物體（可以移動，但不受物理碰撞影響）
    Dynamic     // 動態物體（受重力和碰撞影響）
};

/**
 * 碰撞形狀類型
 */
enum class CollisionShape {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Plane,
    Mesh,
    HeightField
};

/**
 * 物理材質
 */
struct PhysicsMaterial {
    float restitution;    // 彈性係數 (0.0 - 1.0)
    float friction;       // 摩擦係數 (0.0 - 1.0)
    float density;        // 密度
    float rollingFriction; // 滾動摩擦
    float spinningFriction; // 旋轉摩擦
    
    PhysicsMaterial()
        : restitution(0.5f)
        , friction(0.5f)
        , density(1.0f)
        , rollingFriction(0.0f)
        , spinningFriction(0.0f)
    {
    }
};

/**
 * 碰撞數據
 */
struct CollisionData {
    Vector3 position;
    Vector3 normal;
    float penetrationDepth;
    int otherBodyID;  // 舊版相容欄位：等同 bodyBID
    int bodyAID;      // 碰撞對的第一個物體
    int bodyBID;      // 碰撞對的第二個物體
    
    CollisionData()
        : position(Vector3::Zero())
        , normal(Vector3::Zero())
        , penetrationDepth(0.0f)
        , otherBodyID(-1)
        , bodyAID(-1)
        , bodyBID(-1)
    {
    }
};

/**
 * 碰撞回調類型
 */
using CollisionCallback = std::function<void(const CollisionData&)>;

/**
 * 物理物體
 */
class PhysicsBody {
public:
    PhysicsBody();
    ~PhysicsBody();
    
    // 基本屬性
    void SetBodyType(PhysicsBodyType type);
    PhysicsBodyType GetBodyType() const { return bodyType; }
    
    void SetPosition(const Vector3& position);
    Vector3 GetPosition() const { return position; }
    
    void SetRotation(const Quaternion& rotation);
    Quaternion GetRotation() const { return rotation; }
    
    void SetScale(const Vector3& scale);
    Vector3 GetScale() const { return scale; }
    
    // 物理屬性
    void SetMass(float mass);
    float GetMass() const { return mass; }
    
    void SetLinearVelocity(const Vector3& velocity);
    Vector3 GetLinearVelocity() const { return linearVelocity; }
    
    void SetAngularVelocity(const Vector3& velocity);
    Vector3 GetAngularVelocity() const { return angularVelocity; }
    
    void SetLinearDamping(float damping);
    float GetLinearDamping() const { return linearDamping; }
    
    void SetAngularDamping(float damping);
    float GetAngularDamping() const { return angularDamping; }
    
    // 碰撞形狀
    void SetCollisionShape(CollisionShape shape);
    CollisionShape GetCollisionShape() const { return collisionShape; }
    
    void SetCollisionShapeDimensions(const Vector3& dimensions);
    Vector3 GetCollisionShapeDimensions() const { return shapeDimensions; }
    
    // 材質
    void SetMaterial(const PhysicsMaterial& material);
    PhysicsMaterial GetMaterial() const { return material; }
    
    // 碰撞過濾
    void SetCollisionGroup(int group);
    int GetCollisionGroup() const { return collisionGroup; }
    
    void SetCollisionMask(int mask);
    int GetCollisionMask() const { return collisionMask; }
    
    // 碰撞回調
    void SetCollisionCallback(CollisionCallback callback);
    
    // 力和力矩
    void ApplyForce(const Vector3& force);
    void ApplyForceAtPoint(const Vector3& force, const Vector3& point);
    void ApplyTorque(const Vector3& torque);
    void ApplyImpulse(const Vector3& impulse);
    void ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& point);
    
    // 約束
    void SetKinematic(bool kinematic);
    bool IsKinematic() const { return kinematic; }
    
    void SetGravityEnabled(bool enabled);
    bool IsGravityEnabled() const { return gravityEnabled; }
    
    // 唯一標識
    int GetBodyID() const { return bodyID; }
    
    // 轉換矩陣
    Matrix4 GetTransformMatrix() const;
    
private:
    friend class PhysicsWorld;
    int bodyID;
    
    PhysicsBodyType bodyType;
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
    
    float mass;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    Vector3 accumulatedForce;   // 本步累積的力，IntegrateVelocity 時以 dt 積分後清零
    float linearDamping;
    float angularDamping;
    
    CollisionShape collisionShape;
    Vector3 shapeDimensions;
    PhysicsMaterial material;
    
    int collisionGroup;
    int collisionMask;
    
    CollisionCallback collisionCallback;
    
    bool kinematic;
    bool gravityEnabled;
    
    static int nextBodyID;
};

/**
 * 物理世界
 */
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    
    // 初始化和關閉
    bool Initialize();
    void Shutdown();
    
    // 模擬
    void Step(float deltaTime);
    void SetGravity(const Vector3& gravity);
    Vector3 GetGravity() const { return gravity; }
    
    // 物體管理
    PhysicsBody* CreateBody();
    void DestroyBody(PhysicsBody* body);
    void DestroyAllBodies();
    
    PhysicsBody* GetBody(int bodyID);
    const std::vector<PhysicsBody*>& GetAllBodies() const { return bodies; }
    
    // 碰撞檢測
    void EnableCollisionDetection(bool enable);
    bool IsCollisionDetectionEnabled() const { return collisionDetectionEnabled; }
    
    void SetCollisionIterations(int iterations);
    int GetCollisionIterations() const { return collisionIterations; }
    
    // 全局物理設置
    void SetSubSteps(int subSteps);
    int GetSubSteps() const { return subSteps; }
    
    void SetFixedTimeStep(float timeStep);
    float GetFixedTimeStep() const { return fixedTimeStep; }
    
    // 碰撞回調
    void SetGlobalCollisionCallback(CollisionCallback callback);
    
    // 射線檢測
    bool Raycast(const Vector3& from, const Vector3& to, CollisionData& result);
    
    // 性能統計
    int GetBodyCount() const { return static_cast<int>(bodies.size()); }
    int GetCollisionCount() const { return collisionCount; }
    
private:
    void UpdateBodies(float deltaTime);
    void DetectCollisions();
    void ResolveCollisions();
    void IntegrateVelocity(float deltaTime);
    void IntegratePosition(float deltaTime);
    
private:
    Vector3 gravity;
    bool collisionDetectionEnabled;
    int collisionIterations;
    int subSteps;
    float fixedTimeStep;
    
    std::vector<PhysicsBody*> bodies;
    std::vector<CollisionData> activeCollisions;
    
    CollisionCallback globalCollisionCallback;
    
    int collisionCount;
    bool initialized;
    
    float accumulatedTime;
};

/**
 * 物理管理器（全局單例）
 */
class PhysicsManager {
public:
    static PhysicsManager& GetInstance();
    
    PhysicsWorld* CreateWorld(const std::string& name);
    PhysicsWorld* GetWorld(const std::string& name);
    void SetActiveWorld(const std::string& name);
    PhysicsWorld* GetActiveWorld();
    
    void DestroyWorld(const std::string& name);
    void DestroyAllWorlds();
    
    // 全局物理設置
    void SetDefaultGravity(const Vector3& gravity);
    Vector3 GetDefaultGravity() const { return defaultGravity; }
    
private:
    PhysicsManager();
    ~PhysicsManager();
    
    std::unordered_map<std::string, UniquePtr<PhysicsWorld>> worlds;
    PhysicsWorld* activeWorld;
    
    Vector3 defaultGravity;
};

// 全局物理管理器
extern PhysicsManager* gPhysicsManager;

/**
 * 初始化全局物理管理器
 */
bool InitializePhysicsManager();

/**
 * 關閉全局物理管理器
 */
void ShutdownPhysicsManager();

/**
 * 獲取全局物理管理器
 */
PhysicsManager* GetPhysicsManager();

} // namespace Potato

// 便捷宏
#define GET_PHYSICS() Potato::GetPhysicsManager()