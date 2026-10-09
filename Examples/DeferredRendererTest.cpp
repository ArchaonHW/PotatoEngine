// DeferredRendererTest - 延遲渲染管線 facade 整合測試
// GL 段：BeginShadowPass → BeginGeometry → RenderFrame 全鏈，
//   驗證最終 LDR 有亮暗分布（陰影/光照確實走完整條 pipe）
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/DeferredRenderer.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/OpenGLRenderer.h"
#include "Platform/GLFWSharedContext.h"

#ifndef GLFW_INCLUDE_NONE // PotatoEngine PUBLIC 已定義
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
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

static Mesh MakeQuad(float cx, float cy, float z, float half) {
    Mesh m;
    std::vector<Vertex> v(6);
    const Vector3 n(0.0f, 0.0f, 1.0f);
    const float p[6][2] = {
        {cx - half, cy - half}, {cx + half, cy - half}, {cx + half, cy + half},
        {cx - half, cy - half}, {cx + half, cy + half}, {cx - half, cy + half},
    };
    for (int i = 0; i < 6; ++i) {
        v[i].position = Vector3(p[i][0], p[i][1], z);
        v[i].normal = n;
        v[i].texCoord = Vector2(0.0f, 0.0f);
    }
    m.SetVertices(v);
    return m;
}

int main() {
    printf("=== Deferred Renderer Tests ===\n\n");

    // [1] GL context 前建構——不得觸碰 GL、不得 crash
    {
        DeferredRenderer dr;
        Check(!dr.IsValid(), "context 前建構不建立 GL 資源");
        DeferredFrameParams p;
        dr.RenderFrame(p, 0); // IsValid=false → 早退不崩
        Check(true, "未建立時 RenderFrame 安全早退");
    }

    // [2] 真 GL context：隱藏窗口 + glad
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境,GL 路徑測試跳過\n");
        g_skip++;
    } else {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* win = glfwCreateWindow(64, 64, "dr", nullptr, nullptr);
        if (!win) {
            printf("  [SKIP] glfwCreateWindow 失敗——GL 路徑測試跳過\n");
            g_skip++;
        } else {
            glfwMakeContextCurrent(win);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                printf("  [SKIP] gladLoadGLLoader 失敗——GL 路徑測試跳過\n");
                g_skip++;
            } else {
                glGetError(); // 清掉初始化殘留

                DeferredRenderer dr;
                Check(dr.Create(64, 64) && dr.IsValid(),
                      "DeferredRenderer::Create 全子系統建立");

                // 場景：地板 z=-6 + 遮蔽板 z=-2.5
                Mesh floor = MakeQuad(0, 0, -6.0f, 10.0f);
                Mesh blocker = MakeQuad(0, 0, -2.5f, 0.7f);
                const Matrix4 model = Matrix4::Identity();
                const Vector3 lightDir(0.3f, -0.4f, -1.0f);

                // Pass 1a：陰影深度
                Shader depthShader;
                depthShader.LoadFromSource(ShadowMap::DepthVertexShader(),
                                           ShadowMap::DepthFragmentShader());
                const Matrix4 ls = dr.BeginShadowPass(
                    lightDir, Vector3(0, 0, -4), 8.0f);
                depthShader.Bind();
                depthShader.SetUniformMat4("lightSpaceMatrix", ls);
                depthShader.SetUniformMat4("model", model);
                floor.Draw();
                blocker.Draw();
                depthShader.Unbind();
                dr.EndShadowPass();

                // Pass 1b：幾何（view=identity，相機原點朝 -Z）
                Shader geo;
                geo.LoadFromSource(GBuffer::VertexShader(),
                                   GBuffer::FragmentShader());
                const Matrix4 view = Matrix4::Identity();
                const Matrix4 proj = Matrix4::Perspective(
                    60.0f * DEG_TO_RAD, 1.0f, 0.1f, 50.0f);
                dr.BeginGeometry();
                geo.Bind();
                geo.SetUniformMat4("model", model);
                geo.SetUniformMat4("view", view);
                geo.SetUniformMat4("projection", proj);
                geo.SetUniformVec3("albedo", Vector3(0.9f, 0.9f, 0.9f));
                geo.SetUniformInt("useTexture", 0);
                floor.Draw();
                blocker.Draw();
                geo.Unbind();
                dr.EndGeometry();

                // Pass 2-4：AO → 光照(+陰影) → bloom → ACES → LDR
                RenderTarget ldr;
                Check(ldr.Create(64, 64), "LDR 輸出目標建立");

                DeferredFrameParams p;
                p.view = view;
                p.projection = proj;
                DeferredDirLight sun;
                sun.direction = lightDir;
                sun.color = Vector3(2.0f, 1.9f, 1.7f); // HDR 亮度 >1
                sun.intensity = 1.0f;
                sun.castShadow = true;
                p.dirs.push_back(sun);
                p.bloomStrength = 0.4f; // 滿版亮區的 bloom bleed 會
                                        // 把陰影頂到 ~200，調低保留對比
                dr.RenderFrame(p, ldr.GetFBO());
                Check(glGetError() == GL_NO_ERROR,
                      "RenderFrame 全鏈無 GL 錯誤");

                // 讀回 LDR：受光地板亮、陰影/邊界暗 → 分布存在
                std::vector<unsigned char> px;
                ldr.Bind();
                Check(ldr.ReadColor(px), "LDR 讀回");
                ldr.Unbind();
                int bright = 0, dark = 0;
                unsigned char vmax = 0, vmin = 255;
                for (size_t i = 0; i + 2 < px.size(); i += 4) {
                    vmax = px[i] > vmax ? px[i] : vmax;
                    vmin = px[i] < vmin ? px[i] : vmin;
                    if (px[i] > 220) ++bright;
                    if (px[i] < 200) ++dark;
                }
                Check(bright > 0, "全鏈輸出有受光區（>220）");
                Check(dark > 0, "全鏈輸出有暗區（<200，陰影/AO）");
                Check(vmax > vmin, "輸出非單色（管線確實渲染）");
                printf("  [info] 亮 %d / 暗 %d / 範圍 %d-%d\n",
                       bright, dark, vmin, vmax);

                // 關掉 AO/陰影/bloom 的降級路徑也要能跑
                DeferredRenderer drLite;
                DeferredRendererSettings lite;
                lite.enableAO = false;
                lite.enableShadows = false;
                lite.enableBloom = false;
                Check(drLite.Create(32, 32, lite) && drLite.IsValid(),
                      "降級設定（無 AO/陰影/bloom）建立成功");
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
