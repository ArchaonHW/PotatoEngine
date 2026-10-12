// ShadowTest - ShadowMap/PCF 陰影管線測試
// headless 段：ComputeLightSpaceMatrix 不變量（中心→NDC 原點、
//   邊界映射、far 外點、垂直光退化處理）
// GL 段：深度 pass → PCF 光照 pass，驗證遮蔽板在地板上投出陰影
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
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

// XZ 平面水平地板（y 固定，法線 +Y），兩三角形
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

// XY 平面直立板（z 固定，法線 +Z），用於遮光
static Mesh MakeBlocker(float z, float cx, float y0, float y1, float halfW) {
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
    printf("=== Shadow Map Tests ===\n\n");

    // [1] headless：light-space 矩陣不變量
    {
        const Vector3 dir(0.35f, -1.0f, 0.45f);   // 未正規化,內部處理
        const Vector3 center(0.0f, -1.0f, 0.0f);
        const float radius = 10.0f;
        const Matrix4 ls = ShadowMap::ComputeLightSpaceMatrix(dir, center, radius);

        // 包圍球中心 → NDC (0, 0, ~0)
        const Vector3 c = ls.TransformPoint(center);
        Check(std::fabs(c.x) < 1e-4f && std::fabs(c.y) < 1e-4f,
              "包圍球中心映射到 NDC x/y≈0");
        Check(c.z > -1.0f && c.z < 1.0f, "包圍球中心 NDC z 在 [-1,1]");

        // 垂直光方向的偏移點 = ortho 邊界：light-view xy 分量長度 = radius
        // → NDC 平面上 sqrt(x^2+y^2) == 1（可落在對角，不限單軸）
        const Vector3 d = dir.Normalized();
        Vector3 perp = d.Cross(Vector3(0, 0, 1));
        if (perp.LengthSquared() < 1e-6f) perp = Vector3(1, 0, 0);
        perp = perp.Normalized();
        const Vector3 edge = ls.TransformPoint(center + perp * radius);
        const float e = std::sqrt(edge.x * edge.x + edge.y * edge.y);
        Check(std::fabs(e - 1.0f) < 0.02f,
              "垂直偏移 radius 的點落在 ortho 邊界（xy 長度≈1）");

        // 越過 far plane 的點 → NDC z > 1（SamplingGLSL 回 1.0 不吃影）
        const Vector3 beyond = ls.TransformPoint(center + d * radius * 2.5f);
        Check(beyond.z > 1.0f, "far plane 外的點 NDC z > 1");

        // 近乎垂直向下的光（dir ∥ world up）不退化
        const Matrix4 down =
            ShadowMap::ComputeLightSpaceMatrix(Vector3(0, -1, 0),
                                               Vector3(0, 0, 0), 5.0f);
        const Vector3 dc = down.TransformPoint(Vector3(0, 0, 0));
        Check(std::fabs(dc.x) < 1e-4f && std::fabs(dc.y) < 1e-4f &&
              dc.z > -1.0f && dc.z < 1.0f,
              "垂直光（dir∥up）light-space 不退化");
    }

    // [2] GL context 前建構——不得觸碰 GL、不得 crash
    {
        ShadowMap sm;
        Check(!sm.IsValid(), "context 前建構不建立 GL 資源");
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
        GLFWwindow* win = glfwCreateWindow(64, 64, "shadow", nullptr, nullptr);
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

                ShadowMap sm;
                Check(sm.Create(1024) && sm.IsValid(),
                      "ShadowMap::Create(1024) 成功");
                Check(sm.GetDepthMap() != 0, "深度貼圖建立");

                // 場景：地板 y=-2 (±10) + 直立遮蔽板 x±3,y[-2,1.5],z=0.5
                Mesh floor = MakeFloor(-2.0f, 10.0f);
                Mesh blocker = MakeBlocker(0.5f, 0.0f, -2.0f, 1.5f, 3.0f);

                const Vector3 lightDir(0.5f, -1.0f, 0.6f);
                const Matrix4 lightSpace = ShadowMap::ComputeLightSpaceMatrix(
                    lightDir, Vector3(0, 0, 0), 10.0f);
                const Matrix4 model = Matrix4::Identity();

                // 深度 pass：場景渲染進 shadow map
                Shader depthShader;
                Check(depthShader.LoadFromSource(ShadowMap::DepthVertexShader(),
                                                 ShadowMap::DepthFragmentShader()),
                      "深度 pass shader 編譯");
                sm.Bind();
                glEnable(GL_DEPTH_TEST);
                glClear(GL_DEPTH_BUFFER_BIT);
                depthShader.Bind();
                depthShader.SetUniformMat4("lightSpaceMatrix", lightSpace);
                depthShader.SetUniformMat4("model", model);
                floor.Draw();
                blocker.Draw();
                depthShader.Unbind();
                sm.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "深度 pass 無 GL 錯誤");

                // 讀回深度貼圖：遮蔽板比地板近光 → 深度必須有變化
                {
                    std::vector<float> depth(1024 * 1024);
                    glBindTexture(GL_TEXTURE_2D, sm.GetDepthMap());
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                                  GL_FLOAT, depth.data());
                    float dMin = 1.0f, dMax = 0.0f;
                    for (float d : depth) {
                        dMin = d < dMin ? d : dMin;
                        dMax = d > dMax ? d : dMax;
                    }
                    Check(dMin < 1.0f && dMax > dMin,
                          "深度圖有幾何差異（非空白/全同值）");
                }

                // 光照 pass：畫地板到 RenderTarget，
                // 片段 shader 用 SamplingGLSL 的 ShadowFactor 輸出明暗
                RenderTarget rt;
                Check(rt.Create(64, 64), "光照 RenderTarget 建立");

                const std::string litVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
uniform mat4 model, view, projection;
out vec3 WorldPos;
out vec3 Normal;
void main()
{
    WorldPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(model) * aNormal;
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}
)";
                const std::string litFS =
                    std::string(R"(
#version 330 core
in vec3 WorldPos;
in vec3 Normal;
out vec4 FragColor;
uniform mat4 lightSpace;
uniform sampler2D shadowMap;
uniform vec3 lightDir;
uniform float bias;
uniform float texelSize;
)") + ShadowMap::SamplingGLSL() + R"(
void main()
{
    float f = ShadowFactor(WorldPos, normalize(Normal), lightDir,
                           lightSpace, shadowMap, bias, texelSize);
    FragColor = vec4(vec3(f), 1.0);
}
)";

                Shader lit;
                Check(lit.LoadFromSource(litVS, litFS),
                      "PCF 光照 shader 編譯（SamplingGLSL 嵌入）");

                const Matrix4 view = Matrix4::LookAt(
                    Vector3(1.0f, 5.0f, 15.0f), Vector3(1.0f, -1.5f, 2.0f),
                    Vector3(0.0f, 1.0f, 0.0f));
                const Matrix4 proj = Matrix4::Perspective(
                    60.0f * DEG_TO_RAD, 1.0f, 0.1f, 100.0f);

                rt.Bind();
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                lit.Bind();
                lit.SetUniformMat4("model", model);
                lit.SetUniformMat4("view", view);
                lit.SetUniformMat4("projection", proj);
                lit.SetUniformMat4("lightSpace", lightSpace);
                lit.SetUniformVec3("lightDir", lightDir.Normalized());
                lit.SetUniformFloat("bias", 0.005f);
                lit.SetUniformFloat("texelSize", 1.0f / 1024.0f);
                lit.SetUniformInt("shadowMap", 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sm.GetDepthMap());
                floor.Draw();
                lit.Unbind();
                rt.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "光照 pass 無 GL 錯誤");

                // 讀回：陰影區 factor<1 → 像素變暗
                std::vector<unsigned char> px;
                rt.Bind();
                Check(rt.ReadColor(px), "光照結果讀回");
                rt.Unbind();
                int dark = 0, lit2 = 0;
                for (size_t i = 0; i + 2 < px.size(); i += 4) {
                    if (px[i] < 128) ++dark;      // 陰影
                    else if (px[i] > 200) ++lit2; // 受光
                }
                Check(dark > 0, "地板出現陰影區（factor<0.5 的像素）");
                Check(lit2 > 0, "地板存在受光區（factor≈1 的像素）");

                printf("  [info] 陰影像素 %d / 受光 %d\n", dark, lit2);
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
