#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector2.h"
#include <string>
#include <functional>
#include <unordered_map>

namespace Potato {

/**
 * 按鍵碼枚舉
 */
enum class KeyCode {
    Unknown = 0,
    Space, Apostrophe, Comma, Minus, Period, Slash,
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9,
    Semicolon, Equal,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket, Backslash, RightBracket, GraveAccent,
    World1, World2, World3, World4, World5, World6, World7, World8, World9, World10, World11, World12,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24, F25,
    NumLock, ScrollLock, CapsLock, NumPad0, NumPad1, NumPad2, NumPad3, NumPad4,
    NumPad5, NumPad6, NumPad7, NumPad8, NumPad9, NumPadDecimal, NumPadDivide,
    NumPadMultiply, NumPadSubtract, NumPadAdd, NumPadEnter, NumPadEqual,
    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,
    Menu,
    // 後補按鍵（追加在末尾以保持既有枚舉值不變）
    Escape, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up, PageUp, PageDown, Home, End,
    PrintScreen, Pause
};

/**
 * 鼠標按鈕枚舉
 */
enum class MouseButton {
    Unknown = 0,
    Left,
    Right,
    Middle,
    Button4,
    Button5,
    Button6,
    Button7,
    Button8
};

/**
 * 手柄按鈕枚舉
 */
enum class GamepadButton {
    Unknown = 0,
    A, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start,
    Guide,
    LeftThumb, RightThumb,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    LeftTrigger, RightTrigger
};

/**
 * 手柄軸枚舉
 */
enum class GamepadAxis {
    Unknown = 0,
    LeftX, LeftY,
    RightX, RightY,
    LeftTrigger, RightTrigger
};

/**
 * 按鍵狀態
 */
enum class KeyState {
    Released,
    Pressed,
    Repeat
};

/**
 * 鼠標狀態
 */
enum class MouseState {
    Released,
    Pressed
};

/**
 * 鍵盤事件
 */
struct KeyEvent {
    KeyCode key;
    KeyState state;
    int scancode;
    int mods;
    
    bool IsPressed() const { return state == KeyState::Pressed || state == KeyState::Repeat; }
    bool IsReleased() const { return state == KeyState::Released; }
};

/**
 * 鼠標事件
 */
struct MouseEvent {
    MouseButton button;
    MouseState state;
    float x;
    float y;
    float deltaX;
    float deltaY;
    
    bool IsPressed() const { return state == MouseState::Pressed; }
    bool IsReleased() const { return state == MouseState::Released; }
};

/**
 * 鼠標移動事件
 */
struct MouseMoveEvent {
    float x;
    float y;
    float deltaX;
    float deltaY;
};

/**
 * 鼠標滾輪事件
 */
struct MouseScrollEvent {
    float xOffset;
    float yOffset;
};

/**
 * 手柄事件
 */
struct GamepadEvent {
    int gamepadID;
    GamepadButton button;
    bool pressed;
    float value; // 對於軸
};

/**
 * 手柄軸事件
 */
struct GamepadAxisEvent {
    int gamepadID;
    GamepadAxis axis;
    float value;
};

/**
 * 手柄連接事件
 */
struct GamepadConnectionEvent {
    int gamepadID;
    bool connected;
};

/**
 * 窗口大小事件
 */
struct WindowSizeEvent {
    int width;
    int height;
};

/**
 * 輸入回調類型
 */
using KeyCallback = std::function<void(const KeyEvent&)>;
using MouseCallback = std::function<void(const MouseEvent&)>;
using MouseMoveCallback = std::function<void(const MouseMoveEvent&)>;
using MouseScrollCallback = std::function<void(const MouseScrollEvent&)>;
using GamepadCallback = std::function<void(const GamepadEvent&)>;
using GamepadAxisCallback = std::function<void(const GamepadAxisEvent&)>;
using GamepadConnectionCallback = std::function<void(const GamepadConnectionEvent&)>;
using WindowSizeCallback = std::function<void(const WindowSizeEvent&)>;

/**
 * 輸入管理器接口
 */
class IInputManager {
public:
    virtual ~IInputManager() = default;
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    
    virtual void Update() = 0;
    
    // 鍵盤輸入
    virtual bool IsKeyPressed(KeyCode key) const = 0;
    virtual bool IsKeyReleased(KeyCode key) const = 0;
    virtual Vector2 GetMousePosition() const = 0;
    virtual bool IsMouseButtonPressed(MouseButton button) const = 0;
    
    // 手柄輸入
    virtual bool IsGamepadConnected(int gamepadID) const = 0;
    virtual float GetGamepadAxis(int gamepadID, GamepadAxis axis) const = 0;
    virtual bool IsGamepadButtonPressed(int gamepadID, GamepadButton button) const = 0;
    
    // 回調註冊
    virtual void RegisterKeyCallback(KeyCallback callback) = 0;
    virtual void RegisterMouseCallback(MouseCallback callback) = 0;
    virtual void RegisterMouseMoveCallback(MouseMoveCallback callback) = 0;
    virtual void RegisterMouseScrollCallback(MouseScrollCallback callback) = 0;
    virtual void RegisterGamepadCallback(GamepadCallback callback) = 0;
    virtual void RegisterGamepadAxisCallback(GamepadAxisCallback callback) = 0;
    virtual void RegisterGamepadConnectionCallback(GamepadConnectionCallback callback) = 0;
    virtual void RegisterWindowSizeCallback(WindowSizeCallback callback) = 0;
};

/**
 * GLFW 輸入管理器實現
 */
class GLFWInputManager : public IInputManager {
public:
    GLFWInputManager();
    ~GLFWInputManager() override;
    
    bool Initialize() override;
    void Shutdown() override;
    
    void Update() override;
    
    // 鍵盤輸入
    bool IsKeyPressed(KeyCode key) const override;
    bool IsKeyReleased(KeyCode key) const override;
    Vector2 GetMousePosition() const override;
    bool IsMouseButtonPressed(MouseButton button) const override;
    
    // 手柄輸入
    bool IsGamepadConnected(int gamepadID) const override;
    float GetGamepadAxis(int gameadID, GamepadAxis axis) const override;
    bool IsGamepadButtonPressed(int gamepadID, GamepadButton button) const override;
    
    // 回調註冊
    void RegisterKeyCallback(KeyCallback callback) override;
    void RegisterMouseCallback(MouseCallback callback) override;
    void RegisterMouseMoveCallback(MouseMoveCallback callback) override;
    void RegisterMouseScrollCallback(MouseScrollCallback callback) override;
    void RegisterGamepadCallback(GamepadCallback callback) override;
    void RegisterGamepadAxisCallback(GamepadAxisCallback callback) override;
    void RegisterGamepadConnectionCallback(GamepadConnectionCallback callback) override;
    void RegisterWindowSizeCallback(WindowSizeCallback callback) override;
    
    // GLFW 窗口句柄設置
    void SetWindowHandle(void* handle);
    
    // 窗口銷毀通知（由 GLFWWindow::Shutdown 經共享 context 呼叫,防止懸垂 windowHandle）
    void OnWindowDestroyed();
    
    // GLFW 共享回調分發（由 GLFWSharedContext 的統一回調呼叫）
    void OnKeyEvent(int key, int scancode, int action, int mods);
    void OnMouseButtonEvent(int button, int action, int mods);
    void OnCursorPosEvent(double xpos, double ypos);
    void OnScrollEvent(double xoffset, double yoffset);
    void OnWindowSizeEvent(int width, int height);
    
private:
    void SetupGLFWCallbacks();
    void ProcessInput();
    
    KeyCode GLFWKeyToKeyCode(int glfwKey);
    int KeyCodeToGLFWKey(KeyCode key) const;
    MouseButton GLFWMouseButtonToButton(int glfwButton);
    
private:
    void* windowHandle;
    
    bool keyStates[512];
    bool mouseButtonStates[8];
    Vector2 mousePosition;
    Vector2 mouseDelta;
    float mouseScrollOffset;
    
    std::vector<KeyCallback> keyCallbacks;
    std::vector<MouseCallback> mouseCallbacks;
    std::vector<MouseMoveCallback> mouseMoveCallbacks;
    std::vector<MouseScrollCallback> mouseScrollCallbacks;
    std::vector<GamepadCallback> gamepadCallbacks;
    std::vector<GamepadAxisCallback> gamepadAxisCallbacks;
    std::vector<GamepadConnectionCallback> gamepadConnectionCallbacks;
    std::vector<WindowSizeCallback> windowSizeCallbacks;
    
    bool initialized;
};

/**
 * 輸入管理器（全局單例）
 */
class InputManager {
public:
    static InputManager& GetInstance();
    
    IInputManager* GetImplementation() { return inputManager.get(); }
    void SetImplementation(UniquePtr<IInputManager> impl);
    
    // 便捷方法
    bool Initialize();
    void Shutdown();
    void Update();
    
    bool IsKeyPressed(KeyCode key);
    bool IsKeyReleased(KeyCode key);
    Vector2 GetMousePosition();
    bool IsMouseButtonPressed(MouseButton button);
    
    void RegisterKeyCallback(KeyCallback callback);
    void RegisterMouseCallback(MouseCallback callback);
    void RegisterMouseMoveCallback(MouseMoveCallback callback);
    void RegisterMouseScrollCallback(MouseScrollCallback callback);
    
private:
    InputManager();
    ~InputManager();
    
    UniquePtr<IInputManager> inputManager;
};

// 全局輸入管理器
extern InputManager* gInputManager;

/**
 * 初始化全局輸入管理器
 */
bool InitializeInputManager();

/**
 * 關閉全局輸入管理器
 */
void ShutdownInputManager();

/**
 * 獲取全局輸入管理器
 */
InputManager* GetInputManager();

} // namespace Potato

// 便捷宏
#define GET_INPUT() Potato::GetInputManager()