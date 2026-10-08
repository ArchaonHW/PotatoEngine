// DeferredLightingTest - 延遲光照 pass 測試
// headless 段：Pack* 打包語意（view 變換、數量夾取、castShadow 索引）
// GL 段：GBuffer 場景 → deferred 光照，驗證方向光/點光/陰影開關差異
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/DeferredShading.h"
#include "Rendering/GBuffer.h"
#include "Rendering/PostProcess.h"
#include "Rendering/ShadowMap.h"
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
    printf("=== Deferred Lighting Tests ===\n\n");

    // [1] headless：光源打包語意
    {
        const Matrix4 view = Matrix4::Identity();

        // 方向光：方向已正規化、color 折 intensity
        DeferredDirLight d;
        d.direction = Vector3(0, 0, -2); // 會被正規化成 (0,0,-1)
        d.color = Vector3(1, 0.5f, 0);
        d.intensity = 2.0f;
        const auto dp = DeferredLighting::PackDirLights({d}, view);
        Check(dp.size() == 6, "PackDirLights 1 燈 = 6 floats");
        Check(std::fabs(dp[2] + 1.0f) < 1e-5f, "方向光方向被正規化");
        Check(std::fabs(dp[4] - 1.0f) < 1e-5f,
              "color 折 intensity（0.5*2=1.0）");

        // view 旋轉：RotY(90°) 把 (1,0,0) 轉成 (0,0,-1)
        DeferredDirLight d2;
        d2.direction = Vector3(1, 0, 0);
        const auto dp2 = DeferredLighting::PackDirLights(
            {d2}, Matrix4::RotationY(HALF_PI));
        Check(std::fabs(dp2[2] + 1.0f) < 1e-4f,
              "方向光方向轉 view space（RotY90 → -Z）");

        // 數量夾取：10 燈 → 4
        const auto dp10 = DeferredLighting::PackDirLights(
            std::vector<DeferredDirLight>(10), view);
        Check(dp10.size() == 4 * 6, "方向光超過上限夾到 4");

        // 點光：位置 TransformPoint + radius 收尾
        DeferredPointLight p;
        p.position = Vector3(1, 2, -3);
        p.color = Vector3(1, 1, 1);
        p.intensity = 0.5f;
        p.radius = 7.0f;
        const auto pp = DeferredLighting::PackPointLights({p}, view);
        Check(pp.size() == 7, "PackPointLights 1 燈 = 7 floats");
        Check(std::fabs(pp[0] - 1.0f) < 1e-5f &&
              std::fabs(pp[2] + 3.0f) < 1e-5f,
              "點光位置正確打包");
        Check(std::fabs(pp[6] - 7.0f) < 1e-5f, "radius 收尾");
        const auto pp10 = DeferredLighting::PackPointLights(
            std::vector<DeferredPointLight>(10), view);
        Check(pp10.size() == 8 * 7, "點光超過上限夾到 8");

        // FirstShadowCaster
        std::vector<DeferredDirLight> dirs3(3);
        Check(DeferredLighting::FirstShadowCaster(dirs3) == -1,
              "無 castShadow → -1");
        dirs3[1].castShadow = true;
        Check(DeferredLighting::FirstShadowCaster(dirs3) == 1,
              "第二燈 castShadow → 1");
        // 超過上限位置的 castShadow 不計（index 4 以外被夾掉）
        std::vector<DeferredDirLight> dirs6(6);
        dirs6[5].castShadow = true;
        Check(DeferredLighting::FirstShadowCaster(dirs6) == -1,
              "castShadow 在截斷範圍外 → -1");
    }

    // [2] GL context 前建構——不得觸碰 GL、不得 crash
    {
        DeferredLighting dl;
        Check(!dl.IsValid(), "context 前建構不建立 GL 資源");
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
        GLFWwindow* win = glfwCreateWindow(64, 64, "dl", nullptr, nullptr);
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

                // GBuffer 場景：地板 z=-6 + 遮蔽板 z=-2.5
                GBuffer gb;
                Check(gb.Create(64, 64), "GBuffer 建立");
                Mesh floor = MakeQuad(0, 0, -6.0f, 10.0f);
                Mesh blocker = MakeQuad(0, 0, -2.5f, 0.7f);

                Shader geo;
                Check(geo.LoadFromSource(GBuffer::VertexShader(),
                                       GBuffer::FragmentShader()),
                      "GBuffer shader 編譯");
                const Matrix4 model = Matrix4::Identity();
                const Matrix4 view = Matrix4::Identity();
                const Matrix4 proj = Matrix4::Perspective(
                    60.0f * DEG_TO_RAD, 1.0f, 0.1f, 50.0f);
                gb.Bind();
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                geo.Bind();
                geo.SetUniformMat4("model", model);
                geo.SetUniformMat4("view", view);
                geo.SetUniformMat4("projection", proj);
                geo.SetUniformVec3("albedo", Vector3(0.9f, 0.9f, 0.9f));
                geo.SetUniformInt("useTexture", 0);
                floor.Draw();
                blocker.Draw();
                geo.Unbind();
                gb.Unbind();

                // 方向光的 shadow map（light dir 朝前下方）
                const Vector3 lightDir(0.3f, -0.4f, -1.0f);
                ShadowMap sm;
                Check(sm.Create(1024), "ShadowMap 建立");
                const Matrix4 lightSpace =
                    ShadowMap::ComputeLightSpaceMatrix(
                        lightDir, Vector3(0, 0, -4), 8.0f);
                Shader depthShader;
                depthShader.LoadFromSource(ShadowMap::DepthVertexShader(),
                                           ShadowMap::DepthFragmentShader());
                sm.Bind();
                glClear(GL_DEPTH_BUFFER_BIT);
                depthShader.Bind();
                depthShader.SetUniformMat4("lightSpaceMatrix", lightSpace);
                depthShader.SetUniformMat4("model", model);
                floor.Draw();
                blocker.Draw();
                depthShader.Unbind();
                sm.Unbind();
                Check(glGetError() == GL_NO_ERROR, "深度 pass 無 GL 錯誤");

                DeferredLighting dl;
                Check(dl.Create() && dl.IsValid(),
                      "DeferredLighting 建立");

                DeferredDirLight sun;
                sun.direction = lightDir;
                sun.color = Vector3(1.0f, 0.95f, 0.85f);
                sun.intensity = 1.0f;
                sun.castShadow = true;

                HDRRenderTarget hdr;
                Check(hdr.Create(64, 64), "HDR 目標建立");

                auto renderAndRead = [&](const ShadowMap* shadow,
                                         std::vector<float>& out) {
                    hdr.Bind();
                    glClearColor(0, 0, 0, 0);
                    glClear(GL_COLOR_BUFFER_BIT);
                    dl.Render(gb, {sun}, {}, view, 0, shadow, lightSpace,
                              0.05f);
                    hdr.Unbind();
                    hdr.Bind();
                    hdr.ReadColor(out);
                    hdr.Unbind();
                };

                // (a) 無陰影方向光：全場景被照亮
                std::vector<float> noShadow;
                renderAndRead(nullptr, noShadow);
                Check(glGetError() == GL_NO_ERROR,
                      "deferred 渲染無 GL 錯誤");
                float mean = 0.0f;
                for (size_t i = 0; i + 2 < noShadow.size(); i += 4)
                    mean += noShadow[i];
                mean /= (noShadow.size() / 4);
                Check(mean > 0.2f, "方向光照亮場景（平均亮度 > 0.2）");

                // (b) 有陰影：遮蔽板在地板上投出陰影 → 像素變暗
                std::vector<float> withShadow;
                renderAndRead(&sm, withShadow);
                int darkened = 0;
                for (size_t i = 0; i + 2 < noShadow.size(); i += 4) {
                    if (noShadow[i] - withShadow[i] > 0.05f) ++darkened;
                }
                Check(darkened > 0,
                      "陰影開關有實際效果（部分像素變暗）");
                printf("  [info] 陰影壓暗像素 %d\n", darkened);

                // (c) 點光衰減梯度：近光處亮於遠處。
                // 注意遮蔽板 z=-2.5 half=0.7 投影覆蓋螢幕中央 px16-48,
                // 點光放偏一側讓地板亮斑脫離遮蔽板足跡
                DeferredPointLight lamp;
                lamp.position = Vector3(2.5f, -2.5f, -4.0f);
                lamp.color = Vector3(1, 1, 1);
                lamp.intensity = 2.0f;
                lamp.radius = 8.0f;
                hdr.Bind();
                glClear(GL_COLOR_BUFFER_BIT);
                dl.Render(gb, {}, {lamp}, view, 0, nullptr,
                          Matrix4::Identity(), 0.0f);
                hdr.Unbind();
                std::vector<float> pt;
                hdr.Bind();
                hdr.ReadColor(pt);
                hdr.Unbind();
                // 亮斑在地板 (2.5,-2.5,-6) → NDC (0.72,-0.72)
                // → 像素 (row9,col55)；遠處角落 (-2.5,2.5) → (row55,col9)
                const float near_ = pt[(9 * 64 + 55) * 4];
                const float far_ = pt[(55 * 64 + 9) * 4];
                Check(near_ > far_ && near_ > 0.2f,
                      "點光近處亮（衰減梯度存在）");
                printf("  [info] 點光 近 %.3f / 遠 %.3f\n", near_, far_);
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
