#include "InputManager.h"
#include "Logging/Logger.h"
#include <GLFW/glfw3.h>

namespace Potato {

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
    if (!windowHandle) return false;
    
    int glfwKey = static_cast<int>(key);
    return glfwGetKey(static_cast<GLFWwindow*>(windowHandle), glfwKey) == GLFW_PRESS;
}

bool GLFWInputManager::IsKeyReleased(KeyCode key) const {
    if (!windowHandle) return false;
    
    int glfwKey = static_cast<int>(key);
    return glfwGetKey(static_cast<GLFWwindow*>(windowHandle), glfwKey) == GLFW_RELEASE;
}

Vector2 GLFWInputManager::GetMousePosition() const {
    if (!windowHandle) return Vector2(0.0f, 0.0f);
    
    double x, y;
    glfwGetCursorPos(static_cast<GLFWwindow*>(windowHandle), &x, &y);
    return Vector2(static_cast<float>(x), static_cast<float>(y));
}

bool GLFWInputManager::IsMouseButtonPressed(MouseButton button) const {
    if (!windowHandle) return false;
    
    int glfwButton = static_cast<int>(button);
    return glfwGetMouseButton(static_cast<GLFWwindow*>(windowHandle), glfwButton) == GLFW_PRESS;
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
    
    // 鍵盤回調
    glfwSetKeyUserPointer(window, this);
    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetWindowUserPointer(win));
        if (input) {
            KeyEvent event;
            event.key = input->GLFWKeyToKeyCode(key);
            event.state = (action == GLFW_PRESS) ? KeyState::Pressed : 
                         (action == GLFW_RELEASE) ? KeyState::Released : KeyState::Repeat;
            event.scancode = scancode;
            event.mods = mods;
            
            for (const auto& callback : input->keyCallbacks) {
                callback(event);
            }
        }
    });
    
    // 鼠標按鈕回調
    glfwSetMouseButtonUserPointer(window, this);
    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetWindowUserPointer(win));
        if (input) {
            MouseEvent event;
            event.button = input->GLFWMouseButtonToButton(button);
            event.state = (action == GLFW_PRESS) ? MouseState::Pressed : MouseState::Released;
            event.x = input->mousePosition.x;
            event.y = input->mousePosition.y;
            event.deltaX = input->mouseDelta.x;
            event.deltaY = input->mouseDelta.y;
            
            for (const auto& callback : input->mouseCallbacks) {
                callback(event);
            }
        }
    });
    
    // 鼠標移動回調
    glfwSetCursorPosUserPointer(window, this);
    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetWindowUserPointer(win));
        if (input) {
            float newX = static_cast<float>(xpos);
            float newY = static_cast<float>(ypos);
            
            input->mouseDelta.x = newX - input->mousePosition.x;
            input->mouseDelta.y = newY - input->mousePosition.y;
            input->mousePosition.x = newX;
            input->mousePosition.y = newY;
            
            MouseMoveEvent event;
            event.x = newX;
            event.y = newY;
            event.deltaX = input->mouseDelta.x;
            event.deltaY = input->mouseDelta.y;
            
            for (const auto& callback : input->mouseMoveCallbacks) {
                callback(event);
            }
        }
    });
    
    // 鼠標滾輪回調
    glfwSetScrollUserPointer(window, this);
    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetWindowUserPointer(win));
        if (input) {
            input->mouseScrollOffset = static_cast<float>(yoffset);
            
            MouseScrollEvent event;
            event.xOffset = static_cast<float>(xoffset);
            event.yOffset = static_cast<float>(yoffset);
            
            for (const auto& callback : input->mouseScrollCallbacks) {
                callback(event);
            }
        }
    });
    
    // 手柄連接回調
    glfwSetJoystickUserPointer(this);
    glfwSetJoystickCallback([](int jid, int event) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetJoystickUserPointer());
        if (input) {
            GamepadConnectionEvent connectionEvent;
            connectionEvent.gamepadID = jid;
            connectionEvent.connected = (event == GLFW_CONNECTED);
            
            for (const auto& callback : input->gamepadConnectionCallbacks) {
                callback(connectionEvent);
            }
        }
    });
    
    // 窗口大小回調
    glfwSetWindowSizeUserPointer(window, this);
    glfwSetWindowSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        GLFWInputManager* input = static_cast<GLFWInputManager*>(glfwGetWindowUserPointer(win));
        if (input) {
            WindowSizeEvent event;
            event.width = width;
            event.height = height;
            
            for (const auto& callback : input->windowSizeCallbacks) {
                callback(event);
            }
        }
    });
}

void GLFWInputManager::ProcessInput() {
    // 處理持續按鍵（在 Update 中處理）
}

KeyCode GLFWInputManager::GLFWKeyToKeyCode(int glfwKey) {
    // 簡化映射
    switch (glfwKey) {
        case GLFW_KEY_SPACE: return KeyCode::Space;
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
        case GLFW_KEY_ESCAPE: return KeyCode::Unknown;
        case GLFW_KEY_ENTER: return KeyCode::Unknown;
        case GLFW_KEY_TAB: return KeyCode::Unknown;
        case GLFW_KEY_BACKSPACE: return KeyCode::Unknown;
        case GLFW_KEY_INSERT: return KeyCode::Unknown;
        case GLFW_KEY_DELETE: return KeyCode::Unknown;
        case GLFW_KEY_RIGHT: return KeyCode::Unknown;
        case GLFW_KEY_LEFT: return KeyCode::Unknown;
        case GLFW_KEY_DOWN: return KeyCode::Unknown;
        case GLFW_KEY_UP: return KeyCode::Unknown;
        case GLFW_KEY_PAGE_UP: return KeyCode::Unknown;
        case GLFW_KEY_PAGE_DOWN: return KeyCode::Unknown;
        case GLFW_KEY_HOME: return KeyCode::Unknown;
        case GLFW_KEY_END: return KeyCode::Unknown;
        case GLFW_KEY_CAPS_LOCK: return KeyCode::CapsLock;
        case GLFW_KEY_SCROLL_LOCK: return KeyCode::ScrollLock;
        case GLFW_KEY_NUM_LOCK: return KeyCode::NumLock;
        case GLFW_KEY_PRINT_SCREEN: return KeyCode::Unknown;
        case GLFW_KEY_PAUSE: return KeyCode::Unknown;
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