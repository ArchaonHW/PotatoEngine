#include "Rendering/ParticleSystem.h"

#include <algorithm>
#include <cmath>

namespace Potato {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// 在 direction 周圍的圓錐內均勻取樣方向
Vector3 SampleCone(const Vector3& dir, float halfAngleDeg,
                   std::mt19937& rng) {
    const Vector3 d = dir.Normalized();
    if (halfAngleDeg <= 0.0f) return d;

    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const float cosMax = std::cos(halfAngleDeg * kDegToRad);
    const float cosTheta = 1.0f - u(rng) * (1.0f - cosMax);
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = u(rng) * 2.0f * 3.14159265358979323846f;

    // 建正交基
    Vector3 up = (std::fabs(d.y) < 0.99f) ? Vector3::Up() : Vector3::Right();
    Vector3 t = up.Cross(d).Normalized();
    Vector3 b = d.Cross(t);
    return (t * (std::cos(phi) * sinTheta) +
            b * (std::sin(phi) * sinTheta) +
            d * cosTheta).Normalized();
}

} // anonymous namespace

ParticleSystem::ParticleSystem(size_t cap) : capacity(cap) {
    particles.reserve(cap);
}

void ParticleSystem::Burst(int count) {
    const int room = static_cast<int>(capacity - particles.size());
    const int n = std::min(count, room);
    for (int i = 0; i < n; ++i) SpawnOne();
}

void ParticleSystem::SetFlowField(
    std::function<Vector3(const Vector3&, float)> field) {
    flowField = std::move(field);
}

void ParticleSystem::Update(float dt) {
    if (dt <= 0.0f) return;
    simTime += dt;

    // 連續發射：累積小數額，整數部分補發
    if (emitting && emitter.rate > 0.0f) {
        emitAccumulator += emitter.rate * dt;
        const int n = static_cast<int>(emitAccumulator);
        if (n > 0) {
            emitAccumulator -= static_cast<float>(n);
            Burst(n);
        }
    }

    for (auto& p : particles) {
        p.age += dt;
        if (!p.Alive()) continue;

        Vector3 accel = emitter.gravity;
        if (flowField) {
            accel += flowField(p.position, simTime);
        }
        p.velocity += accel * dt;
        if (emitter.drag > 0.0f) {
            p.velocity *= std::max(0.0f, 1.0f - emitter.drag * dt);
        }
        p.position += p.velocity * dt;
        if (emitter.fadeOut) {
            p.a = emitter.a * (1.0f - p.age / p.lifetime);
        }
    }

    // 移除死亡粒子（swap-erase 保緊湊）
    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                       [](const Particle& p) { return !p.Alive(); }),
        particles.end());
}

void ParticleSystem::Clear() {
    particles.clear();
    emitAccumulator = 0.0f;
    simTime = 0.0f;
}

void ParticleSystem::SpawnOne() {
    if (particles.size() >= capacity) return;

    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto lerp = [&](float lo, float hi) {
        return lo + (hi - lo) * u(rng);
    };

    Particle p;
    p.position = emitter.position;
    p.velocity = SampleCone(emitter.direction, emitter.coneAngleDeg, rng) *
                 lerp(emitter.speedMin, emitter.speedMax);
    p.lifetime = std::max(1e-4f, lerp(emitter.lifeMin, emitter.lifeMax));
    p.size = lerp(emitter.sizeMin, emitter.sizeMax);
    p.r = emitter.r; p.g = emitter.g; p.b = emitter.b; p.a = emitter.a;
    particles.push_back(p);
}

} // namespace Potato
