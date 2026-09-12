#pragma once

#include <cstdint>

namespace Potato {

// 前向聲明物理相關類型
class PhysicsBody;
class PhysicsShape;

/**
 * 物理系統接口
 * 定義物理引擎必須實現的基本功能
 */
class IPhysics {
public:
    virtual ~IPhysics() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 物理模擬
    virtual void Simulate(float deltaTime) = 0;
    virtual void SetGravity(float x, float y, float z) = 0;
    
    // 物理體管理
    virtual PhysicsBody* CreateBody() = 0;
    virtual void DestroyBody(PhysicsBody* body) = 0;
    
    // 碰撞檢測
    virtual bool Raycast(float startX, float startY, float startZ,
                        float dirX, float dirY, float dirZ,
                        float maxDistance) = 0;
    
    // 性能信息
    virtual int GetBodyCount() const = 0;
    virtual float GetSimulationTime() const = 0;
};

} // namespace Potato