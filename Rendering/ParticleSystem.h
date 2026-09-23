#pragma once

/**
 * ParticleSystem - CPU 粒子模擬
 *
 * 範圍誠實標註：本類只做模擬（發射/積分/壽命/流場），不含 GL 繪製——
 * 渲染端由呼叫端拿 Particles() 自行對接（SpriteAtlas/點精靈/instancing），
 * 讓模擬邏輯可 headless 測試。
 *
 * 環境流場消費點：SetFlowField 直接接 TurbulenceField/GustField 的
 * Sample(position, time)，煙塵/飄雪/魔法粒子即吃間歇陣風。
 */

#include "MathUtils/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace Potato {

struct Particle {
    Vector3 position;
    Vector3 velocity;
    float age = 0.0f;
    float lifetime = 1.0f;
    float size = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    bool Alive() const { return age < lifetime; }
};

struct EmitterConfig {
    Vector3 position;
    Vector3 direction{0.0f, 1.0f, 0.0f};  // 主噴射方向（自動正規化）
    float coneAngleDeg = 15.0f;           // 圓錐半角
    float speedMin = 1.0f, speedMax = 3.0f;
    float lifeMin = 0.5f, lifeMax = 1.5f;
    float sizeMin = 0.5f, sizeMax = 1.0f;
    float rate = 50.0f;                   // 連續發射：每秒顆數
    Vector3 gravity{0.0f, -9.8f, 0.0f};
    float drag = 0.0f;                    // 線性阻力係數（v *= 1-drag*dt）
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    bool fadeOut = true;                  // alpha 隨壽命線性淡出
};

class ParticleSystem {
public:
    explicit ParticleSystem(size_t capacity = 4096);

    void SetEmitter(const EmitterConfig& cfg) { emitter = cfg; }
    const EmitterConfig& GetEmitter() const { return emitter; }

    // 連續發射開關（rate 每秒累積，Update 時補發整數額）
    void Start() { emitting = true; }
    void Stop() { emitting = false; }
    bool IsEmitting() const { return emitting; }

    // 單發爆發（超過容量截斷）
    void Burst(int count);

    // 環境流場：回呼 (pos, simTime) → 加速度向量。
    // TurbulenceField::Sample / GustField::Sample 可直接包進來。
    void SetFlowField(
        std::function<Vector3(const Vector3&, float)> field);
    void ClearFlowField() { flowField = nullptr; }

    void Update(float dt);
    void Clear();

    const std::vector<Particle>& Particles() const { return particles; }
    size_t AliveCount() const { return particles.size(); }
    size_t Capacity() const { return capacity; }
    float SimulationTime() const { return simTime; }

    // 決定性重播/測試用
    void SetSeed(uint32_t seed) { rng.seed(seed); }

private:
    void SpawnOne();

    std::vector<Particle> particles;
    size_t capacity;
    EmitterConfig emitter;
    std::function<Vector3(const Vector3&, float)> flowField;
    std::mt19937 rng{0x9E3779B9u};
    float emitAccumulator = 0.0f;
    float simTime = 0.0f;
    bool emitting = false;
};

} // namespace Potato
