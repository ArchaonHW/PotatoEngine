#pragma once

#include <string>
#include <memory>

namespace Potato {

/**
 * 渲染系統接口
 * 定義所有渲染器必須實現的基本功能
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;
    
    // 初始化和關閉
    virtual bool Initialize(int windowWidth, int windowHeight, const std::string& title) = 0;
    virtual void Shutdown() = 0;
    
    // 幀渲染
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Present() = 0;
    
    // 視口管理
    virtual void Resize(int width, int height) = 0;
    virtual void SetFullscreen(bool fullscreen) = 0;
    
    // 清除操作
    virtual void Clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f) = 0;
    
    // 性能信息
    virtual float GetFrameTime() const = 0;
    virtual int GetFPS() const = 0;
    
    // 設置
    virtual void SetVSync(bool enabled) = 0;
    virtual void SetTargetFPS(int fps) = 0;
};

} // namespace Potato