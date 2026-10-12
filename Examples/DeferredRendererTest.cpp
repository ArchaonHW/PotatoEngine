// DeferredRendererTest - 延遲渲染管線 facade 整合測試
// GL 段：BeginShadowPass → BeginGeometry → RenderFrame 全鏈，
//   驗證最終 LDR 有亮暗分布（陰影/光照確實走完整條 pipe）
// 無顯示環境下 GL 段跳過（PASS）——CI/headless 不應失敗

#include "MathUtils/MathUtils.h"
#include "Rendering/DeferredRenderer.h"
#include "Rendering/IBL.h"
#include "Rendering/ScreenSpaceReflection.h"
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

    // [1b] SSR 純 CPU 段（不需 GL context）
    {
        const auto ts = ScreenSpaceReflection::MarchStepTs(8, 4.0f);
        bool mono = ts.size() == 8;
        float prev = 0.0f;
        for (float t : ts) {
            if (t <= prev) mono = false;
            prev = t;
        }
        Check(mono && ts.front() > 0.0f && ts.back() == 4.0f,
              "SSR 行進步長遞增至 maxDist");
        Check(ts[0] < ts[7] - ts[6],
              "二次分布近密遠疏");
        const Matrix4 proj =
            Matrix4::Perspective(90.0f * DEG_TO_RAD, 1.0f, 0.1f, 10.0f);
        float u = 0.0f, v = 0.0f;
        Check(ScreenSpaceReflection::ViewToUV(Vector3(0, 0, -1), proj,
                                              u, v) &&
                  std::fabs(u - 0.5f) < 1e-4f &&
                  std::fabs(v - 0.5f) < 1e-4f,
              "ViewToUV 軸上點落螢幕中央");
        Check(!ScreenSpaceReflection::ViewToUV(Vector3(0, 0, 1), proj,
                                               u, v),
              "相機後方投影失敗");
    }

    // [1c] 透明佇列排序（純 CPU）：view=identity，z 越小越遠
    //      ——遠者先畫才符合 alpha 疊加語義
    {
        std::vector<TransparentDraw> draws(3);
        draws[0].model = Matrix4::Translation(0.0f, 0.0f, -2.0f);
        draws[1].model = Matrix4::Translation(0.0f, 0.0f, -8.0f);
        draws[2].model = Matrix4::Translation(0.0f, 0.0f, -4.0f);
        const auto order = DeferredRenderer::SortBackToFront(
            draws, Matrix4::Identity());
        Check(order.size() == 3 && order[0] == 1 && order[1] == 2 &&
                  order[2] == 0,
              "透明佇列依 view 深度遠→近排序");
    }

    // [1d] Mesh 本地 bounds + AABB::Transformed + 不透明佇列剔除
    //      （全純 CPU——Mesh 惰性上傳,無 GL 也可建構）
    {
        Mesh quad = MakeQuad(1.0f, 2.0f, -3.0f, 4.0f);
        Check(quad.HasBounds(), "Mesh::HasBounds 於 SetVertices 後成立");
        const AABB& b = quad.GetLocalAABB();
        Check(std::fabs(b.min.x + 3.0f) < 1e-5f &&
                  std::fabs(b.max.x - 5.0f) < 1e-5f &&
                  std::fabs(b.min.z + 3.0f) < 1e-5f &&
                  std::fabs(b.max.z + 3.0f) < 1e-5f,
              "Mesh 本地 AABB 由頂點算出");
        Check(std::fabs(quad.GetBoundingCenter().x - 1.0f) < 1e-5f &&
                  std::fabs(quad.GetBoundingCenter().y - 2.0f) < 1e-5f &&
                  std::fabs(quad.GetBoundingRadius() - 5.6569f) < 1e-3f,
              "包圍球心/半徑正確（盒心+對角）");

        // AABB::Transformed：平移 10 後盒子跟著平移
        const AABB moved =
            b.Transformed(Matrix4::Translation(10, 0, 0));
        Check(std::fabs(moved.min.x - 7.0f) < 1e-5f &&
                  std::fabs(moved.max.x - 15.0f) < 1e-5f,
              "AABB::Transformed 平移正確");

        // CullGeometryQueue：前方保留、後方剔除、無 bounds 跳過
        Mesh front = MakeQuad(0, 0, -10.0f, 1.0f);
        Mesh back = MakeQuad(0, 0, 10.0f, 1.0f);
        Mesh empty;
        std::vector<OpaqueDraw> draws(3);
        draws[0].mesh = &front;
        draws[1].mesh = &back;
        draws[2].mesh = &empty;
        const Matrix4 viewProj = Matrix4::Perspective(
            60.0f * DEG_TO_RAD, 1.0f, 0.1f, 50.0f);
        const auto kept =
            DeferredRenderer::CullGeometryQueue(draws, viewProj);
        Check(kept.size() == 1 && kept[0] == 0,
              "CullGeometryQueue 剔除後方與無 bounds draw");
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
                // PBR 漫射帶 1/π 係數——亮度需 >3 才能越 ACES 肩點
                sun.color = Vector3(10.0f, 9.5f, 8.5f);
                sun.intensity = 1.0f;
                sun.castShadow = true;
                p.dirs.push_back(sun);
                p.bloomStrength = 0.15f; // 滿版亮區的 bloom bleed 會
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

                // SSR：同場景重跑 geometry + RenderFrame，地板朝
                // 相機的反射射線經 z=-2.5 遮蔽板 → 反射區像素改變
                {
                    DeferredRendererSettings ssrCfg;
                    ssrCfg.enableSSR = true;
                    ssrCfg.ssr.maxDistance = 20.0f;
                    DeferredRenderer drSSR;
                    Check(drSSR.Create(64, 64, ssrCfg) &&
                              drSSR.IsValid(),
                          "SSR renderer 建立");
                    drSSR.BeginGeometry();
                    geo.Bind();
                    geo.SetUniformMat4("model", model);
                    geo.SetUniformMat4("view", view);
                    geo.SetUniformMat4("projection", proj);
                    geo.SetUniformVec3(
                        "albedo", Vector3(0.9f, 0.9f, 0.9f));
                    geo.SetUniformInt("useTexture", 0);
                    floor.Draw();
                    blocker.Draw();
                    geo.Unbind();
                    drSSR.EndGeometry();
                    RenderTarget ldr2;
                    Check(ldr2.Create(64, 64), "SSR LDR 目標建立");
                    drSSR.RenderFrame(p, ldr2.GetFBO());
                    std::vector<unsigned char> px2;
                    ldr2.Bind();
                    ldr2.ReadColor(px2);
                    ldr2.Unbind();
                    int changed = 0;
                    for (size_t i = 0; i + 2 < px.size(); i += 4) {
                        if (std::abs(static_cast<int>(px2[i]) -
                                     static_cast<int>(px[i])) > 6)
                            ++changed;
                    }
                    Check(changed > 0, "SSR 改變地板反射區像素");
                    printf("  [info] SSR 差異像素 %d\n", changed);
                }

                // 透明 forward pass：紅玻璃疊在地板前被照亮混合；
                // 藍玻璃在遮蔽板後被深度剔除（GBuffer 深度 blit）
                {
                    Mesh glassA = MakeQuad(1.5f, 1.5f, -4.0f, 0.6f);
                    Mesh glassB = MakeQuad(0.0f, 0.0f, -4.0f, 0.5f);
                    // 玻璃 A 走佇列（自動排序路徑），
                    // 玻璃 B 走 drawTransparent 回呼——兩路徑都測
                    TransparentMaterial red;
                    red.albedo = Vector3(1.0f, 0.1f, 0.1f);
                    red.alpha = 0.6f;
                    red.roughness = 0.4f;
                    dr.QueueTransparent(glassA, model, red);
                    Check(dr.GetTransparentCount() == 1,
                          "QueueTransparent 入列");
                    p.drawTransparent = [&](DeferredRenderer& r) {
                        TransparentMaterial blue;
                        blue.albedo = Vector3(0.1f, 0.1f, 1.0f);
                        blue.alpha = 0.9f;
                        r.DrawTransparent(glassB, model, blue);
                    };
                    RenderTarget ldr3;
                    Check(ldr3.Create(64, 64), "透明 LDR 目標建立");
                    dr.RenderFrame(p, ldr3.GetFBO());
                    std::vector<unsigned char> px3;
                    ldr3.Bind();
                    ldr3.ReadColor(px3);
                    ldr3.Unbind();
                    const auto at = [](
                        const std::vector<unsigned char>& b,
                        int x, int y) {
                        return &b[static_cast<size_t>(y) * 64 * 4 +
                                  static_cast<size_t>(x) * 4];
                    };
                    // (52,52) 玻璃 A 區：紅色 alpha 疊上白亮
                    // 地板——r 都近飽和，檢查紅度 (r-g) 增加
                    const unsigned char* a0 = at(px, 52, 52);
                    const unsigned char* a1 = at(px3, 52, 52);
                    Check((static_cast<int>(a1[0]) - a1[1]) -
                              (static_cast<int>(a0[0]) - a0[1]) > 5,
                          "透明玻璃 alpha 混合改變地板像素");
                    // (32,32) 遮蔽板中心：玻璃 B 在其後 → 深度剔除
                    const unsigned char* c0 = at(px, 32, 32);
                    const unsigned char* c1 = at(px3, 32, 32);
                    Check(std::abs(static_cast<int>(c1[0]) -
                                   static_cast<int>(c0[0])) <= 6 &&
                              std::abs(static_cast<int>(c1[1]) -
                                       static_cast<int>(c0[1])) <= 6 &&
                              std::abs(static_cast<int>(c1[2]) -
                                       static_cast<int>(c0[2])) <= 6,
                          "遮蔽板後的透明面被深度剔除");
                    printf("  [info] 玻璃區 rgb (%d,%d,%d)→(%d,%d,%d)"
                           " / 遮蔽後 %d→%d\n",
                           a0[0], a0[1], a0[2], a1[0], a1[1], a1[2],
                           c0[0], c1[0]);
                    p.drawTransparent = nullptr;
                    dr.ClearTransparentQueue();
                }

                // 不透明佇列：floor 經佇列繪製、相機後方 quad
                // 被剔除——DrawGeometryQueue 只回報實際繪製數
                {
                    Mesh behind = MakeQuad(0, 0, 5.0f, 2.0f);
                    OpaqueDraw dfloor;
                    dfloor.mesh = &floor;
                    dfloor.model = model;
                    dfloor.albedo = Vector3(0.9f, 0.9f, 0.9f);
                    OpaqueDraw dback;
                    dback.mesh = &behind;
                    dback.model = model;
                    dback.albedo = Vector3(1.0f, 0.0f, 0.0f);
                    dr.QueueGeometry(dfloor);
                    dr.QueueGeometry(dback);
                    Check(dr.GetGeometryQueueSize() == 2,
                          "QueueGeometry 入列");
                    dr.BeginGeometry();
                    const size_t drawn =
                        dr.DrawGeometryQueue(view, proj);
                    dr.EndGeometry();
                    Check(drawn == 1,
                          "視錐外佇列 draw 被剔除（floor 留 1/2）");
                    RenderTarget ldr4;
                    Check(ldr4.Create(64, 64), "佇列 LDR 目標建立");
                    dr.RenderFrame(p, ldr4.GetFBO());
                    std::vector<unsigned char> px4;
                    ldr4.Bind();
                    ldr4.ReadColor(px4);
                    ldr4.Unbind();
                    int bright4 = 0;
                    for (size_t i = 0; i + 2 < px4.size(); i += 4) {
                        if (px4[i] > 220) ++bright4;
                    }
                    Check(bright4 > 0,
                          "佇列路徑實際繪製地板（有亮像素）");
                    printf("  [info] 佇列繪製 %zu/2,亮像素 %d\n",
                           drawn, bright4);
                    dr.ClearGeometryQueue();
                }

                // forward IBL：α=1 玻璃像素 = 純 forward 輸出——
                // ambient=0 時 flat 路徑只餘直接光，env 路徑多
                // irradiance 漫射 + prefilter 鏡面,α=1 擋住背景
                // 差異全來自前向通道
                {
                    const int EW = 64, EH = 32;
                    const auto sky = IBL::ProceduralSkyEquirect(
                        EW, EH, Vector3(0.0f, 0.0f, -1.0f), 15.0f);
                    EnvironmentMaps env;
                    Check(env.Create(32), "EnvironmentMaps 建立");
                    Check(env.Generate(sky.data(), EW, EH, 16, 64),
                          "env Generate（forward IBL）");
                    Mesh glass = MakeQuad(2.0f, 2.0f, -4.0f, 0.7f);
                    TransparentMaterial tm;
                    tm.albedo = Vector3(0.8f, 0.8f, 0.8f);
                    tm.alpha = 1.0f; // α=1 → 像素=純 forward 輸出
                    tm.roughness = 0.5f;
                    p.drawTransparent = [&](DeferredRenderer& r) {
                        r.DrawTransparent(glass, model, tm);
                    };
                    p.ambient = 0.0f;
                    // 清空直接光：flat 路徑玻璃 ≈ 黑（飽和會
                    // 吃掉 IBL 增量），env 路徑由 IBL 點亮
                    const auto savedDirs = p.dirs;
                    p.dirs.clear();
                    auto shot = [&](const EnvironmentMaps* e,
                                    std::vector<unsigned char>& out) {
                        p.environment = e;
                        RenderTarget rt;
                        rt.Create(64, 64);
                        dr.RenderFrame(p, rt.GetFBO());
                        rt.Bind();
                        rt.ReadColor(out);
                        rt.Unbind();
                    };
                    std::vector<unsigned char> flatPx, iblPx;
                    shot(nullptr, flatPx);
                    shot(&env, iblPx);
                    // (52,52) 玻璃區（避開中央遮蔽板）
                    const auto at = [](
                        const std::vector<unsigned char>& b,
                        int x, int y) {
                        return &b[static_cast<size_t>(y) * 64 * 4 +
                                  static_cast<size_t>(x) * 4];
                    };
                    const unsigned char* fp = at(flatPx, 52, 52);
                    const unsigned char* ip = at(iblPx, 52, 52);
                    const int d = std::abs(int(ip[0]) - fp[0]) +
                                  std::abs(int(ip[1]) - fp[1]) +
                                  std::abs(int(ip[2]) - fp[2]);
                    Check(d > 30,
                          "forward IBL 改變透明物件環境項"
                          "（無直接光時只來自 IBL）");
                    printf("  [info] 玻璃 rgb (%d,%d,%d)→(%d,%d,%d)\n",
                           fp[0], fp[1], fp[2], ip[0], ip[1], ip[2]);
                    p.dirs = savedDirs;
                    p.environment = nullptr;
                    p.iblIntensity = 1.0f;
                    p.ambient = 0.05f;
                    p.drawTransparent = nullptr;
                }

                // FXAA 管線接線：enableFXAA → tone map 進 RGBA8
                // 中介再平滑輸出——畫同一塊地板驗亮像素仍在
                {
                    DeferredRendererSettings aaCfg;
                    aaCfg.enableFXAA = true;
                    DeferredRenderer drAA;
                    Check(drAA.Create(64, 64, aaCfg) && drAA.IsValid(),
                          "FXAA renderer 建立");
                    drAA.BeginGeometry();
                    geo.Bind();
                    geo.SetUniformMat4("model", model);
                    floor.Draw();
                    geo.Unbind();
                    drAA.EndGeometry();
                    RenderTarget ldr5;
                    Check(ldr5.Create(64, 64), "FXAA LDR 目標建立");
                    drAA.RenderFrame(p, ldr5.GetFBO());
                    std::vector<unsigned char> px5;
                    ldr5.Bind();
                    ldr5.ReadColor(px5);
                    ldr5.Unbind();
                    int bright5 = 0;
                    for (size_t i = 0; i + 2 < px5.size(); i += 4) {
                        if (px5[i] > 200) ++bright5;
                    }
                    Check(bright5 > 0,
                          "FXAA 管線輸出有效（有亮像素）");
                    Check(glGetError() == GL_NO_ERROR,
                          "FXAA 管線無 GL 錯誤");
                }
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
