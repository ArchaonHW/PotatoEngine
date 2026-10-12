// PrimitiveDrawTest - PrimitiveBatcher（立即模式線段/三角形）
// + PrimitiveMeshes（程序化 3D 網格）測試
// headless 段：頂點產生數量/閉合性/bounds/法線長度
// GL 段：batcher 畫線到 RenderTarget 讀回顏色、cube mesh
//   經簡易 shader 實際光柵化——無顯示環境 SKIP

#include "MathUtils/MathUtils.h"
#include "Rendering/PrimitiveBatcher.h"
#include "Rendering/PrimitiveMeshes.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/OpenGLRenderer.h"
#include "Platform/GLFWSharedContext.h"

#ifndef GLFW_INCLUDE_NONE
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

int main() {
    printf("=== Primitive Draw Tests ===\n\n");

    // [1] PrimitiveBatcher 頂點累積（純 CPU）
    {
        PrimitiveBatcher pb;
        Check(pb.GetLineVertexCount() == 0 &&
                  pb.GetTriangleVertexCount() == 0,
              "新建 batcher 為空");
        const Vector4 white(1, 1, 1, 1);
        pb.Line(Vector3(0, 0, 0), Vector3(1, 0, 0), white);
        Check(pb.GetLineVertexCount() == 2, "Line +2 頂點");

        // Box：12 邊 = 24 頂點
        pb.Clear();
        pb.Box(AABB(Vector3(-1, -1, -1), Vector3(1, 1, 1)),
               white);
        Check(pb.GetLineVertexCount() == 24,
              "AABB 線框 = 24 頂點（12 邊）");
        // 無效盒不產生頂點
        pb.Clear();
        pb.Box(AABB(Vector3(1, 1, 1), Vector3(-1, -1, -1)),
               white);
        Check(pb.GetLineVertexCount() == 0,
              "無效 AABB 不產生頂點");

        // Circle：segments 條邊 = 2·segments 頂點且閉合
        pb.Clear();
        pb.Circle(Vector3(0, 0, 0), 1.0f, Vector3(0, 1, 0),
                  white, 16);
        Check(pb.GetLineVertexCount() == 32,
              "Circle(16) = 32 頂點");

        // WireSphere：3 圓
        pb.Clear();
        pb.WireSphere(Vector3(0, 0, 0), 1.0f, white, 16);
        Check(pb.GetLineVertexCount() == 96,
              "WireSphere = 3 圓 = 96 頂點");

        // Grid：每向 (2d+1) 條線 × 2 向 × 2 頂點
        pb.Clear();
        pb.Grid(4.0f, 4, white);
        Check(pb.GetLineVertexCount() == (9 * 2) * 2,
              "Grid(d=4) = 36 頂點");

        // Axes：3 線 = 6 頂點
        pb.Clear();
        pb.Axes(1.0f);
        Check(pb.GetLineVertexCount() == 6, "Axes = 6 頂點");

        // Rect：4 邊 = 8 頂點
        pb.Clear();
        pb.Rect(0, 0, 10, 10, 0, white);
        Check(pb.GetLineVertexCount() == 8, "Rect = 8 頂點");

        // FrustumBox：12 邊 = 24 頂點
        pb.Clear();
        const Vector3 fr[8] = {
            {0, 0, -1}, {1, 0, -1}, {0, 1, -1}, {1, 1, -1},
            {0, 0, -5}, {2, 0, -5}, {0, 2, -5}, {2, 2, -5},
        };
        pb.FrustumBox(fr, white);
        Check(pb.GetLineVertexCount() == 24,
              "FrustumBox = 24 頂點");

        // Triangle/Quad 走三角佇列
        pb.Clear();
        pb.Quad(Vector3(0, 0, 0), Vector3(1, 0, 0),
                Vector3(1, 1, 0), Vector3(0, 1, 0), white);
        Check(pb.GetTriangleVertexCount() == 6,
              "Quad = 2 三角 = 6 頂點");
    }

    // [2] PrimitiveMeshes：頂點/索引數量、法線、bounds
    {
        const Mesh cube = PrimitiveMeshes::MakeCube(2.0f);
        Check(cube.GetVertexCount() == 24 &&
                  cube.GetIndexCount() == 36,
              "cube = 24 頂點 36 索引");
        Check(cube.HasBounds(), "cube 有 bounds");
        {
            const AABB& b = cube.GetLocalAABB();
            Check(std::fabs(b.min.x + 1.0f) < 1e-5f &&
                      std::fabs(b.max.x - 1.0f) < 1e-5f,
                  "cube bounds = ±1（size=2）");
        }
        {
            // 每面法線是軸向量、頂點法線長度 1
            bool unitN = true;
            for (const Vertex& v : cube.GetVertices())
                if (std::fabs(v.normal.Length() - 1.0f) > 1e-5f)
                    unitN = false;
            Check(unitN, "cube 法線全部單位長");
        }

        const Mesh sphere =
            PrimitiveMeshes::MakeUVSphere(1.0f, 24, 16);
        Check(sphere.GetVertexCount() == 25 * 17,
              "sphere(24,16) = 25×17 頂點");
        Check(sphere.GetIndexCount() == 24 * 16 * 6,
              "sphere = sectors·stacks·6 索引");
        {
            bool onSurface = true;
            for (const Vertex& v : sphere.GetVertices()) {
                if (std::fabs(v.position.Length() - 1.0f) >
                    1e-4f)
                    onSurface = false;
                // 球法線 = 位置方向
                if (v.normal.Length() > 1e-6f &&
                    v.normal.Dot(v.position.Normalized()) <
                        0.999f)
                    onSurface = false;
            }
            Check(onSurface,
                  "sphere 頂點在球面且法線 = 徑向");
        }

        const Mesh cyl =
            PrimitiveMeshes::MakeCylinder(1.0f, 2.0f, 16);
        // 側面 (16+1)*2 + 兩蓋各 (1 + 17) = 34+36=70
        Check(cyl.GetVertexCount() == 70,
              "cylinder 頂點數 = 側面 34 + 蓋 36");
        {
            bool hasCapNormal = false;
            bool hasSideNormal = false;
            for (const Vertex& v : cyl.GetVertices()) {
                if (v.normal.y > 0.9f) hasCapNormal = true;
                if (std::fabs(v.normal.y) < 0.1f)
                    hasSideNormal = true;
            }
            Check(hasCapNormal && hasSideNormal,
                  "cylinder 有蓋法線與側面法線");
        }

        const Mesh plane =
            PrimitiveMeshes::MakePlane(4.0f, 4.0f, 2, 2);
        Check(plane.GetVertexCount() == 9 &&
                  plane.GetIndexCount() == 24,
              "plane(2x2) = 9 頂點 24 索引");
        {
            bool allUp = true;
            for (const Vertex& v : plane.GetVertices())
                if (v.normal.y < 0.999f) allUp = false;
            Check(allUp, "plane 法線全 +Y");
        }

        const Mesh quad = PrimitiveMeshes::MakeQuad(2, 1);
        Check(quad.GetVertexCount() == 4 &&
                  quad.GetIndexCount() == 6,
              "quad = 4 頂點 6 索引");

        const Mesh cone = PrimitiveMeshes::MakeCone(1, 2, 16);
        Check(cone.GetVertexCount() > 0 &&
                  cone.GetIndexCount() > 0,
              "cone 產生頂點/索引");
    }

    // [3] GL context 前建構安全
    {
        PrimitiveBatcher pb;
        Check(!pb.IsValid(), "context 前 batcher 無效");
    }

    // [4] GL 段：實際光柵化
    if (!glfwInit()) {
        printf("  [SKIP] glfwInit 失敗——無顯示環境,GL 路徑跳過\n");
        g_skip++;
    } else {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,
                       GLFW_OPENGL_CORE_PROFILE);
        GLFWwindow* win =
            glfwCreateWindow(64, 64, "prim", nullptr, nullptr);
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

                RenderTarget rt;
                Check(rt.Create(64, 64), "LDR 目標建立");

                // batcher：畫紅色對角線 → 讀回應有紅像素
                PrimitiveBatcher pb;
                Check(pb.Create() && pb.IsValid(),
                      "PrimitiveBatcher 建立");
                const Vector4 red(1, 0, 0, 1);
                pb.Line(Vector3(-0.8f, -0.8f, 0),
                        Vector3(0.8f, 0.8f, 0), red);
                pb.Axes(0.5f);
                rt.Bind();
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT |
                        GL_DEPTH_BUFFER_BIT);
                // viewProj=I：NDC 座標直接畫
                pb.Flush(Matrix4::Identity(), false);
                rt.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "batcher Flush 無 GL 錯誤");
                std::vector<unsigned char> px;
                rt.Bind();
                Check(rt.ReadColor(px), "batcher 讀回");
                rt.Unbind();
                int redPx = 0, greenPx = 0;
                for (size_t i = 0; i + 2 < px.size(); i += 4) {
                    if (px[i] > 200 && px[i + 1] < 80) ++redPx;
                    if (px[i + 1] > 200 && px[i] < 80)
                        ++greenPx;
                }
                Check(redPx > 10,
                      "對角線光柵化（紅像素 >10）");
                Check(greenPx > 0,
                      "Axes Y 軸（綠像素存在）");
                printf("  [info] 紅 %d / 綠 %d\n", redPx,
                       greenPx);

                // cube mesh：經簡易 shader 光柵化——
                // 填滿中心區域（正交視角看 +Z 面）
                Mesh cube = PrimitiveMeshes::MakeCube(1.6f);
                Shader flat;
                const char* flatVS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 viewProj;
void main()
{
    gl_Position = viewProj * vec4(aPos, 1.0);
}
)";
                const char* flatFS = R"(
#version 330 core
out vec4 FragColor;
void main()
{
    FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";
                Check(flat.LoadFromSource(flatVS, flatFS),
                      "平面 shader 編譯");
                rt.Bind();
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT |
                        GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                flat.Bind();
                flat.SetUniformMat4("viewProj",
                                    Matrix4::Identity());
                cube.Draw();
                flat.Unbind();
                rt.Unbind();
                Check(glGetError() == GL_NO_ERROR,
                      "cube 繪製無 GL 錯誤");
                std::vector<unsigned char> px2;
                rt.Bind();
                rt.ReadColor(px2);
                rt.Unbind();
                int fill = 0;
                for (size_t i = 0; i + 2 < px2.size(); i += 4) {
                    if (px2[i + 1] > 200) ++fill;
                }
                // cube 邊長 1.6（NDC 80%）→ ~40% 畫面是綠
                Check(fill > 1000,
                      "cube 實際光柵化（綠像素 >1000）");
                printf("  [info] cube 綠像素 %d/4096\n", fill);
            }
            DestroyGLFWWindow(win);
        }
        glfwTerminate();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n  SKIP: %d\n",
           g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
