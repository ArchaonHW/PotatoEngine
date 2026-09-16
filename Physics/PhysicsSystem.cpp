#include "PhysicsSystem.h"
#include "CollisionDetection.h"
#include "Logging/Logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Potato {

namespace {
float BoundingRadius(const PhysicsBody* body);
OBB BodyOBB(const PhysicsBody* body);
}

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
    // Kinematic（bodyType 或布林旗標）不受力——旗標由 IsKinematic() 統一判定
    if (bodyType == PhysicsBodyType::Dynamic && !IsKinematic()) {
        accumulatedForce += force;
    }
}

void PhysicsBody::ApplyForceAtPoint(const Vector3& force, const Vector3& point) {
    // 簡化實現：應用力到質心
    ApplyForce(force);
}

void PhysicsBody::ApplyTorque(const Vector3& torque) {
    if (bodyType == PhysicsBodyType::Dynamic && !IsKinematic()) {
        angularVelocity += torque;
    }
}

void PhysicsBody::ApplyImpulse(const Vector3& impulse) {
    if (bodyType == PhysicsBodyType::Dynamic && !IsKinematic() && mass > 0.0f) {
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
    , broadphaseCellSize(4.0f)
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
    // 精確射線檢測：依形狀分派 RayVsSphere / RayVsOBB(slab 法)
    Vector3 direction = to - from;
    float distance = direction.Length();
    if (distance <= 1e-6f) {
        return false;
    }
    direction = direction / distance;
    
    PhysicsBody* closestBody = nullptr;
    float closestT = distance;
    Vector3 closestNormal(0.0f, 1.0f, 0.0f);
    
    for (auto body : bodies) {
        float t = 0.0f;
        Vector3 n;
        bool hit = false;
        
        switch (body->GetCollisionShape()) {
            case CollisionShape::Sphere:
                hit = RayVsSphere(from, direction, body->GetPosition(),
                                  BoundingRadius(body), closestT, t, n);
                break;
            case CollisionShape::Box:
                hit = RayVsOBB(from, direction, BodyOBB(body), closestT, t, n);
                break;
            default:
                // 其他形狀退回包圍球
                hit = RayVsSphere(from, direction, body->GetPosition(),
                                  BoundingRadius(body), closestT, t, n);
                break;
        }
        
        if (hit && t <= closestT) {
            closestBody = body;
            closestT = t;
            closestNormal = n;
        }
    }
    
    if (!closestBody) {
        return false;
    }
    
    result.position = from + direction * closestT;
    result.normal = closestNormal;
    result.penetrationDepth = closestT; // 命中距離
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

// 世界縮放的最大軸
float MaxScale(const PhysicsBody* body) {
    const Vector3& s = body->GetScale();
    return std::max({std::fabs(s.x), std::fabs(s.y), std::fabs(s.z)});
}

// 包圍球半徑(含世界縮放):Sphere 取 dimensions.x,其他形狀取最大邊一半
float BoundingRadius(const PhysicsBody* body) {
    const Vector3& d = body->GetCollisionShapeDimensions();
    float base;
    if (body->GetCollisionShape() == CollisionShape::Sphere) {
        base = (d.x > 0.0f) ? d.x : 0.5f;
    } else {
        float maxDim = std::max({d.x, d.y, d.z});
        base = (maxDim > 0.0f) ? maxDim * 0.5f : 0.5f;
    }
    return base * MaxScale(body);
}

// 由 PhysicsBody 建構 OBB(shapeDimensions 為全尺寸,乘 scale 後取半)
OBB BodyOBB(const PhysicsBody* body) {
    return OBB::FromTransform(
        body->GetPosition(),
        body->GetCollisionShapeDimensions() * body->GetScale() * 0.5f,
        body->GetRotation());
}

// Narrowphase 分派:依兩物體形狀選擇精確測試,其餘形狀退回包圍球
// 回傳的 normal 一律為 B → A 方向(配合 ResolveCollisions 的分離方向)
SATResult Narrowphase(const PhysicsBody* a, const PhysicsBody* b) {
    CollisionShape sa = a->GetCollisionShape();
    CollisionShape sb = b->GetCollisionShape();

    if (sa == CollisionShape::Sphere && sb == CollisionShape::Sphere) {
        return TestSphereVsSphere(a->GetPosition(), BoundingRadius(a),
                                  b->GetPosition(), BoundingRadius(b));
    }
    if (sa == CollisionShape::Box && sb == CollisionShape::Box) {
        return TestOBBvsOBB(BodyOBB(a), BodyOBB(b));
    }
    if (sa == CollisionShape::Sphere && sb == CollisionShape::Box) {
        return TestSphereVsOBB(a->GetPosition(), BoundingRadius(a), BodyOBB(b));
    }
    if (sa == CollisionShape::Box && sb == CollisionShape::Sphere) {
        SATResult r = TestSphereVsOBB(b->GetPosition(), BoundingRadius(b), BodyOBB(a));
        r.normal = -r.normal; // box→sphere 翻成 B→A
        return r;
    }
    // Capsule/Cylinder/Cone/Mesh/HeightField 等:退回包圍球近似
    return TestSphereVsSphere(a->GetPosition(), BoundingRadius(a),
                              b->GetPosition(), BoundingRadius(b));
}

} // namespace

void PhysicsWorld::DetectCollisions() {
    activeCollisions.clear();
    collisionCount = 0;
    
    // 快照：碰撞回調可能呼叫 DestroyBody/CreateBody 導致迭代器失效
    std::vector<PhysicsBody*> snapshot = bodies;
    
    // ========================================================================
    // Broadphase: Uniform Spatial Hash
    // 每個物體的包圍球 AABB 映射到網格 cell,只對共享 cell 的物體對做
    // narrowphase — 由 O(N^2) 降至近似 O(N)
    // ========================================================================
    const float invCell = 1.0f / broadphaseCellSize;
    std::unordered_map<int64_t, std::vector<int>> grid;
    grid.reserve(snapshot.size() * 2);
    
    // 3 個 21-bit cell 座標打包(允許負座標,以 2^20 偏移)
    auto packCell = [](int x, int y, int z) -> int64_t {
        return (static_cast<int64_t>(x) + (1 << 20))
             | ((static_cast<int64_t>(y) + (1 << 20)) << 21)
             | ((static_cast<int64_t>(z) + (1 << 20)) << 42);
    };
    
    for (int i = 0; i < static_cast<int>(snapshot.size()); ++i) {
        PhysicsBody* b = snapshot[i];
        float r = BoundingRadius(b);
        const Vector3& p = b->GetPosition();
        
        int x0 = static_cast<int>(std::floor((p.x - r) * invCell));
        int y0 = static_cast<int>(std::floor((p.y - r) * invCell));
        int z0 = static_cast<int>(std::floor((p.z - r) * invCell));
        int x1 = static_cast<int>(std::floor((p.x + r) * invCell));
        int y1 = static_cast<int>(std::floor((p.y + r) * invCell));
        int z1 = static_cast<int>(std::floor((p.z + r) * invCell));
        
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    grid[packCell(x, y, z)].push_back(i);
    }
    
    // 已測試物體對去重(跨 cell 會重複出現)
    std::unordered_set<int64_t> tested;
    
    auto testPair = [&](PhysicsBody* bodyA, PhysicsBody* bodyB) {
        // 雙向碰撞過濾:A 的 group 要在 B 的 mask 內,反之亦然
        if ((bodyA->GetCollisionGroup() & bodyB->GetCollisionMask()) == 0 ||
            (bodyB->GetCollisionGroup() & bodyA->GetCollisionMask()) == 0) {
            return;
        }
        
        SATResult hit = Narrowphase(bodyA, bodyB);
        if (!hit.intersects) return;
        
        CollisionData collision;
        collision.position = (bodyA->GetPosition() + bodyB->GetPosition()) * 0.5f;
        collision.normal = hit.normal;           // B → A
        collision.penetrationDepth = hit.depth;
        collision.bodyAID = bodyA->GetBodyID();
        collision.bodyBID = bodyB->GetBodyID();
        collision.otherBodyID = bodyB->GetBodyID();
        
        activeCollisions.push_back(collision);
        collisionCount++;
        
        if (bodyA->collisionCallback) {
            bodyA->collisionCallback(collision);
        }
        if (bodyB->collisionCallback) {
            // 給 B 的回調:把 other 換成 A,保持「otherBodyID = 對方」的語意
            CollisionData forB = collision;
            forB.otherBodyID = bodyA->GetBodyID();
            bodyB->collisionCallback(forB);
        }
        if (globalCollisionCallback) {
            globalCollisionCallback(collision);
        }
    };
    
    for (auto& [key, cell] : grid) {
        (void)key;
        for (size_t a = 0; a < cell.size(); ++a) {
            for (size_t b = a + 1; b < cell.size(); ++b) {
                int lo = std::min(cell[a], cell[b]);
                int hi = std::max(cell[a], cell[b]);
                int64_t pairKey = (static_cast<int64_t>(lo) << 32) | hi;
                if (!tested.insert(pairKey).second) continue;
                
                PhysicsBody* bodyA = snapshot[lo];
                PhysicsBody* bodyB = snapshot[hi];
                
                // 回調可能已銷毀物體,確認仍屬於世界
                if (std::find(bodies.begin(), bodies.end(), bodyA) == bodies.end() ||
                    std::find(bodies.begin(), bodies.end(), bodyB) == bodies.end()) {
                    continue;
                }
                
                testPair(bodyA, bodyB);
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
        
        // Kinematic（兩種旗標皆含）不受碰撞反應影響,不做分離位移
        if (bodyA->GetBodyType() == PhysicsBodyType::Dynamic && !bodyA->IsKinematic()) {
            bodyA->SetPosition(bodyA->GetPosition() + separation);
        }

        if (bodyB->GetBodyType() == PhysicsBodyType::Dynamic && !bodyB->IsKinematic()) {
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
        
        // ApplyImpulse 內部已擋 Kinematic;此處 bodyType 檢查保留作為快速路徑
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
        // Kinematic（bodyType 或布林旗標）不由力驅動;
        // 累積力照樣清空,避免 kinematic 期間堆積的力在解除後一次爆發
        if (body->GetBodyType() != PhysicsBodyType::Dynamic || body->IsKinematic()) {
            body->accumulatedForce = Vector3::Zero();
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