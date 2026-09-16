#include "PhysicsSystem.h"
#include "Logging/Logger.h"
#include <cmath>
#include <algorithm>

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
    , accumulatedForce(Vector3::Zero())
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
    // 力是連續量：累積到本步，IntegrateVelocity 時乘 dt 積分進速度
    // （瞬間改變速度請用 ApplyImpulse）
    if (bodyType == PhysicsBodyType::Dynamic) {
        accumulatedForce += force;
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
    if (bodyType == PhysicsBodyType::Dynamic && mass > 0.0f) {
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
    PhysicsBody* bodyPtr = new PhysicsBody();
    bodies.push_back(bodyPtr);
    
    LOG_INFO("Created physics body with ID: " + std::to_string(bodyPtr->GetBodyID()));
    
    return bodyPtr;
}

void PhysicsWorld::DestroyBody(PhysicsBody* body) {
    auto it = std::find(bodies.begin(), bodies.end(), body);
    if (it != bodies.end()) {
        int bodyID = body->GetBodyID(); // 先取出 ID：delete 後再讀是 UAF
        bodies.erase(it);
        delete body;
        LOG_INFO("Destroyed physics body with ID: " + std::to_string(bodyID));
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
    collisionIterations = (iterations < 1) ? 1 : iterations;
}

void PhysicsWorld::SetSubSteps(int steps) {
    subSteps = (steps < 1) ? 1 : steps; // Step() 有 fixedTimeStep / subSteps，不可為 0
}

void PhysicsWorld::SetFixedTimeStep(float timeStep) {
    fixedTimeStep = (timeStep > 0.0f) ? timeStep : (1.0f / 60.0f);
}

void PhysicsWorld::SetGlobalCollisionCallback(CollisionCallback callback) {
    globalCollisionCallback = callback;
}

bool PhysicsWorld::Raycast(const Vector3& from, const Vector3& to, CollisionData& result) {
    // 線段-球體測試：檢查線段上最近點到物體中心的距離
    Vector3 direction = to - from;
    float distance = direction.Length();
    if (distance <= 1e-6f) {
        return false;
    }
    direction = direction / distance; // Normalize() 回傳副本，需重新賦值
    
    PhysicsBody* closestBody = nullptr;
    float closestProj = distance;
    Vector3 closestPoint;
    float closestDist = 0.0f;
    
    for (auto body : bodies) {
        Vector3 bodyPos = body->GetPosition();
        float radius = body->GetCollisionShapeDimensions().x * 0.5f;
        if (radius <= 0.0f) {
            radius = 0.5f;
        }
        
        Vector3 toBody = bodyPos - from;
        float proj = toBody.Dot(direction);
        if (proj < 0.0f || proj > closestProj) {
            continue; // 在射線反方向，或比已找到的命中更遠
        }
        
        Vector3 pointOnRay = from + direction * proj;
        float distToCenter = (bodyPos - pointOnRay).Length();
        if (distToCenter <= radius) {
            closestBody = body;
            closestProj = proj;
            closestPoint = pointOnRay;
            closestDist = distToCenter;
        }
    }
    
    if (!closestBody) {
        return false;
    }
    
    result.position = closestPoint;
    // 法線方向：從物體中心指向命中點（頂著射線方向）
    Vector3 n = closestPoint - closestBody->GetPosition();
    result.normal = (n.Length() > 1e-6f) ? n.Normalized() : Vector3(0.0f, 1.0f, 0.0f);
    result.penetrationDepth = closestDist;
    result.otherBodyID = closestBody->GetBodyID();
    result.bodyBID = closestBody->GetBodyID();
    
    return true;
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
    }
    
    // 積分：先速度（累積力 -> 速度）再位置（速度 -> 位置）
    // 兩者內部皆已遍歷所有物體,不可放在每物體迴圈內
    IntegrateVelocity(deltaTime);
    IntegratePosition(deltaTime);
}

namespace {
// 簡化碰撞檢測用包圍球半徑:Sphere 直接取 dimensions.x,其他形狀取最大邊一半
float BoundingRadius(const PhysicsBody* body) {
    const Vector3& d = body->GetCollisionShapeDimensions();
    if (body->GetCollisionShape() == CollisionShape::Sphere) {
        return (d.x > 0.0f) ? d.x : 0.5f;
    }
    float maxDim = std::max({d.x, d.y, d.z});
    return (maxDim > 0.0f) ? maxDim * 0.5f : 0.5f;
}
} // namespace

void PhysicsWorld::DetectCollisions() {
    activeCollisions.clear();
    collisionCount = 0;
    
    // 快照：碰撞回調可能呼叫 DestroyBody/CreateBody 導致迭代器失效
    std::vector<PhysicsBody*> snapshot = bodies;
    
    // 簡化實現：O(N^2) 碰撞檢測
    for (size_t i = 0; i < snapshot.size(); i++) {
        for (size_t j = i + 1; j < snapshot.size(); j++) {
            PhysicsBody* bodyA = snapshot[i];
            PhysicsBody* bodyB = snapshot[j];
            
            // 回調可能已銷毀物體，確認仍屬於世界
            if (std::find(bodies.begin(), bodies.end(), bodyA) == bodies.end() ||
                std::find(bodies.begin(), bodies.end(), bodyB) == bodies.end()) {
                continue;
            }
            
            // 檢查碰撞過濾
            if ((bodyA->GetCollisionGroup() & bodyB->GetCollisionMask()) == 0) {
                continue;
            }
            
            // 簡化碰撞檢測：以包圍球判定
            Vector3 diff = bodyA->GetPosition() - bodyB->GetPosition();
            float distance = diff.Length();
            float minDistance = BoundingRadius(bodyA) + BoundingRadius(bodyB);
            
            if (distance < minDistance) {
                CollisionData collision;
                collision.position = (bodyA->GetPosition() + bodyB->GetPosition()) * 0.5f;
                // 兩物體完全重疊時 diff 為零向量，Normalized 會產生 NaN
                collision.normal = (distance > 1e-6f) ? diff.Normalized() : Vector3(0.0f, 1.0f, 0.0f);
                collision.penetrationDepth = minDistance - distance;
                collision.bodyAID = bodyA->GetBodyID();
                collision.bodyBID = bodyB->GetBodyID();
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
        PhysicsBody* bodyA = GetBody(collision.bodyAID);
        PhysicsBody* bodyB = GetBody(collision.bodyBID);
        if (!bodyA || !bodyB || bodyA == bodyB) continue;
        
        // 分離物體
        Vector3 separation = collision.normal * collision.penetrationDepth * 0.5f;
        
        if (bodyA->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyA->SetPosition(bodyA->GetPosition() + separation);
        }
        
        if (bodyB->GetBodyType() == PhysicsBodyType::Dynamic) {
            bodyB->SetPosition(bodyB->GetPosition() - separation);
        }
        
        // 計算衝量：彈性係數取兩材質平均
        float restitution = (bodyA->GetMaterial().restitution + bodyB->GetMaterial().restitution) * 0.5f;
        Vector3 relativeVelocity = bodyA->GetLinearVelocity() - bodyB->GetLinearVelocity();
        float velocityAlongNormal = Vector3::Dot(relativeVelocity, collision.normal);
        
        if (velocityAlongNormal > 0) {
            continue; // 物體正在分離
        }
        
        // 兩者皆為零質量（視為無限質量）時無衝量
        float massA = bodyA->GetMass();
        float massB = bodyB->GetMass();
        float invMassA = (massA > 0.0f) ? 1.0f / massA : 0.0f;
        float invMassB = (massB > 0.0f) ? 1.0f / massB : 0.0f;
        if (invMassA + invMassB <= 0.0f) {
            continue;
        }
        
        float j = -(1 + restitution) * velocityAlongNormal;
        j /= (invMassA + invMassB);
        
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
    // v += (F / m) * dt,積分完清空累積力
    for (auto body : bodies) {
        if (body->GetBodyType() != PhysicsBodyType::Dynamic) {
            continue;
        }
        float mass = body->GetMass();
        if (mass > 0.0f) {
            body->linearVelocity += (body->accumulatedForce / mass) * deltaTime;
        }
        body->accumulatedForce = Vector3::Zero();
    }
}

void PhysicsWorld::IntegratePosition(float deltaTime) {
    for (auto body : bodies) {
        // Static 不移動;Kinematic 由速度驅動(不受力)但同樣要積分位置
        if (body->GetBodyType() == PhysicsBodyType::Static) {
            continue;
        }
        Vector3 position = body->GetPosition();
        position += body->GetLinearVelocity() * deltaTime;
        body->SetPosition(position);
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