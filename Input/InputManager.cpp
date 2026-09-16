#include "InputManager.h"
#include "Platform/GLFWSharedContext.h"
#include "Logging/Logger.h"
#include <GLFW/glfw3.h>

namespace Potato {

// GLFW 手柄回調是全局的（非窗口）,用靜態指針轉發到當前 InputManager
static GLFWInputManager* s_joystickInputManager = nullptr;

// ============================================================================
// GLFWInputManager 實現
// ============================================================================

GLFWInputManager::GLFWInputManager()
    : windowHandle(nullptr)
    , mousePosition(0.0f, 0.0f)
    , mouseDelta(0.0f, 0.0f)
    , mouseScrollOffset(0.0f)
    , initialized(false)
{
    // 初始化鍵盤狀態
    for (int i = 0; i < 512; i++) {
        keyStates[i] = false;
    }
    
    // 初始化鼠標按鈕狀態
    for (int i = 0; i < 8; i++) {
        mouseButtonStates[i] = false;
    }
}

GLFWInputManager::~GLFWInputManager() {
    Shutdown();
}

bool GLFWInputManager::Initialize() {
    if (initialized) {
        return true;
    }
    
    LOG_INFO("Initializing GLFW Input Manager...");
    
    if (!windowHandle) {
        LOG_ERROR("Window handle not set for Input Manager");
        return false;
    }
    
    SetupGLFWCallbacks();
    
    initialized = true;
    LOG_INFO("GLFW Input Manager initialized successfully");
    
    return true;
}

void GLFWInputManager::Shutdown() {
    if (!initialized) {
        return;
    }
    
    LOG_INFO("Shutting down GLFW Input Manager...");
    
    // 從共享 context 移除自己,避免窗口存活時回調呼叫到已銷毀的 manager
    if (windowHandle) {
        GLFWwindow* window = static_cast<GLFWwindow*>(windowHandle);
        if (GLFWSharedContext* ctx = FindGLFWContext(window)) {
            if (ctx->inputOwner == this) {
                ctx->inputOwner = nullptr;
            }
        }
    }
    if (s_joystickInputManager == this) {
        s_joystickInputManager = nullptr;
        glfwSetJoystickCallback(nullptr);
    }
    
    keyCallbacks.clear();
    mouseCallbacks.clear();
    mouseMoveCallbacks.clear();
    mouseScrollCallbacks.clear();
    gamepadCallbacks.clear();
    gamepadAxisCallbacks.clear();
    gamepadConnectionCallbacks.clear();
    windowSizeCallbacks.clear();
    
    initialized = false;
    LOG_INFO("GLFW Input Manager shutdown complete");
}

void GLFWInputManager::Update() {
    // 重置鼠標增量
    mouseDelta = Vector2(0.0f, 0.0f);
    mouseScrollOffset = 0.0f;
    
    // 更新手柄狀態
    for (int i = 0; i < GLFW_JOYSTICK_LAST; i++) {
        if (glfwJoystickPresent(i)) {
            // 觸發手柄軸回調
            int axisCount;
            const float* axes = glfwGetJoystickAxes(i, &axisCount);
            
            for (int j = 0; j < axisCount; j++) {
                GamepadAxisEvent event;
                event.gamepadID = i;
                event.axis = static_cast<GamepadAxis>(j);
                event.value = axes[j];
                
                for (const auto& callback : gamepadAxisCallbacks) {
                    callback(event);
                }
            }
        }
    }
}

bool GLFWInputManager::IsKeyPressed(KeyCode key) const {
    int glfwKey = KeyCodeToGLFWKey(key);
    if (glfwKey < 0) return false;
    // 有窗口時直接問 GLFW（權威來源）；無窗口（測試/headless）時
    // 退回 OnKeyEvent 維護的 keyStates,讓狀態查詢在無顯示環境也可用
    if (windowHandle) {
        return glfwGetKey(static_cast<GLFWwindow*>(windowHandle), glfwKey) == GLFW_PRESS;
    }
    return (glfwKey < 512) ? keyStates[glfwKey] : false;
}

bool GLFWInputManager::IsKeyReleased(KeyCode key) const {
    int glfwKey = KeyCodeToGLFWKey(key);
    if (glfwKey < 0) return false;
    if (windowHandle) {
        return glfwGetKey(static_cast<GLFWwindow*>(windowHandle), glfwKey) == GLFW_RELEASE;
    }
    return (glfwKey < 512) ? !keyStates[glfwKey] : true;
}

Vector2 GLFWInputManager::GetMousePosition() const {
    if (!windowHandle) return Vector2(0.0f, 0.0f);
    
    double x, y;
    glfwGetCursorPos(static_cast<GLFWwindow*>(windowHandle), &x, &y);
    return Vector2(static_cast<float>(x), static_cast<float>(y));
}

bool GLFWInputManager::IsMouseButtonPressed(MouseButton button) const {
    int glfwButton = static_cast<int>(button);
    if (glfwButton < 0) return false;
    // 同 IsKeyPressed：無窗口時退回 OnMouseButtonEvent 維護的狀態
    if (windowHandle) {
        return glfwGetMouseButton(static_cast<GLFWwindow*>(windowHandle), glfwButton) == GLFW_PRESS;
    }
    return (glfwButton < 8) ? mouseButtonStates[glfwButton] : false;
}

bool GLFWInputManager::IsGamepadConnected(int gamepadID) const {
    return glfwJoystickPresent(gamepadID) == GLFW_TRUE;
}

float GLFWInputManager::GetGamepadAxis(int gamepadID, GamepadAxis axis) const {
    if (!glfwJoystickPresent(gamepadID)) return 0.0f;
    
    int axisCount;
    const float* axes = glfwGetJoystickAxes(gamepadID, &axisCount);
    
    int axisIndex = static_cast<int>(axis);
    if (axisIndex < axisCount) {
        return axes[axisIndex];
    }
    
    return 0.0f;
}

bool GLFWInputManager::IsGamepadButtonPressed(int gamepadID, GamepadButton button) const {
    if (!glfwJoystickPresent(gamepadID)) return false;
    
    int buttonCount;
    const unsigned char* buttons = glfwGetJoystickButtons(gamepadID, &buttonCount);
    
    int buttonIndex = static_cast<int>(button);
    if (buttonIndex < buttonCount) {
        return buttons[buttonIndex] == GLFW_PRESS;
    }
    
    return false;
}

void GLFWInputManager::RegisterKeyCallback(KeyCallback callback) {
    keyCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterMouseCallback(MouseCallback callback) {
    mouseCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterMouseMoveCallback(MouseMoveCallback callback) {
    mouseMoveCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterMouseScrollCallback(MouseScrollCallback callback) {
    mouseScrollCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterGamepadCallback(GamepadCallback callback) {
    gamepadCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterGamepadAxisCallback(GamepadAxisCallback callback) {
    gamepadAxisCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterGamepadConnectionCallback(GamepadConnectionCallback callback) {
    gamepadConnectionCallbacks.push_back(callback);
}

void GLFWInputManager::RegisterWindowSizeCallback(WindowSizeCallback callback) {
    windowSizeCallbacks.push_back(callback);
}

void GLFWInputManager::SetWindowHandle(void* handle) {
    windowHandle = handle;
}

void GLFWInputManager::SetupGLFWCallbacks() {
    GLFWwindow* window = static_cast<GLFWwindow*>(windowHandle);
    
    // 註冊到共享 context：GLFW 每窗口只有一個 user pointer、每種事件只能設
    // 一個回調，必須與 GLFWWindow（Platform）共享，由 GLFWSharedContext 統一分發
    GetOrCreateGLFWContext(window).inputOwner = this;
    InstallGLFWDispatchCallbacks(window);
    s_joystickInputManager = this;
    
    // 手柄連接回調（全局回調,用靜態指針取得 manager）
    glfwSetJoystickCallback([](int jid, int event) {
        GLFWInputManager* input = s_joystickInputManager;
        if (input) {
            GamepadConnectionEvent connectionEvent;
            connectionEvent.gamepadID = jid;
            connectionEvent.connected = (event == GLFW_CONNECTED);
            
            for (const auto& callback : input->gamepadConnectionCallbacks) {
                callback(connectionEvent);
            }
        }
    });
}

void GLFWInputManager::OnWindowDestroyed() {
    windowHandle = nullptr;
    if (s_joystickInputManager == this) {
        s_joystickInputManager = nullptr;
    }
}

void GLFWInputManager::OnKeyEvent(int key, int scancode, int action, int mods) {
    if (key >= 0 && key < 512) {
        keyStates[key] = (action != GLFW_RELEASE);
    }
    
    KeyEvent event;
    event.key = GLFWKeyToKeyCode(key);
    event.state = (action == GLFW_PRESS) ? KeyState::Pressed :
                 (action == GLFW_RELEASE) ? KeyState::Released : KeyState::Repeat;
    event.scancode = scancode;
    event.mods = mods;
    
    for (const auto& callback : keyCallbacks) {
        callback(event);
    }
}

void GLFWInputManager::OnMouseButtonEvent(int button, int action, int mods) {
    (void)mods;
    if (button >= 0 && button < 8) {
        mouseButtonStates[button] = (action == GLFW_PRESS);
    }
    
    MouseEvent event;
    event.button = GLFWMouseButtonToButton(button);
    event.state = (action == GLFW_PRESS) ? MouseState::Pressed : MouseState::Released;
    event.x = mousePosition.x;
    event.y = mousePosition.y;
    event.deltaX = mouseDelta.x;
    event.deltaY = mouseDelta.y;
    
    for (const auto& callback : mouseCallbacks) {
        callback(event);
    }
}

void GLFWInputManager::OnCursorPosEvent(double xpos, double ypos) {
    float newX = static_cast<float>(xpos);
    float newY = static_cast<float>(ypos);
    
    mouseDelta.x = newX - mousePosition.x;
    mouseDelta.y = newY - mousePosition.y;
    mousePosition.x = newX;
    mousePosition.y = newY;
    
    MouseMoveEvent event;
    event.x = newX;
    event.y = newY;
    event.deltaX = mouseDelta.x;
    event.deltaY = mouseDelta.y;
    
    for (const auto& callback : mouseMoveCallbacks) {
        callback(event);
    }
}

void GLFWInputManager::OnScrollEvent(double xoffset, double yoffset) {
    mouseScrollOffset = static_cast<float>(yoffset);
    
    MouseScrollEvent event;
    event.xOffset = static_cast<float>(xoffset);
    event.yOffset = static_cast<float>(yoffset);
    
    for (const auto& callback : mouseScrollCallbacks) {
        callback(event);
    }
}

void GLFWInputManager::OnWindowSizeEvent(int width, int height) {
    WindowSizeEvent event;
    event.width = width;
    event.height = height;
    
    for (const auto& callback : windowSizeCallbacks) {
        callback(event);
    }
}

void GLFWInputManager::ProcessInput() {
    // 處理持續按鍵（在 Update 中處理）
}

KeyCode GLFWInputManager::GLFWKeyToKeyCode(int glfwKey) {
    switch (glfwKey) {
        case GLFW_KEY_SPACE: return KeyCode::Space;
        case GLFW_KEY_APOSTROPHE: return KeyCode::Apostrophe;
        case GLFW_KEY_COMMA: return KeyCode::Comma;
        case GLFW_KEY_MINUS: return KeyCode::Minus;
        case GLFW_KEY_PERIOD: return KeyCode::Period;
        case GLFW_KEY_SLASH: return KeyCode::Slash;
        case GLFW_KEY_A: return KeyCode::A;
        case GLFW_KEY_B: return KeyCode::B;
        case GLFW_KEY_C: return KeyCode::C;
        case GLFW_KEY_D: return KeyCode::D;
        case GLFW_KEY_E: return KeyCode::E;
        case GLFW_KEY_F: return KeyCode::F;
        case GLFW_KEY_G: return KeyCode::G;
        case GLFW_KEY_H: return KeyCode::H;
        case GLFW_KEY_I: return KeyCode::I;
        case GLFW_KEY_J: return KeyCode::J;
        case GLFW_KEY_K: return KeyCode::K;
        case GLFW_KEY_L: return KeyCode::L;
        case GLFW_KEY_M: return KeyCode::M;
        case GLFW_KEY_N: return KeyCode::N;
        case GLFW_KEY_O: return KeyCode::O;
        case GLFW_KEY_P: return KeyCode::P;
        case GLFW_KEY_Q: return KeyCode::Q;
        case GLFW_KEY_R: return KeyCode::R;
        case GLFW_KEY_S: return KeyCode::S;
        case GLFW_KEY_T: return KeyCode::T;
        case GLFW_KEY_U: return KeyCode::U;
        case GLFW_KEY_V: return KeyCode::V;
        case GLFW_KEY_W: return KeyCode::W;
        case GLFW_KEY_X: return KeyCode::X;
        case GLFW_KEY_Y: return KeyCode::Y;
        case GLFW_KEY_Z: return KeyCode::Z;
        case GLFW_KEY_0: return KeyCode::_0;
        case GLFW_KEY_1: return KeyCode::_1;
        case GLFW_KEY_2: return KeyCode::_2;
        case GLFW_KEY_3: return KeyCode::_3;
        case GLFW_KEY_4: return KeyCode::_4;
        case GLFW_KEY_5: return KeyCode::_5;
        case GLFW_KEY_6: return KeyCode::_6;
        case GLFW_KEY_7: return KeyCode::_7;
        case GLFW_KEY_8: return KeyCode::_8;
        case GLFW_KEY_9: return KeyCode::_9;
        case GLFW_KEY_SEMICOLON: return KeyCode::Semicolon;
        case GLFW_KEY_EQUAL: return KeyCode::Equal;
        case GLFW_KEY_LEFT_BRACKET: return KeyCode::LeftBracket;
        case GLFW_KEY_BACKSLASH: return KeyCode::Backslash;
        case GLFW_KEY_RIGHT_BRACKET: return KeyCode::RightBracket;
        case GLFW_KEY_GRAVE_ACCENT: return KeyCode::GraveAccent;
        case GLFW_KEY_WORLD_1: return KeyCode::World1;
        case GLFW_KEY_WORLD_2: return KeyCode::World2;
        case GLFW_KEY_ESCAPE: return KeyCode::Escape;
        case GLFW_KEY_ENTER: return KeyCode::Enter;
        case GLFW_KEY_TAB: return KeyCode::Tab;
        case GLFW_KEY_BACKSPACE: return KeyCode::Backspace;
        case GLFW_KEY_INSERT: return KeyCode::Insert;
        case GLFW_KEY_DELETE: return KeyCode::Delete;
        case GLFW_KEY_RIGHT: return KeyCode::Right;
        case GLFW_KEY_LEFT: return KeyCode::Left;
        case GLFW_KEY_DOWN: return KeyCode::Down;
        case GLFW_KEY_UP: return KeyCode::Up;
        case GLFW_KEY_PAGE_UP: return KeyCode::PageUp;
        case GLFW_KEY_PAGE_DOWN: return KeyCode::PageDown;
        case GLFW_KEY_HOME: return KeyCode::Home;
        case GLFW_KEY_END: return KeyCode::End;
        case GLFW_KEY_CAPS_LOCK: return KeyCode::CapsLock;
        case GLFW_KEY_SCROLL_LOCK: return KeyCode::ScrollLock;
        case GLFW_KEY_NUM_LOCK: return KeyCode::NumLock;
        case GLFW_KEY_PRINT_SCREEN: return KeyCode::PrintScreen;
        case GLFW_KEY_PAUSE: return KeyCode::Pause;
        case GLFW_KEY_F1: return KeyCode::F1;
        case GLFW_KEY_F2: return KeyCode::F2;
        case GLFW_KEY_F3: return KeyCode::F3;
        case GLFW_KEY_F4: return KeyCode::F4;
        case GLFW_KEY_F5: return KeyCode::F5;
        case GLFW_KEY_F6: return KeyCode::F6;
        case GLFW_KEY_F7: return KeyCode::F7;
        case GLFW_KEY_F8: return KeyCode::F8;
        case GLFW_KEY_F9: return KeyCode::F9;
        case GLFW_KEY_F10: return KeyCode::F10;
        case GLFW_KEY_F11: return KeyCode::F11;
        case GLFW_KEY_F12: return KeyCode::F12;
        case GLFW_KEY_F13: return KeyCode::F13;
        case GLFW_KEY_F14: return KeyCode::F14;
        case GLFW_KEY_F15: return KeyCode::F15;
        case GLFW_KEY_F16: return KeyCode::F16;
        case GLFW_KEY_F17: return KeyCode::F17;
        case GLFW_KEY_F18: return KeyCode::F18;
        case GLFW_KEY_F19: return KeyCode::F19;
        case GLFW_KEY_F20: return KeyCode::F20;
        case GLFW_KEY_F21: return KeyCode::F21;
        case GLFW_KEY_F22: return KeyCode::F22;
        case GLFW_KEY_F23: return KeyCode::F23;
        case GLFW_KEY_F24: return KeyCode::F24;
        case GLFW_KEY_F25: return KeyCode::F25;
        case GLFW_KEY_KP_0: return KeyCode::NumPad0;
        case GLFW_KEY_KP_1: return KeyCode::NumPad1;
        case GLFW_KEY_KP_2: return KeyCode::NumPad2;
        case GLFW_KEY_KP_3: return KeyCode::NumPad3;
        case GLFW_KEY_KP_4: return KeyCode::NumPad4;
        case GLFW_KEY_KP_5: return KeyCode::NumPad5;
        case GLFW_KEY_KP_6: return KeyCode::NumPad6;
        case GLFW_KEY_KP_7: return KeyCode::NumPad7;
        case GLFW_KEY_KP_8: return KeyCode::NumPad8;
        case GLFW_KEY_KP_9: return KeyCode::NumPad9;
        case GLFW_KEY_KP_DECIMAL: return KeyCode::NumPadDecimal;
        case GLFW_KEY_KP_DIVIDE: return KeyCode::NumPadDivide;
        case GLFW_KEY_KP_MULTIPLY: return KeyCode::NumPadMultiply;
        case GLFW_KEY_KP_SUBTRACT: return KeyCode::NumPadSubtract;
        case GLFW_KEY_KP_ADD: return KeyCode::NumPadAdd;
        case GLFW_KEY_KP_ENTER: return KeyCode::NumPadEnter;
        case GLFW_KEY_KP_EQUAL: return KeyCode::NumPadEqual;
        case GLFW_KEY_LEFT_SHIFT: return KeyCode::LeftShift;
        case GLFW_KEY_LEFT_CONTROL: return KeyCode::LeftControl;
        case GLFW_KEY_LEFT_ALT: return KeyCode::LeftAlt;
        case GLFW_KEY_LEFT_SUPER: return KeyCode::LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT: return KeyCode::RightShift;
        case GLFW_KEY_RIGHT_CONTROL: return KeyCode::RightControl;
        case GLFW_KEY_RIGHT_ALT: return KeyCode::RightAlt;
        case GLFW_KEY_RIGHT_SUPER: return KeyCode::RightSuper;
        case GLFW_KEY_MENU: return KeyCode::Menu;
        default: return KeyCode::Unknown;
    }
}

int GLFWInputManager::KeyCodeToGLFWKey(KeyCode key) const {
    // KeyCode 是序數 enum，不能直接 cast 成 GLFW 鍵碼（GLFW 使用 ASCII/自訂值）
    switch (key) {
        case KeyCode::Space: return GLFW_KEY_SPACE;
        case KeyCode::Apostrophe: return GLFW_KEY_APOSTROPHE;
        case KeyCode::Comma: return GLFW_KEY_COMMA;
        case KeyCode::Minus: return GLFW_KEY_MINUS;
        case KeyCode::Period: return GLFW_KEY_PERIOD;
        case KeyCode::Slash: return GLFW_KEY_SLASH;
        case KeyCode::_0: return GLFW_KEY_0;
        case KeyCode::_1: return GLFW_KEY_1;
        case KeyCode::_2: return GLFW_KEY_2;
        case KeyCode::_3: return GLFW_KEY_3;
        case KeyCode::_4: return GLFW_KEY_4;
        case KeyCode::_5: return GLFW_KEY_5;
        case KeyCode::_6: return GLFW_KEY_6;
        case KeyCode::_7: return GLFW_KEY_7;
        case KeyCode::_8: return GLFW_KEY_8;
        case KeyCode::_9: return GLFW_KEY_9;
        case KeyCode::Semicolon: return GLFW_KEY_SEMICOLON;
        case KeyCode::Equal: return GLFW_KEY_EQUAL;
        case KeyCode::A: return GLFW_KEY_A;
        case KeyCode::B: return GLFW_KEY_B;
        case KeyCode::C: return GLFW_KEY_C;
        case KeyCode::D: return GLFW_KEY_D;
        case KeyCode::E: return GLFW_KEY_E;
        case KeyCode::F: return GLFW_KEY_F;
        case KeyCode::G: return GLFW_KEY_G;
        case KeyCode::H: return GLFW_KEY_H;
        case KeyCode::I: return GLFW_KEY_I;
        case KeyCode::J: return GLFW_KEY_J;
        case KeyCode::K: return GLFW_KEY_K;
        case KeyCode::L: return GLFW_KEY_L;
        case KeyCode::M: return GLFW_KEY_M;
        case KeyCode::N: return GLFW_KEY_N;
        case KeyCode::O: return GLFW_KEY_O;
        case KeyCode::P: return GLFW_KEY_P;
        case KeyCode::Q: return GLFW_KEY_Q;
        case KeyCode::R: return GLFW_KEY_R;
        case KeyCode::S: return GLFW_KEY_S;
        case KeyCode::T: return GLFW_KEY_T;
        case KeyCode::U: return GLFW_KEY_U;
        case KeyCode::V: return GLFW_KEY_V;
        case KeyCode::W: return GLFW_KEY_W;
        case KeyCode::X: return GLFW_KEY_X;
        case KeyCode::Y: return GLFW_KEY_Y;
        case KeyCode::Z: return GLFW_KEY_Z;
        case KeyCode::LeftBracket: return GLFW_KEY_LEFT_BRACKET;
        case KeyCode::Backslash: return GLFW_KEY_BACKSLASH;
        case KeyCode::RightBracket: return GLFW_KEY_RIGHT_BRACKET;
        case KeyCode::GraveAccent: return GLFW_KEY_GRAVE_ACCENT;
        case KeyCode::World1: return GLFW_KEY_WORLD_1;
        case KeyCode::World2: return GLFW_KEY_WORLD_2;
        // World3-World12 無對應 GLFW 鍵碼 → 落到 default 回 -1
        case KeyCode::F1: return GLFW_KEY_F1;
        case KeyCode::F2: return GLFW_KEY_F2;
        case KeyCode::F3: return GLFW_KEY_F3;
        case KeyCode::F4: return GLFW_KEY_F4;
        case KeyCode::F5: return GLFW_KEY_F5;
        case KeyCode::F6: return GLFW_KEY_F6;
        case KeyCode::F7: return GLFW_KEY_F7;
        case KeyCode::F8: return GLFW_KEY_F8;
        case KeyCode::F9: return GLFW_KEY_F9;
        case KeyCode::F10: return GLFW_KEY_F10;
        case KeyCode::F11: return GLFW_KEY_F11;
        case KeyCode::F12: return GLFW_KEY_F12;
        case KeyCode::F13: return GLFW_KEY_F13;
        case KeyCode::F14: return GLFW_KEY_F14;
        case KeyCode::F15: return GLFW_KEY_F15;
        case KeyCode::F16: return GLFW_KEY_F16;
        case KeyCode::F17: return GLFW_KEY_F17;
        case KeyCode::F18: return GLFW_KEY_F18;
        case KeyCode::F19: return GLFW_KEY_F19;
        case KeyCode::F20: return GLFW_KEY_F20;
        case KeyCode::F21: return GLFW_KEY_F21;
        case KeyCode::F22: return GLFW_KEY_F22;
        case KeyCode::F23: return GLFW_KEY_F23;
        case KeyCode::F24: return GLFW_KEY_F24;
        case KeyCode::F25: return GLFW_KEY_F25;
        case KeyCode::NumLock: return GLFW_KEY_NUM_LOCK;
        case KeyCode::ScrollLock: return GLFW_KEY_SCROLL_LOCK;
        case KeyCode::CapsLock: return GLFW_KEY_CAPS_LOCK;
        case KeyCode::NumPad0: return GLFW_KEY_KP_0;
        case KeyCode::NumPad1: return GLFW_KEY_KP_1;
        case KeyCode::NumPad2: return GLFW_KEY_KP_2;
        case KeyCode::NumPad3: return GLFW_KEY_KP_3;
        case KeyCode::NumPad4: return GLFW_KEY_KP_4;
        case KeyCode::NumPad5: return GLFW_KEY_KP_5;
        case KeyCode::NumPad6: return GLFW_KEY_KP_6;
        case KeyCode::NumPad7: return GLFW_KEY_KP_7;
        case KeyCode::NumPad8: return GLFW_KEY_KP_8;
        case KeyCode::NumPad9: return GLFW_KEY_KP_9;
        case KeyCode::NumPadDecimal: return GLFW_KEY_KP_DECIMAL;
        case KeyCode::NumPadDivide: return GLFW_KEY_KP_DIVIDE;
        case KeyCode::NumPadMultiply: return GLFW_KEY_KP_MULTIPLY;
        case KeyCode::NumPadSubtract: return GLFW_KEY_KP_SUBTRACT;
        case KeyCode::NumPadAdd: return GLFW_KEY_KP_ADD;
        case KeyCode::NumPadEnter: return GLFW_KEY_KP_ENTER;
        case KeyCode::NumPadEqual: return GLFW_KEY_KP_EQUAL;
        case KeyCode::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case KeyCode::LeftSuper: return GLFW_KEY_LEFT_SUPER;
        case KeyCode::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case KeyCode::RightControl: return GLFW_KEY_RIGHT_CONTROL;
        case KeyCode::RightAlt: return GLFW_KEY_RIGHT_ALT;
        case KeyCode::RightSuper: return GLFW_KEY_RIGHT_SUPER;
        case KeyCode::Menu: return GLFW_KEY_MENU;
        case KeyCode::Escape: return GLFW_KEY_ESCAPE;
        case KeyCode::Enter: return GLFW_KEY_ENTER;
        case KeyCode::Tab: return GLFW_KEY_TAB;
        case KeyCode::Backspace: return GLFW_KEY_BACKSPACE;
        case KeyCode::Insert: return GLFW_KEY_INSERT;
        case KeyCode::Delete: return GLFW_KEY_DELETE;
        case KeyCode::Right: return GLFW_KEY_RIGHT;
        case KeyCode::Left: return GLFW_KEY_LEFT;
        case KeyCode::Down: return GLFW_KEY_DOWN;
        case KeyCode::Up: return GLFW_KEY_UP;
        case KeyCode::PageUp: return GLFW_KEY_PAGE_UP;
        case KeyCode::PageDown: return GLFW_KEY_PAGE_DOWN;
        case KeyCode::Home: return GLFW_KEY_HOME;
        case KeyCode::End: return GLFW_KEY_END;
        case KeyCode::PrintScreen: return GLFW_KEY_PRINT_SCREEN;
        case KeyCode::Pause: return GLFW_KEY_PAUSE;
        default: return -1;
    }
}

MouseButton GLFWInputManager::GLFWMouseButtonToButton(int glfwButton) {
    switch (glfwButton) {
        case GLFW_MOUSE_BUTTON_LEFT: return MouseButton::Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return MouseButton::Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return MouseButton::Middle;
        default: return MouseButton::Unknown;
    }
}

// ============================================================================
// InputManager 實現
// ============================================================================

InputManager::InputManager() {
}

InputManager::~InputManager() {
    if (inputManager) {
        inputManager->Shutdown();
    }
}

InputManager& InputManager::GetInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::SetImplementation(UniquePtr<IInputManager> impl) {
    inputManager = std::move(impl);
}

bool InputManager::Initialize() {
    if (!inputManager) {
        LOG_ERROR("No input manager implementation set");
        return false;
    }
    return inputManager->Initialize();
}

void InputManager::Shutdown() {
    if (inputManager) {
        inputManager->Shutdown();
    }
}

void InputManager::Update() {
    if (inputManager) {
        inputManager->Update();
    }
}

bool InputManager::IsKeyPressed(KeyCode key) {
    if (inputManager) {
        return inputManager->IsKeyPressed(key);
    }
    return false;
}

bool InputManager::IsKeyReleased(KeyCode key) {
    if (inputManager) {
        return inputManager->IsKeyReleased(key);
    }
    return false;
}

Vector2 InputManager::GetMousePosition() {
    if (inputManager) {
        return inputManager->GetMousePosition();
    }
    return Vector2(0.0f, 0.0f);
}

bool InputManager::IsMouseButtonPressed(MouseButton button) {
    if (inputManager) {
        return inputManager->IsMouseButtonPressed(button);
    }
    return false;
}

void InputManager::RegisterKeyCallback(KeyCallback callback) {
    if (inputManager) {
        inputManager->RegisterKeyCallback(callback);
    }
}

void InputManager::RegisterMouseCallback(MouseCallback callback) {
    if (inputManager) {
        inputManager->RegisterMouseCallback(callback);
    }
}

void InputManager::RegisterMouseMoveCallback(MouseMoveCallback callback) {
    if (inputManager) {
        inputManager->RegisterMouseMoveCallback(callback);
    }
}

void InputManager::RegisterMouseScrollCallback(MouseScrollCallback callback) {
    if (inputManager) {
        inputManager->RegisterMouseScrollCallback(callback);
    }
}

// ============================================================================
// 全局函數
// ============================================================================

InputManager* gInputManager = nullptr;

bool InitializeInputManager() {
    if (!gInputManager) {
        gInputManager = &InputManager::GetInstance();
    }
    return gInputManager->Initialize();
}

void ShutdownInputManager() {
    if (gInputManager) {
        gInputManager->Shutdown();
    }
}

InputManager* GetInputManager() {
    if (!gInputManager) {
        gInputManager = &InputManager::GetInstance();
    }
    return gInputManager;
}

} // namespace Potato