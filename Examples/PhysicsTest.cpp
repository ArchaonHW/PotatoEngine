// PhysicsTest - 驗證物理管線積分語意
// 覆蓋:重力速率(dt 正確)、N 物體不重複積分、碰撞分離、kinematic 移動

#include "Physics/PhysicsSystem.h"
#include "MathUtils/Vector3.h"

#include <cstdio>
#include <cmath>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, float value = 0.0f, float expected = 0.0f) {
    if (ok) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s  (got %.4f, expected %.4f)\n", name, value, expected);
    }
}

int main() {
    printf("=== Physics Integration Tests ===\n\n");

    // [1] 重力速率:1 秒後 v ≈ -9.81,y 位移 ≈ -4.9
    // 舊 bug:ApplyForce 直接 v += F/m(無 dt)且位置被積分 N 次
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("gravity_test");
        world->Initialize();

        PhysicsBody* body = world->CreateBody();
        body->SetPosition(Vector3(0.0f, 100.0f, 0.0f));
        body->SetGravityEnabled(true);

        // 步進 1 秒(60 個 fixed step)
        for (int i = 0; i < 60; i++) {
            world->Step(1.0f / 60.0f);
        }

        float vy = body->GetLinearVelocity().y;
        float y = body->GetPosition().y;
        Check(std::fabs(vy + 9.81f) < 0.5f, "重力 1s 後速度 ≈ -9.81", vy, -9.81f);
        Check(std::fabs(y - 95.1f) < 1.0f, "重力 1s 後 y ≈ 95.1", y, 95.1f);
    }

    // [2] 多物體各自只積分一次(舊 bug:N 物體時每物體走 N 倍)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("multi_body");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* first = nullptr;
        for (int i = 0; i < 5; i++) {
            PhysicsBody* b = world->CreateBody();
            b->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
            b->SetLinearVelocity(Vector3(1.0f, 0.0f, 0.0f));
            b->SetGravityEnabled(false);
            b->SetCollisionMask(0); // 排除碰撞:本測試只驗證積分次數
            if (!first) first = b;
        }

        world->Step(1.0f / 60.0f);

        // 每個物體應恰好移動 1/60,舊 bug 會移動 5/60
        float x = first ? first->GetPosition().x : -1.0f;
        Check(std::fabs(x - 1.0f / 60.0f) < 1e-4f, "5 物體各只積分 1 次", x, 1.0f / 60.0f);
    }

    // [3] Kinematic 物體由速度驅動(舊 bug:只積分 Dynamic,kinematic 不動)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("kinematic_test");
        world->Initialize();

        PhysicsBody* b = world->CreateBody();
        b->SetBodyType(PhysicsBodyType::Kinematic);
        b->SetPosition(Vector3::Zero());
        b->SetLinearVelocity(Vector3(0.0f, 2.0f, 0.0f));

        world->Step(1.0f / 60.0f);
        float y = b->GetPosition().y;
        Check(std::fabs(y - 2.0f / 60.0f) < 1e-4f, "Kinematic 依速度移動", y, 2.0f / 60.0f);
    }

    // [4] 碰撞分離:兩個重疊球體應被推開
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("collision_test");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Sphere);
        a->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        a->SetPosition(Vector3(0.0f, 0.0f, 0.0f));
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        b->SetPosition(Vector3(0.6f, 0.0f, 0.0f)); // 重疊 0.4
        b->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);

        float dist = (a->GetPosition() - b->GetPosition()).Length();
        Check(dist > 0.6f, "重疊球體被分離", dist, 1.0f);
    }

    // [5] ApplyForce 是連續力:F=60, m=1, 一步 dt=1/60 -> Δv = 1.0
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("force_test");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* b = world->CreateBody();
        b->SetPosition(Vector3::Zero());
        b->SetGravityEnabled(false);
        b->ApplyForce(Vector3(60.0f, 0.0f, 0.0f));

        world->Step(1.0f / 60.0f);
        float vx = b->GetLinearVelocity().x;
        Check(std::fabs(vx - 1.0f) < 1e-4f, "ApplyForce 乘 dt 積分", vx, 1.0f);
    }

    // [6] OBB SAT:旋轉 45° 的盒子角對角可與另一盒碰撞(包圍球會誤判)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("obb_test");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Box);
        a->SetCollisionShapeDimensions(Vector3(2.0f, 2.0f, 2.0f));
        a->SetPosition(Vector3::Zero());
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Box);
        b->SetCollisionShapeDimensions(Vector3(2.0f, 2.0f, 2.0f));
        // 繞 Z 轉 45°,頂點朝 -X 伸向 a
        b->SetRotation(Quaternion::FromAxisAngle(Vector3(0, 0, 1), 3.14159265f / 4.0f));
        // 45° 旋轉後半徑(頂點)= sqrt(2)≈1.414;放在 x=2.3 → 頂點伸到 x≈0.886,穿入 a(半徑1)
        b->SetPosition(Vector3(2.3f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);
        Check(world->GetCollisionCount() > 0, "旋轉 OBB 角碰撞被 SAT 偵測");
    }

    // [7] OBB SAT:旋轉盒平行移開後不碰撞(排除假陽性)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("obb_miss");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Box);
        a->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        a->SetPosition(Vector3::Zero());
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Box);
        b->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        b->SetRotation(Quaternion::FromAxisAngle(Vector3(0, 0, 1), 0.6f));
        b->SetPosition(Vector3(3.0f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);
        Check(world->GetCollisionCount() == 0, "分離的旋轉 OBB 不誤報");
    }

    // [8] Broadphase:大量稀疏物體只產生鄰近碰撞
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("broadphase_test");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        // 10x10 稀疏網格(間距 10,半徑 0.5 → 無碰撞)
        for (int x = 0; x < 10; x++) {
            for (int z = 0; z < 10; z++) {
                PhysicsBody* b = world->CreateBody();
                b->SetCollisionShape(CollisionShape::Sphere);
                b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
                b->SetPosition(Vector3(x * 10.0f, 0.0f, z * 10.0f));
                b->SetGravityEnabled(false);
            }
        }
        // 再放一對重疊球體
        PhysicsBody* c1 = world->CreateBody();
        c1->SetCollisionShape(CollisionShape::Sphere);
        c1->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        c1->SetPosition(Vector3(500.0f, 0.0f, 0.0f));
        c1->SetGravityEnabled(false);
        PhysicsBody* c2 = world->CreateBody();
        c2->SetCollisionShape(CollisionShape::Sphere);
        c2->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        c2->SetPosition(Vector3(500.8f, 0.0f, 0.0f));
        c2->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);
        Check(world->GetCollisionCount() == 1, "102 物體 spatial hash 只偵測到 1 對真碰撞");
    }

    // [9] Raycast 球體:命中表面點(修正:Sphere dims.x 就是半徑,舊版誤乘 0.5)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("ray_sphere");
        world->Initialize();

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f)); // 半徑 1
        b->SetPosition(Vector3(5.0f, 0.0f, 0.0f));

        CollisionData hit;
        Check(world->Raycast(Vector3::Zero(), Vector3(10.0f, 0.0f, 0.0f), hit),
              "射線命中球體");
        Check(std::fabs(hit.position.x - 4.0f) < 0.05f,
              "命中點在球表面 x≈4", hit.position.x, 4.0f);
        Check(hit.normal.x < -0.9f, "命中法線朝 -X(頂著射線)", hit.normal.x, -1.0f);
        Check(hit.otherBodyID == b->GetBodyID(), "命中回傳正確 bodyID");
    }

    // [10] Raycast 未命中:射線從球上方掠過
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("ray_miss");
        world->Initialize();

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        b->SetPosition(Vector3(5.0f, 0.0f, 0.0f));

        CollisionData hit;
        Check(!world->Raycast(Vector3(0.0f, 3.0f, 0.0f), Vector3(10.0f, 3.0f, 0.0f), hit),
              "掠過射線未命中");
    }

    // [11] Raycast 最近命中:遠近兩球,應命中近者
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("ray_nearest");
        world->Initialize();

        PhysicsBody* near_ = world->CreateBody();
        near_->SetCollisionShape(CollisionShape::Sphere);
        near_->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        near_->SetPosition(Vector3(3.0f, 0.0f, 0.0f));

        PhysicsBody* far = world->CreateBody();
        far->SetCollisionShape(CollisionShape::Sphere);
        far->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        far->SetPosition(Vector3(8.0f, 0.0f, 0.0f));

        CollisionData hit;
        Check(world->Raycast(Vector3::Zero(), Vector3(10.0f, 0.0f, 0.0f), hit)
              && hit.otherBodyID == near_->GetBodyID(),
              "命中最近的球體");
    }

    // [12] Raycast 旋轉 OBB:45° 盒子的斜邊被精確命中(包圍球會誤判提早命中)
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("ray_obb");
        world->Initialize();

        PhysicsBody* box = world->CreateBody();
        box->SetCollisionShape(CollisionShape::Box);
        box->SetCollisionShapeDimensions(Vector3(2.0f, 2.0f, 2.0f));
        box->SetPosition(Vector3::Zero());
        box->SetRotation(Quaternion::FromAxisAngle(Vector3(0, 0, 1), 3.14159265f / 4.0f));

        // 射線高 y=0.8 水平 +X:45° 旋轉盒在 y=0.8 的橫切面是 x∈[-0.614,0.614]
        // (|0.707x+0.566|≤1 ∩ |0.566-0.707x|≤1 → 進入點 x≈-0.614)
        // 若誤用包圍球(半徑√2)會在 x≈-1.166 提早命中
        CollisionData hit;
        Check(world->Raycast(Vector3(-5.0f, 0.8f, 0.0f), Vector3(5.0f, 0.8f, 0.0f), hit),
              "射線命中旋轉 OBB");
        Check(std::fabs(hit.position.x + 0.6142f) < 0.02f,
              "OBB 命中點在斜邊上 x≈-0.614", hit.position.x, -0.6142f);
        Check(hit.normal.Dot(Vector3(1.0f, 0.0f, 0.0f)) < 0.0f,
              "OBB 命中法線頂著射線方向");
    }

    // [13] Raycast 線段端點:線段太短碰不到球 → 未命中
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("ray_segment");
        world->Initialize();

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(1.0f, 1.0f, 1.0f));
        b->SetPosition(Vector3(5.0f, 0.0f, 0.0f));

        CollisionData hit;
        Check(!world->Raycast(Vector3::Zero(), Vector3(2.0f, 0.0f, 0.0f), hit),
              "短線段未命中(端點在球前)");
    }

    // [14] 碰撞回調雙向觸發:A、B 各自的 callback 都要收到,otherBodyID 指向對方
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("cb_both");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        auto mkBall = [&](float x) {
            PhysicsBody* b = world->CreateBody();
            b->SetCollisionShape(CollisionShape::Sphere);
            b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
            b->SetPosition(Vector3(x, 0.0f, 0.0f));
            b->SetGravityEnabled(false);
            return b;
        };
        PhysicsBody* a = mkBall(0.0f);
        PhysicsBody* b = mkBall(0.6f);

        int aOther = -1, bOther = -1;
        a->SetCollisionCallback([&](const CollisionData& c) { aOther = c.otherBodyID; });
        b->SetCollisionCallback([&](const CollisionData& c) { bOther = c.otherBodyID; });

        world->Step(1.0f / 60.0f);
        Check(aOther == b->GetBodyID(), "bodyA 回調收到 otherBodyID=B", (float)aOther, (float)b->GetBodyID());
        Check(bOther == a->GetBodyID(), "bodyB 回調收到 otherBodyID=A", (float)bOther, (float)a->GetBodyID());
    }

    // [15] 回調內 DestroyBody 不 crash（pair 迴圈用 snapshot 且每步驗活）
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("cb_destroy");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Sphere);
        a->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        a->SetPosition(Vector3::Zero());
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        b->SetPosition(Vector3(0.6f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);

        a->SetCollisionCallback([world, b](const CollisionData&) {
            world->DestroyBody(b); // 在回調內銷毀對方
        });
        world->Step(1.0f / 60.0f);
        world->Step(1.0f / 60.0f); // 再一步確認無殘留野指標
        Check(true, "回調內 DestroyBody 後續 Step 不 crash");
    }

    // [16] kinematic 布林旗標與 bodyType 語義一致：Dynamic+SetKinematic(true) 不受力
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("kin_flag");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* b = world->CreateBody();
        b->SetPosition(Vector3::Zero());
        b->SetGravityEnabled(false);
        b->SetKinematic(true); // 布林旗標路徑
        b->ApplyForce(Vector3(600.0f, 0.0f, 0.0f));
        b->ApplyImpulse(Vector3(5.0f, 0.0f, 0.0f));

        world->Step(1.0f / 60.0f);
        float vx = b->GetLinearVelocity().x;
        Check(std::fabs(vx) < 1e-4f, "SetKinematic(true) 的 Dynamic 不受力/衝量", vx, 0.0f);
    }

    // [17] 零質量對：兩個 mass=0 物體重疊不產生 NaN/除零
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("zero_mass");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Sphere);
        a->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        a->SetMass(0.0f);
        a->SetPosition(Vector3::Zero());
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        b->SetMass(0.0f);
        b->SetPosition(Vector3(0.6f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);
        float v = a->GetLinearVelocity().x + b->GetLinearVelocity().x;
        Check(std::isfinite(v), "零質量對碰撞不產生 NaN 速度", v, 0.0f);
    }

    // [18] 完全重合（coincident）兩球：法線退化時不 crash
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("coincident");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        for (int i = 0; i < 2; i++) {
            PhysicsBody* b = world->CreateBody();
            b->SetCollisionShape(CollisionShape::Sphere);
            b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
            b->SetPosition(Vector3::Zero());
            b->SetGravityEnabled(false);
        }
        world->Step(1.0f / 60.0f);
        Check(true, "完全重合球體 Step 不 crash");
    }

    // [19] restitution：正向對撞,彈性 1 應近似反轉速度
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("restitution");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Sphere);
        a->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        a->SetPosition(Vector3::Zero());
        a->SetLinearVelocity(Vector3(2.0f, 0.0f, 0.0f));
        a->SetGravityEnabled(false);
        PhysicsMaterial matA; matA.restitution = 1.0f; a->SetMaterial(matA);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Sphere);
        b->SetCollisionShapeDimensions(Vector3(0.5f, 0.5f, 0.5f));
        b->SetPosition(Vector3(0.9f, 0.0f, 0.0f)); // 已重疊,下一步即解算
        b->SetLinearVelocity(Vector3(-2.0f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);
        PhysicsMaterial matB; matB.restitution = 1.0f; b->SetMaterial(matB);

        world->Step(1.0f / 60.0f);
        float va = a->GetLinearVelocity().x;
        float vb = b->GetLinearVelocity().x;
        Check(va < -1.0f && vb > 1.0f, "restitution=1 對撞後速度近似反轉", va, -2.0f);
    }

    // [20] 非球形 BoundingRadius：Box 用最大邊一半,broadphase 不漏碰撞
    {
        PhysicsManager& mgr = PhysicsManager::GetInstance();
        PhysicsWorld* world = mgr.CreateWorld("box_bounds");
        world->Initialize();
        world->SetGravity(Vector3::Zero());

        PhysicsBody* a = world->CreateBody();
        a->SetCollisionShape(CollisionShape::Box);
        a->SetCollisionShapeDimensions(Vector3(4.0f, 0.2f, 0.2f)); // 長盒,半長 2
        a->SetPosition(Vector3::Zero());
        a->SetGravityEnabled(false);

        PhysicsBody* b = world->CreateBody();
        b->SetCollisionShape(CollisionShape::Box);
        b->SetCollisionShapeDimensions(Vector3(4.0f, 0.2f, 0.2f));
        // 中心距 3.5：兩盒在 x 向重疊 [1.5,2.0]。
        // 若 broadphase 誤用最小邊(0.1)當包圍半徑,sum=0.2 < 3.5 會漏碰
        b->SetPosition(Vector3(3.5f, 0.0f, 0.0f));
        b->SetGravityEnabled(false);

        world->Step(1.0f / 60.0f);
        Check(world->GetCollisionCount() == 1, "長盒 broadphase 以最大邊計算不漏碰");
    }

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
