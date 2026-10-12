#pragma once

#include <cstdint>
#include <functional>

namespace Potato {

/**
 * 輸入系統接口
 * 定義輸入管理必須實現的基本功能
 */
class IInput {
public:
    virtual ~IInput() = default;
    
    // 初始化和關閉
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    // 輸入更新
    virtual void Update() = 0;
    
    // 鍵盤輸入
    virtual bool IsKeyPressed(int key) const = 0;
    virtual bool IsKeyJustPressed(int key) const = 0;
    virtual bool IsKeyJustReleased(int key) const = 0;
    
    // 鼠標輸入
    virtual bool IsMouseButtonPressed(int button) const = 0;
    virtual bool IsMouseButtonJustPressed(int button) const = 0;
    virtual bool IsMouseButtonJustReleased(int button) const = 0;
    virtual void GetMousePosition(int& x, int& y) const = 0;
    virtual void GetMouseDelta(int& deltaX, int& deltaY) const = 0;
    virtual int GetMouseWheel() const = 0;
    
    // 手柄輸入
    virtual bool IsGamepadConnected(int gamepadIndex) const = 0;
    virtual float GetGamepadAxis(int gamepadIndex, int axis) const = 0;
    virtual bool IsGamepadButtonPressed(int gamepadIndex, int button) const = 0;
    
    // 回調函數
    using KeyCallback = std::function<void(int, bool)>;
    virtual void SetKeyCallback(KeyCallback callback) = 0;
    
    using MouseButtonCallback = std::function<void(int, bool)>;
    virtual void SetMouseButtonCallback(MouseButtonCallback callback) = 0;
    
    using MouseMoveCallback = std::function<void(int, int)>;
    virtual void SetMouseMoveCallback(MouseMoveCallback callback) = 0;
};

} // namespace Potato