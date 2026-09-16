#include "GLFWSharedContext.h"
#include "PlatformSystem.h"
#include "Input/InputManager.h"
#include <GLFW/glfw3.h>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Potato {

namespace {
    std::mutex g_contextMutex;
    std::unordered_map<GLFWwindow*, std::unique_ptr<GLFWSharedContext>> g_contexts;
}

GLFWSharedContext& GetOrCreateGLFWContext(GLFWwindow* window) {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto it = g_contexts.find(window);
    if (it != g_contexts.end() && it->second->installed &&
        glfwGetWindowUserPointer(window) != it->second.get()) {
        // 此位址的舊窗口曾被安裝回調,但 user pointer 已不再指向該 context——
        // 表示窗口被 raw glfwDestroyWindow 銷毀後記憶體位址被新窗口回收。
        // 殘留的 owner 指標已懸空,必須丟棄重建,否則回調會觸發 UAF。
        g_contexts.erase(it);
        it = g_contexts.end();
    }
    if (it == g_contexts.end()) {
        it = g_contexts.emplace(window, std::make_unique<GLFWSharedContext>()).first;
    }
    return *it->second;
}

GLFWSharedContext* FindGLFWContext(GLFWwindow* window) {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    auto it = g_contexts.find(window);
    return (it != g_contexts.end()) ? it->second.get() : nullptr;
}

void ReleaseGLFWContext(GLFWwindow* window) {
    std::lock_guard<std::mutex> lock(g_contextMutex);
    g_contexts.erase(window);
}

void InstallGLFWDispatchCallbacks(GLFWwindow* window) {
    if (!window) return;
    
    // user pointer 指向共享 context（map 中的物件位址在 unordered_map 內穩定）
    GLFWSharedContext& ctx = GetOrCreateGLFWContext(window);
    glfwSetWindowUserPointer(window, &ctx);
    ctx.installed = true;
    
    // 窗口關閉
    glfwSetWindowCloseCallback(window, [](GLFWwindow* win) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (c && c->windowOwner) {
            c->windowOwner->OnCloseEvent();
        }
    });
    
    // 窗口大小（Platform 與 Input 都要收到）
    glfwSetWindowSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (!c) return;
        if (c->windowOwner) {
            c->windowOwner->OnResizeEvent(width, height);
        }
        if (c->inputOwner) {
            c->inputOwner->OnWindowSizeEvent(width, height);
        }
    });
    
    // 鍵盤
    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (c && c->inputOwner) {
            c->inputOwner->OnKeyEvent(key, scancode, action, mods);
        }
    });
    
    // 鼠標按鈕
    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (c && c->inputOwner) {
            c->inputOwner->OnMouseButtonEvent(button, action, mods);
        }
    });
    
    // 鼠標移動
    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (c && c->inputOwner) {
            c->inputOwner->OnCursorPosEvent(xpos, ypos);
        }
    });
    
    // 鼠標滾輪
    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        GLFWSharedContext* c = static_cast<GLFWSharedContext*>(glfwGetWindowUserPointer(win));
        if (c && c->inputOwner) {
            c->inputOwner->OnScrollEvent(xoffset, yoffset);
        }
    });
}

void DestroyGLFWWindow(GLFWwindow* window) {
    if (!window) return;
    // 先通知 input manager 窗口即將銷毀(清其 windowHandle),
    // 再清 user pointer 與共享 context,最後銷毀窗口
    if (GLFWSharedContext* ctx = FindGLFWContext(window)) {
        if (ctx->inputOwner) {
            ctx->inputOwner->OnWindowDestroyed();
        }
    }
    glfwSetWindowUserPointer(window, nullptr);
    ReleaseGLFWContext(window);
    glfwDestroyWindow(window);
}

} // namespace Potato
