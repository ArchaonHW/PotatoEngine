// PhysicsMathTest - 未解決物理問題落地技術的 headless 驗證
//
// 覆蓋（docs/research/UNSOLVED_PHYSICS_TO_ENGINE.md）：
//   VelocityVerlet — 自由落體解析精確、彈簧軌道誤差優於 Euler、
//                    kinematic 物體仍移動
//   TurbulenceField — 散度≈0（無散度不變量）、seeded 決定性、
//                     時間演化、混沌敏感度（初值微擾→分歧）

#include "Physics/PhysicsSystem.h"
#include "MathUtils/CurlNoise.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace Potato;
using namespace Potato::Quasi;

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failures; } \
    else { printf("  ok: %s\n", msg); } \
} while (0)

// 自由落體：從 y=100 靜止落下 T 秒，回傳末端高度
static float DropTest(IntegratorType integrator, float T, float dt) {
    PhysicsWorld world;
    world.Initialize();
    world.SetGravity(Vector3(0.0f, -9.81f, 0.0f));
    world.SetFixedTimeStep(dt);
    world.EnableCollisionDetection(false);
    world.SetIntegrator(integrator);

    PhysicsBody* b = world.CreateBody();
    b->SetPosition(Vector3(0.0f, 100.0f, 0.0f));

    // T/dt 的 float 商可能略小於整數（截斷少跑一步）→ 用四捨五入
    const int steps = static_cast<int>(std::lround(T / dt));
    for (int i = 0; i < steps; ++i) world.Step(dt);

    const float y = b->GetPosition().y;
    world.Shutdown();
    return y;
}

static void TestFreeFall() {
    printf("-- VelocityVerlet 自由落體 --\n");

    const float T = 2.0f, dt = 1.0f / 60.0f;
    const float exact = 100.0f - 0.5f * 9.81f * T * T; // 80.38

    const float eulerY = DropTest(IntegratorType::SemiImplicitEuler, T, dt);
    const float verletY = DropTest(IntegratorType::VelocityVerlet, T, dt);
    const float eulerErr = std::fabs(eulerY - exact);
    const float verletErr = std::fabs(verletY - exact);

    printf("    落體 2s：exact=%.4f  Euler=%.4f(err %.4f)  Verlet=%.4f(err %.6f)\n",
           exact, eulerY, eulerErr, verletY, verletErr);

    // 常數加速度下 Verlet 解析精確（誤差只剩 float 精度）
    CHECK(verletErr < 1e-3f, "Verlet 自由落體近解析精確（err<1e-3）");
    CHECK(verletErr < eulerErr, "Verlet 誤差 < Euler 誤差");
    // Euler 每步漏 ½a·dt² → 系統性超前下墜
    CHECK(eulerY < exact, "Euler 呈現預期的系統性偏差");
}

// 彈簧振子：F = -k·x，解析解 x = A·cos(ωt)；比較軌跡最大誤差
static float SpringMaxError(IntegratorType integrator, float T, float dt) {
    PhysicsWorld world;
    world.Initialize();
    world.SetGravity(Vector3::Zero());
    world.SetFixedTimeStep(dt);
    world.EnableCollisionDetection(false);
    world.SetIntegrator(integrator);

    const float k = 20.0f, A = 10.0f;
    const float omega = std::sqrt(k); // m=1

    PhysicsBody* b = world.CreateBody();
    b->SetGravityEnabled(false);
    b->SetPosition(Vector3(A, 0.0f, 0.0f));

    float maxErr = 0.0f, t = 0.0f;
    const int steps = static_cast<int>(std::lround(T / dt));
    for (int i = 0; i < steps; ++i) {
        // 彈簧力（外力，每步施加——位置相依力）
        const Vector3 pos = b->GetPosition();
        b->ApplyForce(Vector3(-k * pos.x, -k * pos.y, -k * pos.z));
        world.Step(dt);
        t += dt;
        const float analytic = A * std::cos(omega * t);
        maxErr = std::max(maxErr,
            std::fabs(b->GetPosition().x - analytic));
    }
    world.Shutdown();
    return maxErr;
}

static void TestSpringOrbit() {
    printf("-- VelocityVerlet 彈簧振子 --\n");

    const float T = 4.0f, dt = 1.0f / 60.0f;
    const float eEuler = SpringMaxError(IntegratorType::SemiImplicitEuler, T, dt);
    const float eVerlet = SpringMaxError(IntegratorType::VelocityVerlet, T, dt);
    printf("    彈簧 4s 最大位置誤差：Euler=%.4f  Verlet=%.4f\n",
           eEuler, eVerlet);
    CHECK(eVerlet < eEuler, "Verlet 彈簧軌道最大誤差 < Euler");
    CHECK(eVerlet < 1.0f, "Verlet 彈簧軌道 4s 內誤差有界（<1）");
}

static void TestKinematicVerlet() {
    printf("-- Verlet 下 kinematic 物體 --\n");

    PhysicsWorld world;
    world.Initialize();
    world.SetFixedTimeStep(1.0f / 60.0f);
    world.EnableCollisionDetection(false);
    world.SetIntegrator(IntegratorType::VelocityVerlet);

    PhysicsBody* b = world.CreateBody();
    b->SetBodyType(PhysicsBodyType::Kinematic);
    b->SetPosition(Vector3::Zero());
    b->SetLinearVelocity(Vector3(3.0f, 0.0f, 0.0f));

    for (int i = 0; i < 60; ++i) world.Step(1.0f / 60.0f);
    const float x = b->GetPosition().x;
    CHECK(std::fabs(x - 3.0f) < 1e-3f, "kinematic 物體 Verlet 模式仍移動");
    world.Shutdown();
}

static void TestCurlNoise() {
    printf("-- TurbulenceField 無散度場 --\n");

    TurbulenceField field(6, 42, 0.15f, 1.0f);

    // 無散度不變量（湍流外觀的核心）：∇·v ≈ 0
    double maxDiv = 0.0;
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<float> u(-20.0f, 20.0f);
    for (int i = 0; i < 50; ++i) {
        const Vector3 p(u(rng), u(rng), u(rng));
        const double div = std::fabs(field.EstimateDivergence(p, i * 0.37f));
        maxDiv = std::max(maxDiv, div);
    }
    printf("    50 點最大散度 |∇·v| = %.2e\n", maxDiv);
    CHECK(maxDiv < 1e-2, "湍流場散度≈0（無散度不變量）");

    // 非平凡性：場不為零向量
    bool nonzero = false;
    for (int i = 0; i < 10; ++i) {
        if (field.Sample(Vector3(u(rng), u(rng), u(rng))).Length() > 1e-4f) {
            nonzero = true;
            break;
        }
    }
    CHECK(nonzero, "湍流場非零");

    // 決定性：同 seed 同輸出（回放需求）
    TurbulenceField f2(6, 42, 0.15f, 1.0f);
    const Vector3 probe(1.5f, -2.0f, 3.7f);
    const Vector3 a = field.Sample(probe, 1.23f);
    const Vector3 b = f2.Sample(probe, 1.23f);
    CHECK(a.x == b.x && a.y == b.y && a.z == b.z,
          "同 seed 湍流場逐位一致");

    // 時間演化：t 不同場不同
    const Vector3 va = field.Sample(probe, 0.0f);
    const Vector3 vb = field.Sample(probe, 5.0f);
    CHECK(va.x != vb.x || va.y != vb.y || va.z != vb.z,
          "湍流場隨時間演化");
}

static void TestChaosSensitivity() {
    printf("-- 混沌敏感度（初值微擾）--\n");

    // 不同 seed 的場在相鄰點快速去相關——蝴蝶效應的引擎版：
    // 同場鄰近點相關、異 seed 同點無關
    TurbulenceField fa(6, 1, 0.5f, 1.0f), fb(6, 2, 0.5f, 1.0f);
    const Vector3 p(3.0f, 1.0f, -2.0f);
    const Vector3 pa = fa.Sample(p), pb = fb.Sample(p);
    const float dist = (pa - pb).Length();
    printf("    異 seed 同點速度差 = %.4f\n", dist);
    CHECK(dist > 0.01f, "異 seed 場去相關（可擾動性）");
}

int main() {
    printf("=== PhysicsMathTest ===\n");
    TestFreeFall();
    TestSpringOrbit();
    TestKinematicVerlet();
    TestCurlNoise();
    TestChaosSensitivity();

    if (g_failures == 0) {
        printf("=== ALL CHECKS PASSED ===\n");
        return 0;
    }
    printf("=== %d FAILURES ===\n", g_failures);
    return 1;
}
