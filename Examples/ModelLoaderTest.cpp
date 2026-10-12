// ModelLoaderTest - OBJ 解析器測試（四種面格式 / 負索引 / 越界索引 / quad 三角化 / tangent）

#include "Rendering/ModelLoader.h"
#include <cstdio>
#include <cmath>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

static bool Load(const char* obj, ModelData& out) {
    return OBJLoader::LoadFromMemory(obj, out);
}

int main() {
    printf("=== OBJ Loader Tests ===\n\n");

    // ---- 四種面格式 ----
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "vt 0 0\n" "vt 1 0\n" "vt 0 1\n"
            "vn 0 0 1\n"
            "f 1 2 3\n";
        Check(Load(obj, m) && m.meshes.size() == 1 &&
              m.meshes[0].vertices.size() == 3 &&
              m.meshes[0].indices.size() == 3,
              "f v 格式");
    }
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "vt 0 0\n" "vt 1 0\n" "vt 0 1\n"
            "f 1/1 2/2 3/3\n";
        Check(Load(obj, m) && m.meshes[0].vertices.size() == 3 &&
              std::fabs(m.meshes[0].vertices[1].texCoord.x - 1.0f) < 1e-6f,
              "f v/vt 格式（vt 正確對應）");
    }
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "vt 9 9\n"
            "vn 0 0 1\n"
            "f 1//1 2//1 3//1\n";
        // v//vn 的 vn 不得被誤讀成 vt
        Check(Load(obj, m) && m.meshes[0].vertices.size() == 3 &&
              std::fabs(m.meshes[0].vertices[0].normal.z - 1.0f) < 1e-6f &&
              std::fabs(m.meshes[0].vertices[0].texCoord.x) < 1e-6f,
              "f v//vn 格式（vn 不被誤讀為 vt）");
    }
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "vt 0 0\n" "vt 1 0\n" "vt 0 1\n"
            "vn 0 0 1\n"
            "f 1/1/1 2/2/1 3/3/1\n";
        Check(Load(obj, m) && m.meshes[0].vertices.size() == 3 &&
              std::fabs(m.meshes[0].vertices[0].normal.z - 1.0f) < 1e-6f,
              "f v/vt/vn 格式");
    }

    // ---- 負索引（相對檔尾）----
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "f -3 -2 -1\n";
        Check(Load(obj, m) && m.meshes[0].vertices.size() == 3 &&
              std::fabs(m.meshes[0].vertices[2].position.y - 1.0f) < 1e-6f,
              "負索引解析（-1 = 最後一個頂點）");
    }

    // ---- 越界索引拒絕（不 crash、不產頂點）----
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "f 9999/1/1 2 3\n";  // 9999 越界 → 該頂點跳過
        Check(Load(obj, m) && m.meshes.size() <= 1 &&
              (m.meshes.empty() || m.meshes[0].vertices.size() == 2),
              "越界正索引被跳過（無 OOB read）");
    }
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "f -99 2 3\n";       // 負索引超出範圍 → 跳過
        Check(Load(obj, m) &&
              (m.meshes.empty() || m.meshes[0].vertices.size() == 2),
              "越界負索引被跳過");
    }
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "f -2147483648 2 3\n"; // INT_MIN：舊版 -idx 會 UB
        Check(Load(obj, m) &&
              (m.meshes.empty() || m.meshes[0].vertices.size() == 2),
              "INT_MIN 索引不觸發 overflow");
    }

    // ---- quad 三角化 ----
    {
        ModelData m;
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 1 1 0\n" "v 0 1 0\n"
            "f 1 2 3 4\n";
        Check(Load(obj, m) && m.meshes[0].vertices.size() == 6 &&
              m.meshes[0].indices.size() == 6,
              "quad fan 三角化（4 角 → 2 三角形 / 6 索引）");
    }

    // ---- tangent/bitangent 計算 ----
    {
        ModelData m;
        // XY 平面上的三角形，UV 與世界 XY 對齊 → tangent≈+X, bitangent≈+Y
        const char* obj =
            "v 0 0 0\n" "v 1 0 0\n" "v 0 1 0\n"
            "vt 0 0\n" "vt 1 0\n" "vt 0 1\n"
            "f 1/1 2/2 3/3\n";
        Check(Load(obj, m), "tangent 測試載入");
        const ModelVertex& v = m.meshes[0].vertices[0];
        Check(std::fabs(v.tangent.x - 1.0f) < 1e-3f &&
              std::fabs(v.tangent.y) < 1e-3f,
              "tangent 指向 +X");
        Check(std::fabs(v.bitangent.y - 1.0f) < 1e-3f &&
              std::fabs(v.bitangent.x) < 1e-3f,
              "bitangent 指向 +Y");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
