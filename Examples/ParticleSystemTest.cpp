/**
 * ParticleSystemTest - CPU 粒子模擬契約測試（headless）
 *
 * 覆蓋：
 *   [發射] Burst/連續 rate/容量上限
 *   [壽命] 到期移除、fadeOut alpha 遞減
 *   [積分] 圓錐方向、重力、drag
 *   [流場] 定值風場漂移 + TurbulenceField 實接
 *   [決定] 同 seed 序列一致、Clear 重置
 */

#include "Rendering/ParticleSystem.h"
#include "MathUtils/CurlNoise.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Potato;

namespace {

int g_pass = 0;
int g_fail = 0;

void Expect(bool cond, const char* name, const std::string& detail = "") {
    if (cond) {
        g_pass++;
        printf("  [PASS] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    } else {
        g_fail++;
        printf("  [FAIL] %s%s%s\n", name,
               detail.empty() ? "" : "  -- ", detail.c_str());
    }
}

EmitterConfig BaseEmitter() {
    EmitterConfig c;
    c.position = Vector3::Zero();
    c.direction = Vector3::Up();
    c.coneAngleDeg = 0.0f;
    c.speedMin = c.speedMax = 2.0f;
    c.lifeMin = c.lifeMax = 1.0f;
    c.gravity = Vector3::Zero();
    c.rate = 0.0f;
    return c;
}

} // anonymous namespace

int main() {
    printf("=== ParticleSystem 測試 ===\n\n");

    // ---- [發射] ----
    printf("-- 發射 --\n");
    {
        ParticleSystem ps(256);
        ps.SetEmitter(BaseEmitter());
        ps.Burst(100);
        Expect(ps.AliveCount() == 100, "Burst(100) → 100 存活");
        ps.Burst(200);
        Expect(ps.AliveCount() == 256, "容量上限截斷 256",
               std::to_string(ps.AliveCount()));
    }
    {
        ParticleSystem ps(4096);
        auto cfg = BaseEmitter();
        cfg.rate = 100.0f;
        ps.SetEmitter(cfg);
        ps.Start();
        for (int i = 0; i < 60; ++i) ps.Update(1.0f / 60.0f);  // 1 秒
        Expect(std::fabs(static_cast<float>(ps.AliveCount()) - 100.0f) <= 2.0f,
               "rate=100 跑 1s ≈ 100 顆",
               std::to_string(ps.AliveCount()));
    }

    // ---- [壽命] ----
    printf("-- 壽命 --\n");
    {
        ParticleSystem ps;
        ps.SetEmitter(BaseEmitter());
        ps.Burst(50);
        for (int i = 0; i < 70; ++i) ps.Update(1.0f / 60.0f);  // >1s > lifeMax
        Expect(ps.AliveCount() == 0, "過壽命全移除");
    }
    {
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.lifeMin = cfg.lifeMax = 2.0f;
        cfg.a = 1.0f;
        ps.SetEmitter(cfg);
        ps.Burst(1);
        ps.Update(1.0f);  // 半壽
        Expect(ps.Particles()[0].a < 0.6f && ps.Particles()[0].a > 0.4f,
               "fadeOut 半壽 alpha≈0.5",
               std::to_string(ps.Particles()[0].a));
    }

    // ---- [積分] ----
    printf("-- 積分 --\n");
    {
        ParticleSystem ps;
        ps.SetEmitter(BaseEmitter());  // cone=0, dir=up, speed=2
        ps.Burst(10);
        ps.Update(0.5f);
        const auto& p = ps.Particles()[0];
        Expect(std::fabs(p.position.y - 1.0f) < 1e-4f &&
               std::fabs(p.position.x) < 1e-4f,
               "cone=0 直向上位移 v*t=1");
    }
    {
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.gravity = Vector3(0, -10, 0);
        ps.SetEmitter(cfg);
        ps.Burst(1);
        const float vy0 = ps.Particles()[0].velocity.y;
        ps.Update(0.1f);
        Expect(ps.Particles()[0].velocity.y < vy0 - 0.9f,
               "重力拉低 vy");
    }
    {
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.drag = 2.0f;
        ps.SetEmitter(cfg);
        ps.Burst(1);
        const float v0 = ps.Particles()[0].velocity.Length();
        ps.Update(0.1f);
        Expect(ps.Particles()[0].velocity.Length() < v0 * 0.85f,
               "drag 減速");
    }
    {
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.direction = Vector3(1, 0, 0);
        cfg.coneAngleDeg = 30.0f;
        cfg.speedMin = cfg.speedMax = 1.0f;
        ps.SetEmitter(cfg);
        ps.Burst(200);
        bool allInCone = true;
        for (const auto& p : ps.Particles()) {
            const float cosA = p.velocity.Normalized().Dot(Vector3(1, 0, 0));
            if (cosA < std::cos(30.0f * 3.14159f / 180.0f) - 1e-4f)
                allInCone = false;
        }
        Expect(allInCone, "200 顆全在 30° 錐內");
    }

    // ---- [流場] ----
    printf("-- 流場 --\n");
    {
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.speedMin = cfg.speedMax = 0.0f;  // 無初速
        cfg.direction = Vector3::Zero();
        ps.SetEmitter(cfg);
        ps.SetFlowField([](const Vector3&, float) {
            return Vector3(5, 0, 0);  // 定值風 +x
        });
        ps.Burst(1);
        for (int i = 0; i < 60; ++i) ps.Update(1.0f / 60.0f);  // 1s
        Expect(ps.Particles()[0].position.x > 2.0f,
               "定值風場 +x 漂移",
               std::to_string(ps.Particles()[0].position.x));
    }
    {
        // TurbulenceField 實接消費
        ParticleSystem ps;
        auto cfg = BaseEmitter();
        cfg.speedMin = cfg.speedMax = 0.0f;
        cfg.direction = Vector3::Zero();
        cfg.lifeMin = cfg.lifeMax = 5.0f;
        ps.SetEmitter(cfg);
        Quasi::TurbulenceField turb(4, 12345u, 0.5f, 2.0f);
        ps.SetFlowField([&turb](const Vector3& p, float t) {
            return turb.Sample(p, t);
        });
        ps.Burst(32);
        for (int i = 0; i < 120; ++i) ps.Update(1.0f / 60.0f);  // 2s
        float maxDisp = 0.0f;
        bool finite = true;
        for (const auto& p : ps.Particles()) {
            maxDisp = std::max(maxDisp, p.position.Length());
            if (!std::isfinite(p.position.x + p.velocity.x)) finite = false;
        }
        Expect(maxDisp > 0.05f && finite,
               "TurbulenceField 驅動非零有限位移",
               std::to_string(maxDisp));
    }

    // ---- [決定性] ----
    printf("-- 決定性/重置 --\n");
    {
        ParticleSystem a, b;
        a.SetSeed(777); b.SetSeed(777);
        auto cfg = BaseEmitter();
        cfg.coneAngleDeg = 45.0f;
        cfg.speedMin = 1.0f; cfg.speedMax = 4.0f;
        a.SetEmitter(cfg); b.SetEmitter(cfg);
        a.Burst(20); b.Burst(20);
        bool same = a.Particles()[0].position == b.Particles()[0].position &&
                    a.Particles()[0].velocity == b.Particles()[0].velocity;
        Expect(same, "同 seed 同序列");
        a.Clear();
        Expect(a.AliveCount() == 0 && a.SimulationTime() == 0.0f,
               "Clear 重置");
    }

    printf("\n=== 結果: %d PASS / %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
