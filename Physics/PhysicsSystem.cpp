#include "PhysicsSystem.h"
#include "Logging/Logger.h"
#include <cmath>

namespace Potato {

// ============================================================================
// PhysicsBody 實現
// ============================================================================

int PhysicsBody::nextBodyID = 0;

PhysicsBody::PhysicsBody()
    : bodyID(nextBodyID++)
    , bodyType(PhysicsBodyType::Dynamic)
    , position(Vector3::Zero())
    , rotation(Quaternion::Identity())
    , scale(Vector3::One())
    , mass(1.0f)
    , linearVelocity(Vector3::Zero())
    , angularVelocity(Vector3::Zero())
    , linearDamping(0.0f)
    , angularDamping(0.0f)
    , collisionShape(CollisionShape::Box)
    , shapeDimensions(Vector3::One())
    , collisionGroup(1)
    , collisionMask(1)
    , kinematic(false)
    , gravityEnabled(true)
{
}

PhysicsBody::~PhysicsBody() {
}

void PhysicsBody::SetBodyType(PhysicsBodyType type) {
    bodyType = type;
}

void PhysicsBody::SetPosition(const Vector3& pos) {
    position = pos;
}

void PhysicsBody::SetRotation(const Quaternion& rot) {
    rotation = rot;
}

void PhysicsBody::SetScale(const Vector3& s) {
    scale = s;
}

void PhysicsBody::SetMass(float m) {
    mass = m;
}

void PhysicsBody::SetLinearVelocity(const Vector3& velocity) {
    linearVelocity = velocity;
}

void PhysicsBody::SetAngularVelocity(const Vector3& velocity) {
    angularVelocity = velocity;
}

void PhysicsBody::SetLinearDamping(float damping) {
    linearDamping = damping;
}

void PhysicsBody::SetAngularDamping(float damping) {
    angularDamping = damping;
}

void PhysicsBody::SetCollisionShape(CollisionShape shape) {
    collisionShape = shape;
}

void PhysicsBody::SetCollisionShapeDimensions(const Vector3& dimensions) {
    shapeDimensions = dimensions;
}

void PhysicsBody::SetMaterial(const PhysicsMaterial& mat) {
    material = mat;
}

void PhysicsBody::SetCollisionGroup(int group) {
    collisionGroup = group;
}

void PhysicsBody::SetCollisionMask(int mask) {
    collisionMask = mask;
}

void PhysicsBody::SetCollisionCallback(CollisionCallback callback) {
    collisionCallback = callback;
}

void PhysicsBody::ApplyForce(const Vector3& force) {
    if (bodyType == PhysicsBodyType::Dynamic) {
        linearVelocity += force / mass;
    }
}

void PhysicsBody::ApplyForceAtPoint(const Vector3& force, const Vector3& point) {
    // 簡化實現：應用力到質心
    ApplyForce(force);
}

void PhysicsBody::ApplyTorque(const Vector3& torque) {
    if (bodyType == PhysicsBodyType::Dynamic) {
        angularVelocity += torque;
    }
}

void PhysicsBody::ApplyImpulse(const Vector3& impulse) {
    if (bodyType == PhysicsBodyType::Dynamic) {
        linearVelocity += impulse / mass;
    }
}

void PhysicsBody::ApplyImpulseAtPoint(const Vector3& impulse, const Vector3& point) {
    // 簡化實現
    ApplyImpulse(impulse);
}

void PhysicsBody::SetKinematic(bool kinematic) {
    this->kinematic = kinematic;
}

void PhysicsBody::SetGravityEnabled(bool enabled) {
    gravityEnabled = enabled;
}

Matrix4 PhysicsBody::GetTransformMatrix() const {
    Matrix4 translation = Matrix4::Translation(position);
    Matrix4 rotationMatrix = rotation.ToMatrix();
    Matrix4 scaleMatrix = Matrix4::Scale(scale);
    
    return translation * rotationMatrix * scaleMatrix;
}

// ============================================================================
// PhysicsWorld 實現
// ============================================================================

PhysicsWorld::PhysicsWorld()
    : gravity(Vector3(0.0f, -9.81f, 0.0f))
    , collisionDetectionEnabled(true)
    , collisionIterations(10)
    , subSteps(1)
    , fixedTimeStep(1.0f / 60.0f)
    , collisionCount(0)
    , initialized(false)
    , accumulatedTime(0.0f)
{
}

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

bool PhysicsWorld::Initialize() {
    if (initialized) {
        return true;
    }
    
    LOG_INFO("Initializing Physics World...");
    
    initialized = true;
    LOG_INFO("Physics World initialized successfully");
    
    return true;
}

void PhysicsWorld::Shutdown() {
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Shutting down Physics World...");
    
    DestroyAllBodies();
    
    initialized = false;
    LOG_INFO("Physics World shutdown complete");
}

void PhysicsWorld::Step(float deltaTime) {
    if (!initialized) {
        return;
    }
    
    // 固定時間步長
    accumulatedTime += deltaTime;
    
    while (accumulatedTime >= fixedTimeStep) {
        float stepTime = fixedTimeStep / subSteps;
        
        for (int i = 0; i < subSteps; i++) {
            UpdateBodies(stepTime);
            
            if (collisionDetectionEnabled) {
                DetectCollisions();
                ResolveCollisions();
            }
        }
        
        accumulatedTime -= fixedTimeStep;
    }
}

void PhysicsWorld::SetGravity(const Vector3& grav) {
    gravity = grav;
}

PhysicsBody* PhysicsWorld::CreateBody() {
    auto body = MakeUnique<PhysicsBody>();
    PhysicsBody* bodyPtr = body.get();
    bodies.push_back(bodyPtr);
    
    LOG_INFO("Created physics body with ID: " + std::to_string(bodyPtr->GetBodyID()));
    
    return bodyPtr;
}

void PhysicsWorld::DestroyBody(PhysicsBody* body) {
    auto it = std::find(bodies.begin(), bodies.end(), body);
    if (it != bodies.end()) {
        bodies.erase(it);
        delete body;
        LOG_INFO("Destroyed physics body with ID: " + std::to_string(body->GetBodyID()));
    }
}

void PhysicsWorld::DestroyAllBodies() {
    for (auto body : bodies) {
        delete body;
    }
    bodies.clear();
    LOG_INFO("Destroyed all physics bodies");
}

PhysicsBody* PhysicsWorld::GetBody(int bodyID) {
    for (auto body : bodies) {
        if (body->GetBodyID() == bodyID) {
            return body;
        }
    }
    return nullptr;
}

void PhysicsWorld::EnableCollisionDetection(bool enable) {
    collisionDetectionEnabled = enable;
    LOG_INFO("Collision detection " + std::string(enable ? "enabled" : "disabled"));
}

void PhysicsWorld::SetCollisionIterations(int iterations) {
    collisionIterations = iterations;
}

int PhysicsWorld::GetCollisionIterations() const {
    return collisionIterations;
}

void PhysicsWorld::SetSubSteps(int steps) {
    subSteps = steps;
}

int PhysicsWorld::GetSubSteps() const {
    return subSteps;
}

void PhysicsWorld::SetFixedTimeStep(float timeStep) {
    fixedTimeStep = timeStep;
}

float PhysicsWorld::GetFixedTimeStep() const {
    return fixedTimeStep;
}

void PhysicsWorld::SetGlobalCollisionCallback(CollisionCallback callback) {
    globalCollisionCallback = callback;
}

bool PhysicsWorld::Raycast(const Vector3& from, const Vector3& to, CollisionData& result) {
    // 簡化實現：線段與 AABB 碰撞檢測
    Vector3 direction = to - from;
    float distance = direction.Length();
    direction.Normalize();
    
    for (auto body : bodies) {
        if (body->GetBodyType() == PhysicsBodyType::Static) {
            continue;
        }
        
        // 簡化碰撞檢測
        Vector3 bodyPos = body->GetPosition();
        Vector3 toBody = bodyPos - from;
        float bodyDistance = toBody.Length();
        
        if (bodyDistance < distance) {
            result.position = bodyPos;
            result.normal = (from - bodyPos).Normalized();
            result.penetrationDepth = distance - bodyDistance;
            result.otherBodyID = body->GetBodyID();
            
            return true;
        }
    }
    
    return false;
}

void PhysicsWorld::UpdateBodies(float deltaTime) {
    for (auto body : bodies) {
        if (body->GetBodyType() == PhysicsBodyType::Static) {
            continue;
        }
        
        if (body->IsKinematic()) {
            // 運動學物體：直接設置速度
            continue;
        }
        
        // 應用重力
        if (body->IsGravityEnabled()) {
            body->ApplyForce(gravity * body->GetMass());
        }
        
        // 應用阻尼
        body->SetLinearVelocity(body->GetLinearVelocity() * (1.0f - body->GetLinearDamping() * deltaTime));
        body->SetAngularVelocity(body->GetAngularVelocity() * (1.0f - body->GetAngularDamping() * deltaTime));
        
        // 積分速度
        IntegrateVelocity(deltaTime);
    }
}

void PhysicsWorld::DetectCollisions() {
    activeCollisions.clear();
    collisionCount = 0;
    
    // 簡化實現：O(N^2) 碰撞檢測
    for (size_t i = 0; i < bodies.size(); i++) {
        for (size_t j = i + 1; j < bodies.size(); j++) {
            PhysicsBody* bodyA = bodies[i];
            PhysicsBody* bodyB = bodies[j];
            
            // 檢查碰撞過濾
            if ((bodyA->GetCollisionGroup() & bodyB->GetCollisionMask()) == 0) {
                continue;
            }
            
            // 簡化碰撞檢測：球體碰撞
            Vector3 diff = bodyA->GetPosition() - bodyB->GetPosition();
            float distance = diff.Length();
            float minDistance = 1.0f; // 簡化
            
            if (distance < minDistance) {
                CollisionData collision;
                collision.position = (bodyA->GetPosition() + bodyB->GetPosition()) * 0.5f;
                collision.normal = diff.Normalized();
                collision.penetrationDepth = minDistance - distance;
                collision.otherBodyID = bodyB->GetBodyID();
                
                activeCollisions.push_back(collision);
                collisionCount++;
                
                // 觸發回調
                if (bodyA->collisionCallback) {
                    bodyA->collisionCallback(collision);
                }
                
                if (globalCollisionCallback) {
                    globalCollisionCallback(collision);
                }
            }
        }
    }
}

void PhysicsWorld::ResolveCollisions() {
    // 簡化實現：分離軸定理
    for (const auto& collision : activeCollisions) {
        PhysicsBody* bodyA = GetBody(collision.otherBodyID);
        if (!bodyA) continue;
        
        PhysicsBody* bodyB = GetBody(collision.otherBodyID); // 需要正確的實現
        if (!bodyB) continue;
        
        // 分離物體
        Vector3 separation = collision.normal * collision.penetrationDepth * 0.5f;
        
        if (bodyA->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyA->SetPosition(bodyA->GetPosition() + separation);
        }
        
        if (bodyB->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyB->SetPosition(bodyB->GetPosition() - separation);
        }
        
        // 計算衝量
        float restitution = 0.5f; // 簡化
        Vector3 relativeVelocity = bodyA->GetLinearVelocity() - bodyB->GetLinearVelocity();
        float velocityAlongNormal = Vector3::Dot(relativeVelocity, collision.normal);
        
        if (velocityAlongNormal > 0) {
            continue; // 物體正在分離
        }
        
        float j = -(1 + restitution) * velocityAlongNormal;
        j /= (1.0f / bodyA->GetMass() + 1.0f / bodyB->GetMass());
        
        Vector3 impulse = collision.normal * j;
        
        if (bodyA->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyA->ApplyImpulse(impulse);
        }
        
        if (bodyB->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyB->ApplyImpulse(-impulse);
        }
    }
}

void PhysicsWorld::IntegrateVelocity(float deltaTime) {
    for (auto body : bodies) {
        if (body->GetBodyType() == PhysicsBodyType::Dynamic) {
            IntegratePosition(deltaTime);
        }
    }
}

void PhysicsWorld::IntegratePosition(float deltaTime) {
    for (auto body : bodies) {
        if (body->GetBodyType() == PhysicsBodyType::Dynamic) {
            Vector3 position = body->GetPosition();
            Vector3 velocity = body->GetLinearVelocity();
            
            position += velocity * deltaTime;
            body->SetPosition(position);
        }
    }
}

// ============================================================================
// PhysicsManager 實現
// ============================================================================

PhysicsManager::PhysicsManager()
    : activeWorld(nullptr)
    , defaultGravity(Vector3(0.0f, -9.81f, 0.0f))
{
}

PhysicsManager::~PhysicsManager() {
    DestroyAllWorlds();
}

PhysicsManager& PhysicsManager::GetInstance() {
    static PhysicsManager instance;
    return instance;
}

PhysicsWorld* PhysicsManager::CreateWorld(const std::string& name) {
    auto world = MakeUnique<PhysicsWorld>();
    world->Initialize();
    world->SetGravity(defaultGravity);
    
    PhysicsWorld* worldPtr = world.get();
    worlds[name] = std::move(world);
    
    if (!activeWorld) {
        activeWorld = worldPtr;
    }
    
    LOG_INFO("Created physics world: " + name);
    return worldPtr;
}

PhysicsWorld* PhysicsManager::GetWorld(const std::string& name) {
    auto it = worlds.find(name);
    if (it != worlds.end()) {
        return it->second.get();
    }
    return nullptr;
}

void PhysicsManager::SetActiveWorld(const std::string& name) {
    PhysicsWorld* world = GetWorld(name);
    if (world) {
        activeWorld = world;
        LOG_INFO("Set active physics world: " + name);
    }
}

PhysicsWorld* PhysicsManager::GetActiveWorld() {
    return activeWorld;
}

void PhysicsManager::DestroyWorld(const std::string& name) {
    auto it = worlds.find(name);
    if (it != worlds.end()) {
        if (it->second.get() == activeWorld) {
            activeWorld = nullptr;
        }
        worlds.erase(it);
        LOG_INFO("Destroyed physics world: " + name);
    }
}

void PhysicsManager::DestroyAllWorlds() {
    worlds.clear();
    activeWorld = nullptr;
    LOG_INFO("Destroyed all physics worlds");
}

void PhysicsManager::SetDefaultGravity(const Vector3& gravity) {
    defaultGravity = gravity;
}

// ============================================================================
// 全局函數
// ============================================================================

PhysicsManager* gPhysicsManager = nullptr;

bool InitializePhysicsManager() {
    if (!gPhysicsManager) {
        gPhysicsManager = &PhysicsManager::GetInstance();
    }
    return true;
}

void ShutdownPhysicsManager() {
    if (gPhysicsManager) {
        gPhysicsManager->DestroyAllWorlds();
    }
}

PhysicsManager* GetPhysicsManager() {
    if (!gPhysicsManager) {
        gPhysicsManager = &PhysicsManager::GetInstance();
    }
    return gPhysicsManager;
}

} // namespace Potato