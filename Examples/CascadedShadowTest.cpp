// CascadedShadowTest - 級聯陰影貼圖（CSM）測試
// headless 段：ComputeSplits 邊界不變量（端點/單調/λ=0 均勻、λ=1 對數）、
//   ViewDistToNdcZ 單調、FrustumSliceCorners 角點距離、
//   ComputeSliceLightSpace 緊密包圍（8 角點全在 NDC 內）
// GL 段：4 級 depth array 逐層渲染 → ShadowFactorCSM 光照 pass
//   驗證近/遠兩塊遮蔽板在各自級聯層投出陰影
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/CascadedShadowMap.h"
#include "Rendering/RenderTarget.h"
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
#include <string>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

static Mesh MakeFloor(float y, float half) {
    Mesh m;
    std::vector<Vertex> v(6);
    const Vector3 n(0.0f, 1.0f, 0.0f);
    const float p[6][2] = {
        {-half, -half}, { half, -half}, { half,  half},
        {-half, -half}, { half,  half}, {-half,  half},
    };
    for (int i = 0; i < 6; ++i) {
        v[i].position = Vector3(p[i][0], y, p[i][1]);
        v[i].normal = n;
        v[i].texCoord = Vector2(0.0f, 0.0f);
    }
    m.SetVertices(v);
    return m;
}

static Mesh MakeBlocker(float z, float cx, float y0, float y1,
                        float halfW) {
    Mesh m;
    std::vector<Vertex> v(6);
    const Vector3 n(0.0f, 0.0f, 1.0f);
    const float p[6][2] = {
        {cx - halfW, y0}, {cx + halfW, y0}, {cx + halfW, y1},
        {cx - halfW, y0}, {cx + halfW, y1}, {cx - halfW, y1},
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
    printf("=== Cascaded Shadow Map Tests ===\n\n");

    // [1] headless：切片邊界不變量
    {
        const auto e = CascadedShadowMap::ComputeSplits(0.5f, 200.f, 4, 0.6f);
        Check(e.size() == 5, "N+1 個邊界");
        Check(e.front() == 0.5f && e.back() == 200.f,
              "首尾等於 near/far");
        bool mono = true;
        for (size_t i = 1; i < e.size(); ++i)
            if (e[i] <= e[i - 1]) mono = false;
        Check(mono, "邊界嚴格遞增");

        // λ=0 → 均勻；λ=1 → 對數（比值恆定）
        const auto lin =
            CascadedShadowMap::ComputeSplits(1.f, 100.f, 4, 0.0f);
        Check(std::fabs(lin[1] - 25.75f) < 0.01f,
              "λ=0 為均勻切分");
        const auto lg =
            CascadedShadowMap::ComputeSplits(1.f, 100.f, 4, 1.0f);
        const float r1 = lg[1] / lg[0], r2 = lg[2] / lg[1];
        Check(std::fabs(r1 - r2) < 0.01f && r1 > 3.f,
              "λ=1 為對數切分（等比）");
    }

    // [2] headless：切片角點與 light-space 緊密包圍
    {
        const Matrix4 view = Matrix4::LookAt(
            Vector3(0, 10, 30), Vector3(0, 0, -20), Vector3(0, 1, 0));
        const Matrix4 proj = Matrix4::Perspective(
            60.0f * DEG_TO_RAD, 16.0f / 9.0f, 0.5f, 200.0f);

        // NDC z 隨距離單調遞增
        Check(CascadedShadowMap::ViewDistToNdcZ(proj, 10.f) <
                  CascadedShadowMap::ViewDistToNdcZ(proj, 50.f),
              "ViewDistToNdcZ 單調遞增");

        // 切片 10..50 的 8 角點：view-space 深度應分別為 10 或 50
        const auto corners = CascadedShadowMap::FrustumSliceCorners(
            view, proj, 10.f, 50.f);
        Check(corners.size() == 8, "切片 8 角點");
        bool depthsOk = true;
        for (const auto& c : corners) {
            const float vd = -view.TransformPoint(c).z;
            if (std::fabs(vd - 10.f) > 0.01f &&
                std::fabs(vd - 50.f) > 0.01f)
                depthsOk = false;
        }
        Check(depthsOk, "角點 view 深度 ∈ {10, 50}");

        // 緊密包圍：8 角點全映射進 NDC [-1,1]
        const Vector3 dir(0.4f, -1.f, 0.3f);
        const Matrix4 ls = CascadedShadowMap::ComputeSliceLightSpace(
            dir, corners.data(), 1024);
        // z：near/far 各留 range 餘量，切片角點落在 (-1,1) 內側
        bool inside = true;
        for (const auto& c : corners) {
            const Vector3 p = ls.TransformPoint(c);
            if (std::fabs(p.x) > 1.001f || std::fabs(p.y) > 1.001f ||
                p.z < -1.001f || p.z > 1.001f) {
                inside = false;
                printf("  [dbg] 角點越界 ndc=(%.4f %.4f %.4f)\n",
                       p.x, p.y, p.z);
            }
        }
        Check(inside, "切片角點全在 light-space NDC 內");

        // 質心應落在 NDC 原點附近（緊密包圍以角點群為中心）
        Vector3 centroid(0, 0, 0);
        for (const auto& c : corners) centroid += c;
        centroid = centroid * (1.f / 8.f);
        const Vector3 cn = ls.TransformPoint(centroid);
        Check(std::fabs(cn.x) < 0.5f && std::fabs(cn.y) < 0.5f,
              "質心映射近 NDC 中心");

        // 垂直光不退化
        const Matrix4 down = CascadedShadowMap::ComputeSliceLightSpace(
            Vector3(0, -1, 0), corners.data(), 1024);
        bool ok2 = true;
        for (const auto& c : corners) {
            const Vector3 p = down.TransformPoint(c);
            if (std::fabs(p.x) > 1.01f || std::fabs(p.y) > 1.01f)
                ok2 = false;
        }
        Check(ok2, "垂直光（dir∥up）切片包圍不退化");
    }

    // [3] context 前建構安全
    {
        CascadedShadowMap c;
        Check(!c.IsValid(), "context 前建構不建立 GL 資源");
        Check(CascadedShadowMap::SamplingGLSL() != nullptr,
              "SamplingGLSL 非空");
    }

    // [4] GL 段
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境,GL 路徑測試跳過\n");
        g_skip++;
    } else {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* win =
            glfwCreateWindow(64, 64, "csm", nullptr, nullptr);
        if (!win) {
            printf("  [SKIP] glfwCreateWindow 失敗\n");
            g_skip++;
        } else {
            glfwMakeContextCurrent(win);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                printf("  [SKIP] gladLoadGLLoader 失敗\n");
                g_skip++;
            } else {
                glGetError();

                CascadedShadowMap csm;
                Check(csm.Create(512, 4) && csm.IsValid(),
                      "CascadedShadowMap::Create(512,4)");
                Check(csm.GetDepthArray() != 0 &&
                          csm.GetCascadeCount() == 4,
                      "深度陣列建立");

                // 大場景：地板半徑 80、近遮蔽板 z=-8、遠遮蔽板 z=-60
                Mesh floor = MakeFloor(-2.0f, 80.0f);
                Mesh nearB = MakeBlocker(-8.0f, -3.0f, -2.f, 2.f, 2.5f);
                Mesh farB = MakeBlocker(-60.f, 3.0f, -2.f, 8.f, 6.f);

                const Vector3 lightDir(0.4f, -1.f, 0.35f);
                const Matrix4 view = Matrix4::LookAt(
                    Vector3(0, 12, 35), Vector3(0, -1, -30),
                    Vector3(0, 1, 0));
                const Matrix4 proj = Matrix4::Perspective(
                    60.0f * DEG_TO_RAD, 1.0f, 0.5f, 200.0f);
                const auto edges = CascadedShadowMap::ComputeSplits(
                    0.5f, 200.f, 4, 0.6f);

                std::vector<Matrix4> ls(4);
                Shader depthShader;
                Check(depthShader.LoadFromSource(
                          ShadowMap::DepthVertexShader(),
                          ShadowMap::DepthFragmentShader()),
                      "深度 shader 編譯");
                depthShader.Bind();
                depthShader.SetUniformMat4("model",
                                           Matrix4::Identity());
                // 深度測試關閉時 GL 不寫深度——depth-only pass 必須開
                glEnable(GL_DEPTH_TEST);
                for (int i = 0; i < 4; ++i) {
                    const auto corners =
                        CascadedShadowMap::FrustumSliceCorners(
                            view, proj, edges[i], edges[i + 1]);
                    ls[i] = CascadedShadowMap::ComputeSliceLightSpace(
                        lightDir, corners.data(), 512);
                    csm.BindCascade(i);
                    glClear(GL_DEPTH_BUFFER_BIT);
                    depthShader.SetUniformMat4("lightSpaceMatrix",
                                               ls[i]);
                    floor.Draw();
                    nearB.Draw();
                    farB.Draw();
                }
                depthShader.Unbind();
                csm.Unbind();
                Check(glGetError() == GL_NO_ERROR, "逐層深度 pass");

                // 讀回深度：逐層綁 FBO + glReadPixels（glGetTexImage
                // 對 depth array 在部分驅動行為不一致）
                {
                    std::vector<float> d(512 * 512);
                    auto layerMin = [&](int l) {
                        csm.BindCascade(l);
                        glReadPixels(0, 0, 512, 512,
                                     GL_DEPTH_COMPONENT, GL_FLOAT,
                                     d.data());
                        float m = 1.f;
                        for (float v : d) m = std::min(m, v);
                        return m;
                    };
                    const float lm[4] = {layerMin(0), layerMin(1),
                                         layerMin(2), layerMin(3)};
                    csm.Unbind();
                    printf("  [info] layerMin: %.4f %.4f %.4f %.4f\n",
                           lm[0], lm[1], lm[2], lm[3]);
                    Check(lm[0] < 1.f || lm[1] < 1.f,
                          "近端級聯層有深度內容");
                    Check(lm[2] < 1.f || lm[3] < 1.f,
                          "遠端級聯層有深度內容");
                    Check(lm[0] != lm[1] || lm[2] != lm[3],
                          "不同級聯深度內容不同");
                }

                // 光照 pass：ShadowFactorCSM + 視深選層
                RenderTarget rt;
                Check(rt.Create(128, 128), "光照 RenderTarget");

                const std::string litFS =
                    std::string(R"(
#version 330 core
in vec3 WorldPos;
in vec3 Normal;
in vec3 ViewPos;
out vec4 FragColor;
uniform mat4 csmLightSpace[4];
uniform vec4 csmSplits;
uniform sampler2DArray csmMap;
uniform vec3 lightDir;
uniform float bias;
uniform float texelSize;
)") + CascadedShadowMap::SamplingGLSL() + R"(
void main()
{
    float vd = -ViewPos.z;
    int layer = 0;
    for (int c = 0; c < 3; ++c)
        if (vd > csmSplits[c]) ++layer;
    float f = ShadowFactorCSM(WorldPos, normalize(Normal),
                              lightDir, csmLightSpace[layer],
                              csmMap, layer, bias, texelSize);
    FragColor = vec4(vec3(f), 1.0);
}
)";
                const std::string litVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 model, view, projection;
out vec3 WorldPos;
out vec3 Normal;
out vec3 ViewPos;
void main()
{
    WorldPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(model) * aNormal;
    vec4 vp = view * vec4(WorldPos, 1.0);
    ViewPos = vp.xyz;
    gl_Position = projection * vp;
}
)";
                Shader lit;
                Check(lit.LoadFromSource(litVS, litFS),
                      "CSM 光照 shader 編譯");

                rt.Bind();
                glViewport(0, 0, 128, 128);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                lit.Bind();
                lit.SetUniformMat4("model", Matrix4::Identity());
                lit.SetUniformMat4("view", view);
                lit.SetUniformMat4("projection", proj);
                lit.SetUniformVec3("lightDir",
                                   lightDir.Normalized());
                lit.SetUniformFloat("bias", 0.004f);
                lit.SetUniformFloat("texelSize", 1.f / 512.f);
                lit.SetUniformInt("csmMap", 0);
                glUniformMatrix4fv(
                    glGetUniformLocation(lit.GetProgramID(),
                                         "csmLightSpace[0]"),
                    4, false, ls[0].Data());
                const float sp[4] = {edges[1], edges[2], edges[3],
                                     1e30f};
                glUniform4fv(
                    glGetUniformLocation(lit.GetProgramID(),
                                         "csmSplits"),
                    1, sp);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D_ARRAY,
                              csm.GetDepthArray());
                floor.Draw();
                nearB.Draw();
                farB.Draw();
                lit.Unbind();
                rt.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "CSM 光照 pass 無 GL 錯誤");

                std::vector<unsigned char> px;
                rt.Bind();
                Check(rt.ReadColor(px), "CSM 光照讀回");
                rt.Unbind();
                int dark = 0, litPx = 0;
                for (size_t i = 0; i + 2 < px.size(); i += 4) {
                    if (px[i] < 128) ++dark;
                    else if (px[i] > 200) ++litPx;
                }
                Check(dark > 0, "近/遠遮蔽板產生陰影像素");
                Check(litPx > 0, "存在受光像素");
                printf("  [info] 陰影 %d / 受光 %d\n", dark, litPx);
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
