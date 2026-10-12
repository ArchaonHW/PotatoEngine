// TextRendererTest - 位圖文字渲染測試
// headless 段：TTF 烘焙圖集（R8 墨水覆蓋率）、Layout/MeasureWidth/
//   換行/未知碼位 fallback、ExpandToRGBA、QueueText 實例產生
// GL 段：SpriteBatcher 單次 instanced draw 畫字串 → 讀回驗證墨水
// TTF 來源：系統字型目錄（找不到 → 全測試 SKIP，不當失敗）

#include "Rendering/FontAtlas.h"
#include "Rendering/TextRenderer.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/SpriteBatcher.h"
#include "MathUtils/MathUtils.h"
#include "Platform/GLFWSharedContext.h"

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <fstream>
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

static bool FindFont(std::string& out) {
    const char* candidates[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (const char* p : candidates) {
        if (std::ifstream(p, std::ios::binary).good()) {
            out = p;
            return true;
        }
    }
    return false;
}

int main() {
    printf("=== Text Renderer Tests ===\n\n");

    // [0] 無效輸入安全
    {
        FontAtlas bad;
        Check(!bad.Bake(nullptr, 0, 16.f), "nullptr TTF → Bake 失敗");
        const uint8 junk[16] = {0};
        Check(!bad.Bake(junk, sizeof(junk), 16.f), "垃圾資料 → Bake 失敗");
        Check(!bad.IsValid(), "失敗後 IsValid 保持 false");
        Check(bad.Layout("abc").empty(), "無效圖集 Layout 回空");
        Check(bad.MeasureWidth("abc") == 0.f, "無效圖集 MeasureWidth 回 0");
    }

    std::string fontPath;
    if (!FindFont(fontPath)) {
        printf("  [SKIP] 找不到系統 TTF 字型——跳過烘焙與 GL 測試\n");
        g_skip++;
    } else {
        printf("  [info] 使用字型 %s\n", fontPath.c_str());

        FontAtlas font;
        Check(font.BakeFromFile(fontPath, 24.f), "BakeFromFile 成功");
        Check(font.IsValid() && font.GetAtlasSize() == 512,
              "圖集有效 512x512");
        Check(font.GetPixels().size() == 512 * 512,
              "R8 圖集大小正確");

        // 有墨水：coverage 有非零值
        {
            size_t ink = 0;
            for (uint8 v : font.GetPixels()) if (v > 0) ++ink;
            Check(ink > 1000, "圖集有墨水（非零 coverage 像素）");
        }

        // advance：'i' 窄於 'W'、space 有 advance 但無 quad
        Check(font.GetAdvance('W') > font.GetAdvance('i'),
              "'W' advance > 'i' advance");
        Check(font.GetAdvance(' ') > 0.f, "space 有 advance");
        Check(font.GetAdvance(0x4E2D) == font.GetAdvance('?'),
              "未知碼位 fallback 到 '?'");

        // Layout
        {
            const auto q = font.Layout("Hi", 1.0f);
            Check(q.size() == 2, "Layout(\"Hi\") 兩個 quad");
            Check(q[0].w > 0 && q[0].h > 0, "'H' quad 非零尺寸");
            Check(q[1].x > q[0].x, "'i' pen 在 'H' 右側");
            Check(std::fabs(q[0].u1 - q[0].u0) > 0 &&
                      std::fabs(q[0].v1 - q[0].v0) > 0,
                  "uv 非退化");

            const auto nl = font.Layout("A\nB", 1.0f);
            Check(nl.size() == 2, "換行後仍兩個 quad");
            Check(std::fabs(nl[1].y - (nl[0].y + font.GetLineHeight()))
                      < 0.5f || nl[1].y != nl[0].y,
                  "換行 quad 移到下一行");
            // pen.x 歸零：'B' 的 quad.x 只剩自身 bearing
            Check(std::fabs(nl[1].x - font.Layout("B")[0].x) < 1e-4f,
                  "換行 pen.x 歸零");

            Check(font.MeasureWidth("AA") > font.MeasureWidth("A"),
                  "MeasureWidth 隨字數增加");
            Check(font.MeasureWidth("A\nBBBB") >
                      font.MeasureWidth("A"),
                  "MeasureWidth 取最長行");

            const auto scaled = font.Layout("H", 2.0f);
            const auto base = font.Layout("H", 1.0f);
            Check(std::fabs(scaled[0].w - base[0].w * 2.f) < 0.1f,
                  "scale=2 尺寸加倍");
        }

        // ExpandToRGBA
        {
            const auto rgba = TextRenderer::ExpandToRGBA(font);
            Check(rgba.size() == font.GetPixels().size() * 4,
                  "RGBA 展開尺寸");
            bool whiteOpaque = false, alphaOk = false;
            for (size_t i = 0; i < font.GetPixels().size(); ++i) {
                if (font.GetPixels()[i] > 200) {
                    if (rgba[i * 4] == 255 && rgba[i * 4 + 3] > 200)
                        whiteOpaque = true;
                }
            }
            for (size_t i = 0; i < font.GetPixels().size(); ++i) {
                if (font.GetPixels()[i] == 0 && rgba[i * 4 + 3] == 0)
                    alphaOk = true;
            }
            Check(whiteOpaque, "墨水區 rgb=白 alpha=高");
            Check(alphaOk, "空白區 alpha=0");
        }

        // QueueText（純 CPU）
        {
            const auto inst = TextRenderer::QueueText(
                font, "Hi", 10.f, 30.f, 1.0f, Vector4(1, 0, 0, 1),
                0.5f);
            Check(inst.size() == 2, "QueueText 產生兩實例");
            Check(inst[0].r == 1.f && inst[0].g == 0.f &&
                      inst[0].a == 1.f,
                  "色調透傳");
            Check(inst[0].z == 0.5f, "zOrder 透傳");
            Check(inst[0].x > 10.f && inst[0].y > 0.f,
                  "實例位置隨 pen 偏移");
            // space 無 quad
            const auto sp = TextRenderer::QueueText(font, "A B", 0, 0);
            Check(sp.size() == 2, "空格不產生 quad（僅佔位）");
        }

        // [GL] 實際渲染
        if (!glfwInit()) {
            printf("  [SKIP] glfwInit 失敗——GL 段跳過\n");
            g_skip++;
        } else {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE,
                           GLFW_OPENGL_CORE_PROFILE);
            GLFWwindow* win =
                glfwCreateWindow(64, 64, "text", nullptr, nullptr);
            if (!win) {
                printf("  [SKIP] glfwCreateWindow 失敗\n");
                g_skip++;
            } else {
                glfwMakeContextCurrent(win);
                if (!gladLoadGLLoader(
                        (GLADloadproc)glfwGetProcAddress)) {
                    printf("  [SKIP] gladLoadGLLoader 失敗\n");
                    g_skip++;
                } else {
                    glGetError();

                    TextRenderer tr;
                    Check(tr.Create(font) && tr.IsValid(),
                          "TextRenderer::Create 上傳圖集");
                    Check(tr.GetTexture() != 0, "圖集 GL id 非零");

                    SpriteBatcher batch;
                    Check(batch.Create(), "SpriteBatcher 建立");

                    RenderTarget rt;
                    Check(rt.Create(128, 64), "RenderTarget 建立");

                    rt.Bind();
                    glViewport(0, 0, 128, 64);
                    glClearColor(0, 0, 0, 1);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA,
                                GL_ONE_MINUS_SRC_ALPHA);
                    glDisable(GL_DEPTH_TEST);

                    const auto inst = TextRenderer::QueueText(
                        font, "Hi!", 4.f, 40.f, 1.0f,
                        Vector4(1, 1, 1, 1));
                    batch.Render(inst, tr.GetTexture(),
                                 Matrix4::Orthographic(0, 128, 64, 0,
                                                       -1, 1));
                    glDisable(GL_BLEND);
                    rt.Unbind();
                    Check(glGetError() == GL_NO_ERROR,
                          "文字渲染無 GL 錯誤");

                    std::vector<unsigned char> px;
                    rt.Bind();
                    Check(rt.ReadColor(px), "文字結果讀回");
                    rt.Unbind();
                    int inkPx = 0;
                    for (size_t i = 0; i + 2 < px.size(); i += 4) {
                        if (px[i] > 100) ++inkPx; // 白字亮像素
                    }
                    Check(inkPx > 20,
                          "字串產生墨水滴落像素（glyph 真的畫出來）");
                    printf("  [info] 墨水滴落像素 %d\n", inkPx);
                }
                DestroyGLFWWindow(win);
            }
            glfwTerminate();
        }
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
