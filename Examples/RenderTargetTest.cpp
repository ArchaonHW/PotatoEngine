// RenderTargetTest - Rendering/RenderTarget 離屏 FBO 測試
//
// GL-gated：無顯示環境 [SKIP] 回 0（GLSmokeTest 慣例）。
// 驗證：Create/Bind/ReadColor/ReadDepth/resize 重建/move 語義/非法尺寸拒絕。

#include "Rendering/RenderTarget.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Potato;

static int g_pass = 0, g_fail = 0, g_skip = 0;
static void Check(bool ok, const char* name) {
    if (ok) { ++g_pass; printf("  [PASS] %s\n", name); }
    else    { ++g_fail; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== RenderTarget Test ===\n\n");

    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境\n");
        g_skip = 1;
        printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
               g_pass, g_fail, g_skip);
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "rt_test", nullptr, nullptr);
    if (!win) {
        printf("  [SKIP] glfwCreateWindow 失敗\n");
        g_skip = 1;
        glfwTerminate();
        printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
               g_pass, g_fail, g_skip);
        return 0;
    }
    glfwMakeContextCurrent(win); // glad 需要 current context,先綁再載
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        printf("  [SKIP] gladLoadGLLoader 失敗\n");
        g_skip = 1;
        glfwDestroyWindow(win);
        glfwTerminate();
        printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
               g_pass, g_fail, g_skip);
        return 0;
    }

    // ---- [1] 建立與非法尺寸 ----
    printf("[1] Create\n");
    RenderTarget rt;
    Check(!rt.IsValid(), "初始無效");
    Check(!rt.Create(0, 64) && !rt.IsValid(), "寬 0 拒絕");
    Check(!rt.Create(64, -1) && !rt.IsValid(), "負高拒絕");
    Check(rt.Create(64, 48) && rt.IsValid(), "64x48 建立");
    Check(rt.GetWidth() == 64 && rt.GetHeight() == 48, "尺寸正確");
    Check(rt.GetColorTexture() != 0 && rt.GetDepthRenderbuffer() != 0,
          "附件非零");

    // ---- [2] 渲染 + 讀回 ----
    printf("\n[2] 讀回\n");
    rt.Bind();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    std::vector<unsigned char> rgba;
    Check(rt.ReadColor(rgba), "ReadColor 回 true");
    Check(rgba.size() == 64u * 48u * 4u, "RGBA 大小 = W*H*4");
    // 中心像素應等於 clear color（±2 容差）
    const size_t ci = (24 * 64 + 32) * 4;
    Check(std::abs((int)rgba[ci] - 64) <= 2 &&
              std::abs((int)rgba[ci + 1] - 128) <= 2 &&
              std::abs((int)rgba[ci + 2] - 191) <= 2,
          "中心像素=clear color");
    std::vector<float> depth;
    Check(rt.ReadDepth(depth), "ReadDepth 回 true");
    Check(depth.size() == 64u * 48u, "深度大小 = W*H");
    Check(std::fabs(depth[24 * 64 + 32] - 1.0f) < 0.001f,
          "空場景深度≈1.0(far)");
    rt.Unbind();

    // ---- [3] resize 重建 ----
    printf("\n[3] resize\n");
    Check(rt.Create(32, 32) && rt.IsValid(), "重建 32x32");
    Check(rt.GetWidth() == 32 && rt.GetHeight() == 32, "新尺寸生效");

    // ---- [4] move 語義 ----
    printf("\n[4] move\n");
    const uint32 idBefore = rt.GetColorTexture();
    RenderTarget moved(std::move(rt));
    Check(moved.IsValid() && !rt.IsValid(), "move 轉移所有權");
    Check(moved.GetColorTexture() == idBefore, "texture id 跟著走");

    // ---- [5] 未建即讀 ----
    printf("\n[5] 邊界\n");
    RenderTarget empty;
    std::vector<unsigned char> tmp;
    Check(!empty.ReadColor(tmp), "未建 RT 讀回 false");
    Check(!empty.IsValid(), "仍無效");

    moved.Destroy();
    glfwDestroyWindow(win);
    glfwTerminate();
    printf("\n=== %d PASS, %d FAIL, %d SKIP ===\n", g_pass, g_fail,
           g_skip);
    return g_fail == 0 ? 0 : 1;
}
