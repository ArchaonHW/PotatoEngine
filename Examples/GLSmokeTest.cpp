// GLSmokeTest - GL-context 依賴路徑的煙霧測試
// 覆蓋:VertexArray 惰性建立、Mesh::Draw 空/非索引/索引路徑、ShouldClose 空守衛
// 無顯示環境下跳過（PASS）——CI/headless 不應失敗

#include "Rendering/OpenGLRenderer.h"
#include "Platform/GLFWSharedContext.h"

#define GLFW_INCLUDE_NONE
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== GL Smoke Tests ===\n\n");

    // [1] GL context 建立前建構 VertexArray/Mesh —— 不得觸碰 GL、不得 crash
    {
        VertexArray va;
        Mesh mesh;
        mesh.Draw();            // 空 mesh：early-return,不碰 GL
        mesh.DrawInstanced(4);
        (void)va;
    }
    Check(true, "context 前建構/Draw 空 mesh 不 crash（惰性建立）");

    // [2] ShouldClose 空窗口守衛
    {
        OpenGLRenderer renderer;
        Check(renderer.ShouldClose(), "ShouldClose 無窗口回傳 true（不 UB）");
    }

    // [3] 真 GL context：隱藏窗口 + glad
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境,GL 路徑測試跳過\n");
        g_skip++;
    } else {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* win = glfwCreateWindow(64, 64, "gl-smoke", nullptr, nullptr);
        if (!win) {
            printf("  [SKIP] glfwCreateWindow 失敗——GL 路徑測試跳過\n");
            g_skip++;
        } else {
            glfwMakeContextCurrent(win);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                printf("  [SKIP] gladLoadGLLoader 失敗——GL 路徑測試跳過\n");
                g_skip++;
            } else {
                glGetError(); // 清掉初始化期間的殘留 error

                // 惰性建立：Bind/GetVAO 後 VAO 必須非 0
                VertexArray va;
                va.Bind();
                Check(va.GetVAO() != 0, "EnsureCreated 建立非零 VAO");
                va.Unbind();

                // 非索引繪製路徑（vertices only → glDrawArrays）
                Mesh tri;
                std::vector<Vertex> verts(3);
                verts[0].position = Vector3(0, 0, 0);
                verts[1].position = Vector3(1, 0, 0);
                verts[2].position = Vector3(0, 1, 0);
                tri.SetVertices(verts);
                tri.Draw();
                Check(glGetError() == GL_NO_ERROR, "非索引 Draw 無 GL 錯誤");

                // 索引繪製路徑
                tri.SetIndices({0, 1, 2});
                tri.Draw();
                Check(glGetError() == GL_NO_ERROR, "索引 Draw 無 GL 錯誤");

                // Instanced
                tri.DrawInstanced(8);
                Check(glGetError() == GL_NO_ERROR, "DrawInstanced 無 GL 錯誤");

                // instanceCount<=0 早退
                tri.DrawInstanced(0);
                Check(glGetError() == GL_NO_ERROR, "DrawInstanced(0) 早退無錯誤");
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
