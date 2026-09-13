#pragma once

#include "MathUtils/Vector3.h"
#include <string>
#include <memory>

namespace Potato {

/**
 * 混合模式
 */
enum class BlendMode {
    None,
    Alpha,
    Additive,
    Multiply
};

/**
 * 剔除模式
 */
enum class CullMode {
    None,
    Front,
    Back
};

/**
 * 深度測試函數
 */
enum class DepthFunc {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

/**
 * 包裹模式
 */
enum class WrapMode {
    Repeat,
    Clamp,
    Mirror
};

/**
 * 過濾模式
 */
enum class FilterMode {
    Nearest,
    Linear,
    Mipmap
};

/**
 * 渲染系統接口
 * 定義所有渲染器必須實現的基本功能
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 幀渲染
    virtual void Clear() = 0;
    virtual void Present() = 0;
    
    // 視口和剪裁
    virtual void SetViewport(int x, int y, int width, int height) = 0;
    virtual void SetScissor(int x, int y, int width, int height) = 0;
    
    // 渲染狀態
    virtual void SetClearColor(const Vector3& color) = 0;
    
    // 渲染設置
    virtual void EnableDepthTest(bool enable) = 0;
    virtual void EnableStencilTest(bool enable) = 0;
    virtual void EnableBlending(bool enable) = 0;
    virtual void EnableCulling(bool enable) = 0;
    
    virtual void SetBlendMode(BlendMode mode) = 0;
    virtual void SetCullMode(CullMode mode) = 0;
    virtual void SetDepthFunc(DepthFunc func) = 0;
};

} // namespace Potato