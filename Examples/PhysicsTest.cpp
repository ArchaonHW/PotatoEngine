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

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
