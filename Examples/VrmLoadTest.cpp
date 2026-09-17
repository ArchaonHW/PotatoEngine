// VrmLoadTest - glTF/GLB/VRM 載入管線測試（tinygltf 後端）
// 1) 記憶體內手工組裝一個最小 GLB 三角形,驗證解析不崩且資料正確
// 2) assets/avatars/ 有真實 .vrm 時,驗證完整載入（缺檔則略過,不影響 CI）

#include "Rendering/ModelLoader.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

// ---- 手工組裝最小 GLB：一個三角形，POSITION + indices ----
static std::string BuildMinimalGlb() {
    // JSON chunk（buffer 0 = positions 36B + indices 6B,pad 到 4 對齊）
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":1,\"mode\":4}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"buffers\":[{\"byteLength\":44}]}";
    // BIN chunk：3 個 float3 頂點 + 3 個 uint16 index + 2B padding
    const float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    const unsigned short idx[3] = {0, 1, 2};

    auto pad4 = [](size_t n) { return (4 - (n % 4)) % 4; };
    std::string jsonChunk(json);
    size_t jsonPad = pad4(jsonChunk.size());
    jsonChunk.append(jsonPad, ' ');
    std::vector<unsigned char> bin;
    bin.insert(bin.end(), (const unsigned char*)pos,
               (const unsigned char*)pos + 36);
    bin.insert(bin.end(), (const unsigned char*)idx,
               (const unsigned char*)idx + 6);
    bin.insert(bin.end(), pad4(bin.size()), 0);

    const uint32 totalLen =
        12 + 8 + (uint32)jsonChunk.size() + 8 + (uint32)bin.size();
    std::string glb;
    glb.reserve(totalLen);
    glb.append("glTF", 4);
    uint32 ver = 2, len = totalLen;
    glb.append((const char*)&ver, 4).append((const char*)&len, 4);
    uint32 jlen = (uint32)jsonChunk.size(), jtype = 0x4E4F534A; // "JSON"
    glb.append((const char*)&jlen, 4).append((const char*)&jtype, 4);
    glb.append(jsonChunk);
    uint32 blen = (uint32)bin.size(), btype = 0x004E4942; // "BIN\0"
    glb.append((const char*)&blen, 4).append((const char*)&btype, 4);
    glb.append((const char*)bin.data(), bin.size());
    return glb;
}

// ---- 帶動畫的 GLB：node 0 的 translation 0→1 秒從 y=0 升到 y=1 ----
static std::string BuildAnimatedGlb() {
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},"
        "\"indices\":1,\"mode\":4}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":8},"
        "{\"buffer\":0,\"byteOffset\":52,\"byteLength\":24}],"
        "\"buffers\":[{\"byteLength\":76}],"
        "\"animations\":[{\"samplers\":[{\"input\":2,\"output\":3,"
        "\"interpolation\":\"LINEAR\"}],"
        "\"channels\":[{\"sampler\":0,"
        "\"target\":{\"node\":0,\"path\":\"translation\"}}]}]}";
    const float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    const unsigned short idx[3] = {0, 1, 2};
    const float times[2] = {0.0f, 1.0f};
    const float values[6] = {0,0,0, 0,1,0};

    auto pad4 = [](size_t n) { return (4 - (n % 4)) % 4; };
    std::string jsonChunk(json);
    jsonChunk.append(pad4(jsonChunk.size()), ' ');
    std::vector<unsigned char> bin;
    bin.insert(bin.end(), (const unsigned char*)pos,
               (const unsigned char*)pos + 36);
    bin.insert(bin.end(), (const unsigned char*)idx,
               (const unsigned char*)idx + 6);
    bin.insert(bin.end(), pad4(bin.size()), 0);
    bin.insert(bin.end(), (const unsigned char*)times,
               (const unsigned char*)times + 8);
    bin.insert(bin.end(), (const unsigned char*)values,
               (const unsigned char*)values + 24);

    const uint32 totalLen =
        12 + 8 + (uint32)jsonChunk.size() + 8 + (uint32)bin.size();
    std::string glb;
    glb.reserve(totalLen);
    glb.append("glTF", 4);
    uint32 ver = 2, len = totalLen;
    glb.append((const char*)&ver, 4).append((const char*)&len, 4);
    uint32 jlen = (uint32)jsonChunk.size(), jtype = 0x4E4F534A;
    glb.append((const char*)&jlen, 4).append((const char*)&jtype, 4);
    glb.append(jsonChunk);
    uint32 blen = (uint32)bin.size(), btype = 0x004E4942;
    glb.append((const char*)&blen, 4).append((const char*)&btype, 4);
    glb.append((const char*)bin.data(), bin.size());
    return glb;
}

static bool FileExists(const char* path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = fopen(path, "rb");
#endif
    if (f) { fclose(f); return true; }
    return false;
}

int main() {
    printf("=== glTF/VRM Loader Tests ===\n\n");

    // ---- 最小 GLB 記憶體解析 ----
    {
        ModelData m;
        std::string glb = BuildMinimalGlb();
        bool ok = GLTFLoader::LoadFromMemory(glb, m);
        Check(ok && m.meshes.size() == 1, "minimal GLB parses");
        if (ok && !m.meshes.empty()) {
            Check(m.meshes[0].vertices.size() == 3 &&
                  m.meshes[0].indices.size() == 3,
                  "vertex/index counts");
            Check(std::fabs(m.meshes[0].vertices[1].position.x - 1.0f) < 1e-6f,
                  "POSITION accessor decoded");
        }
    }

    // ---- 壞資料不得崩潰 ----
    {
        ModelData m;
        Check(!GLTFLoader::LoadFromMemory("not a gltf", m),
              "garbage input rejected");
        Check(!GLTFLoader::LoadFromMemory("glTF", m),
              "truncated GLB rejected");
    }

    // ---- 動畫求值：node translation 隨時間變化 ----
    {
        ModelData m;
        std::string glb = BuildAnimatedGlb();
        bool ok = GLTFLoader::LoadFromMemory(glb, m);
        Check(ok && m.animations.size() == 1 &&
                  m.animations[0].channels.size() == 1,
              "animated GLB parses (1 clip, 1 channel)");
        if (ok) {
            // Model 層級：無 GL context 下 LoadFromData 也應可用（延遲上傳）
            Model model;
            Check(model.LoadFromData(m), "Model::LoadFromData headless");
            Check(model.GetAnimationCount() == 1, "animation count");
            Check(model.PlayAnimation(0), "PlayAnimation(0)");
            model.UpdateAnimation(0.5f); // t=0.5 → y 應插值到 ~0.5
            Matrix4 w = model.GetNodeWorldTransform(0);
            Check(std::fabs(w.m[13] - 0.5f) < 1e-4f,
                  "animated translation sampled (y≈0.5)");
            model.StopAnimation();
            w = model.GetNodeWorldTransform(0);
            Check(std::fabs(w.m[13]) < 1e-6f,
                  "StopAnimation returns to bind pose");
        }
    }

    // ---- 真實 VRM（assets/avatars/ 存在才跑）----
    const char* vrmFiles[] = {
        "assets/avatars/AvatarSample.vrm",
        "assets/avatars/xiao-renou.vrm",
        "assets/avatars/blonde-character.vrm",
    };
    for (const char* path : vrmFiles) {
        // ctest working dir 是 bin/：MinGW 為 build-mingw/bin（往上兩層），
        // MSVC 為 build/bin/Release（往上三層）
        std::string resolved = path;
        if (!FileExists(resolved.c_str())) resolved = std::string("../../") + path;
        if (!FileExists(resolved.c_str())) resolved = std::string("../../../") + path;
        if (!FileExists(resolved.c_str())) {
            printf("  [SKIP] %s (asset not present)\n", path);
            continue;
        }
        ModelData m;
        bool ok = GLTFLoader::LoadFromFile(resolved, m);
        Check(ok, path);
        if (!ok) continue;
        Check(!m.meshes.empty(), "  has meshes");
        Check(!m.materials.empty(), "  has materials");
        Check(m.hasVrmExtension, "  VRM extension detected");
        // VRM avatar 必有 skin + joints
        Check(!m.skins.empty(), "  has skin data");
        bool anyJoints = false;
        for (const auto& mesh : m.meshes)
            if (!mesh.joints.empty()) anyJoints = true;
        Check(anyJoints, "  has JOINTS_0/WEIGHTS_0");
        bool anyTexture = false;
        for (const auto& kv : m.materials)
            if (!kv.second.embeddedDiffuse.empty()) anyTexture = true;
        Check(anyTexture, "  has embedded textures");

        // Model 層級蒙皮檢查（headless：GPU 上傳已延遲）
        Model model;
        Check(model.LoadFromData(m), "  Model::LoadFromData");
        Check(model.HasSkinning(), "  Model::HasSkinning");
        Check(model.GetJointCount(0) > 0, "  joint count > 0");
        model.UpdateAnimation(0.016f);
        const auto& palette = model.GetJointPalette(0);
        Check(palette.size() == (size_t)model.GetJointCount(0),
              "  joint palette sized correctly");
        bool finite = true;
        for (const auto& jm : palette) {
            for (int e = 0; e < 16; ++e) {
                if (!std::isfinite(jm.m[e])) finite = false;
            }
        }
        Check(finite, "  joint palette finite");
    }

    printf("\n=== %s (%d failures) ===\n",
           g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
