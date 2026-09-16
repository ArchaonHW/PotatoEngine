#pragma once

// GLFW 每個窗口只有一個 user pointer、每種事件只能註冊一個回調。
// GLFWWindow（Platform）與 GLFWInputManager（Input）都需要接收事件，
// 因此用共享 context 保存兩者指標，由 GLFWSharedContext.cpp 統一安裝
// 回調並分發給兩個子系統，避免互相覆蓋或拿到錯誤類型的指標。

struct GLFWwindow;

namespace Potato {

class GLFWWindow;
class GLFWInputManager;

struct GLFWSharedContext {
    GLFWWindow* windowOwner = nullptr;
    GLFWInputManager* inputOwner = nullptr;
};

// 取得（必要時建立）窗口的共享 context
GLFWSharedContext& GetOrCreateGLFWContext(GLFWwindow* window);

// 查找既有 context（回調內使用；不存在時回傳 nullptr）
GLFWSharedContext* FindGLFWContext(GLFWwindow* window);

// 釋放窗口的共享 context（GLFWwindow 銷毀時呼叫）
void ReleaseGLFWContext(GLFWwindow* window);

// 為窗口安裝統一的 GLFW 回調（冪等，兩個子系統各呼叫一次也安全）
void InstallGLFWDispatchCallbacks(GLFWwindow* window);

} // namespace Potato
