// InputTest - GLFWInputManager 無頭（headless）測試
// 驗證 OnKeyEvent 分發、雙向 keymap 一致性、無窗口狀態查詢

#include "Input/InputManager.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <vector>
#include <utility>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== Input Manager Tests ===\n\n");

    GLFWInputManager input; // 不設窗口：OnKeyEvent + keyStates 路徑可無頭運作

    // ---- OnKeyEvent 分發 ----
    KeyCode gotKey = KeyCode::Unknown;
    KeyState gotState = KeyState::Released;
    input.RegisterKeyCallback([&](const KeyEvent& e) {
        gotKey = e.key;
        gotState = e.state;
    });
    input.OnKeyEvent(GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    Check(gotKey == KeyCode::Escape && gotState == KeyState::Pressed,
          "OnKeyEvent(Escape) 分發 KeyCode::Escape + Pressed");

    // ---- 無窗口狀態查詢（keyStates 整合）----
    Check(input.IsKeyPressed(KeyCode::Escape), "Escape 按下後 IsKeyPressed=true（無窗口）");
    input.OnKeyEvent(GLFW_KEY_ESCAPE, 0, GLFW_RELEASE, 0);
    Check(input.IsKeyReleased(KeyCode::Escape), "Escape 放開後 IsKeyReleased=true");

    // ---- 雙向 keymap 一致性 ----
    // OnKeyEvent 走 GLFW→KeyCode,IsKeyPressed 走 KeyCode→GLFW;
    // 兩表不一致時 press 後 IsKeyPressed 會是 false
    struct Pair { int glfw; KeyCode code; const char* name; };
    const Pair pairs[] = {
        {GLFW_KEY_APOSTROPHE,   KeyCode::Apostrophe,   "Apostrophe"},
        {GLFW_KEY_COMMA,        KeyCode::Comma,        "Comma"},
        {GLFW_KEY_MINUS,        KeyCode::Minus,        "Minus"},
        {GLFW_KEY_PERIOD,       KeyCode::Period,       "Period"},
        {GLFW_KEY_SLASH,        KeyCode::Slash,        "Slash"},
        {GLFW_KEY_SEMICOLON,    KeyCode::Semicolon,    "Semicolon"},
        {GLFW_KEY_EQUAL,        KeyCode::Equal,        "Equal"},
        {GLFW_KEY_LEFT_BRACKET, KeyCode::LeftBracket,  "LeftBracket"},
        {GLFW_KEY_BACKSLASH,    KeyCode::Backslash,    "Backslash"},
        {GLFW_KEY_RIGHT_BRACKET,KeyCode::RightBracket, "RightBracket"},
        {GLFW_KEY_GRAVE_ACCENT, KeyCode::GraveAccent,  "GraveAccent"},
        {GLFW_KEY_WORLD_1,      KeyCode::World1,       "World1"},
        {GLFW_KEY_WORLD_2,      KeyCode::World2,       "World2"},
        {GLFW_KEY_F13,          KeyCode::F13,          "F13"},
        {GLFW_KEY_F20,          KeyCode::F20,          "F20"},
        {GLFW_KEY_F25,          KeyCode::F25,          "F25"},
        {GLFW_KEY_KP_0,         KeyCode::NumPad0,      "NumPad0"},
        {GLFW_KEY_KP_5,         KeyCode::NumPad5,      "NumPad5"},
        {GLFW_KEY_KP_9,         KeyCode::NumPad9,      "NumPad9"},
        {GLFW_KEY_KP_DECIMAL,   KeyCode::NumPadDecimal,"NumPadDecimal"},
        {GLFW_KEY_KP_DIVIDE,    KeyCode::NumPadDivide, "NumPadDivide"},
        {GLFW_KEY_KP_MULTIPLY,  KeyCode::NumPadMultiply,"NumPadMultiply"},
        {GLFW_KEY_KP_SUBTRACT,  KeyCode::NumPadSubtract,"NumPadSubtract"},
        {GLFW_KEY_KP_ADD,       KeyCode::NumPadAdd,    "NumPadAdd"},
        {GLFW_KEY_KP_ENTER,     KeyCode::NumPadEnter,  "NumPadEnter"},
        {GLFW_KEY_KP_EQUAL,     KeyCode::NumPadEqual,  "NumPadEqual"},
    };
    int mapFail = 0;
    for (const Pair& p : pairs) {
        gotKey = KeyCode::Unknown;
        input.OnKeyEvent(p.glfw, 0, GLFW_PRESS, 0);
        if (gotKey != p.code || !input.IsKeyPressed(p.code)) {
            printf("  [FAIL] keymap %s (event=%d, pressed=%d)\n",
                   p.name, static_cast<int>(gotKey), input.IsKeyPressed(p.code));
            ++mapFail;
        }
        input.OnKeyEvent(p.glfw, 0, GLFW_RELEASE, 0);
    }
    Check(mapFail == 0, "標點/World/F13-25/NumPad 雙向映射一致（26 組）");

    // ---- 未知鍵安全處理 ----
    gotKey = KeyCode::Escape; // sentinel
    input.OnKeyEvent(-1, 0, GLFW_PRESS, 0);
    Check(gotKey == KeyCode::Unknown, "未知 GLFW 鍵 (-1) → KeyCode::Unknown 不 crash");

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
