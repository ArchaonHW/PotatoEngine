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

// ---- 蒙皮 GLB：單一 joint（node 0 自身）、identity IBM ----
// 驗證 skin 解析、joint palette 計算、RotateNodeLocal 與重載不累積
static std::string BuildSkinnedGlb() {
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0,\"skin\":0,\"name\":\"Bone0\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,"
        "\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":1,\"mode\":4}]}],"
        "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":4}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":1,\"type\":\"MAT4\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":56,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":104,\"byteLength\":64}],"
        "\"buffers\":[{\"byteLength\":168}]}";
    const float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    const unsigned short idx[3] = {0, 1, 2};
    const unsigned char joints[12] = {0,0,0,0, 0,0,0,0, 0,0,0,0};
    const float weights[12] = {1,0,0,0, 1,0,0,0, 1,0,0,0};
    const float ibm[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    auto pad4 = [](size_t n) { return (4 - (n % 4)) % 4; };
    std::string jsonChunk(json);
    jsonChunk.append(pad4(jsonChunk.size()), ' ');
    std::vector<unsigned char> bin;
    bin.insert(bin.end(), (const unsigned char*)pos,
               (const unsigned char*)pos + 36);
    bin.insert(bin.end(), (const unsigned char*)idx,
               (const unsigned char*)idx + 6);
    bin.insert(bin.end(), pad4(bin.size()), 0);
    bin.insert(bin.end(), joints, joints + 12);
    bin.insert(bin.end(), (const unsigned char*)weights,
               (const unsigned char*)weights + 48);
    bin.insert(bin.end(), (const unsigned char*)ibm,
               (const unsigned char*)ibm + 64);

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

// ---- VRM 擴充 GLB：蒙皮 + morph target + humanoid/expression/spring/MToon ----
static std::string BuildVrmGlb() {
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"mesh\":0,\"skin\":0,\"name\":\"Hips\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,"
        "\"JOINTS_0\":2,\"WEIGHTS_0\":3},\"indices\":1,\"mode\":4,"
        "\"material\":0,\"targets\":[{\"POSITION\":5}]}]}],"
        "\"materials\":[{\"name\":\"FaceMat\",\"pbrMetallicRoughness\":{}}],"
        "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":4}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":2,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":1,\"type\":\"MAT4\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":44,\"byteLength\":12},"
        "{\"buffer\":0,\"byteOffset\":56,\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":104,\"byteLength\":64},"
        "{\"buffer\":0,\"byteOffset\":168,\"byteLength\":36}],"
        "\"buffers\":[{\"byteLength\":204}],"
        "\"extensions\":{\"VRM\":{"
        "\"humanoid\":{\"humanBones\":[{\"bone\":\"hips\",\"node\":0,"
        "\"useDefaultValues\":true}]},"
        "\"blendShapeMaster\":{\"blendShapeGroups\":[{\"name\":\"Blink\","
        "\"presetName\":\"blink\",\"isBinary\":false,"
        "\"binds\":[{\"mesh\":0,\"index\":0,\"weight\":100}]}]},"
        "\"secondaryAnimation\":{"
        "\"colliderGroups\":[{\"node\":0,\"colliders\":[{\"offset\":"
        "{\"x\":0,\"y\":0,\"z\":0},\"radius\":0.1}]}],"
        "\"boneGroups\":[{\"stiffiness\":1.0,\"gravityPower\":0.1,"
        "\"gravityDir\":{\"x\":1,\"y\":0,\"z\":0},\"dragForce\":0.4,"
        "\"hitRadius\":0.02,\"bones\":[0],\"colliderGroups\":[0]}]},"
        "\"materialProperties\":[{\"name\":\"FaceMat\","
        "\"shader\":\"VRM/MToon\",\"floatProperties\":{\"_ShadeToony\":0.9},"
        "\"vectorProperties\":{\"_ShadeColor\":[0.5,0.4,0.6,1]}}]}}}";
    const float pos[9] = {0,0,0, 1,0,0, 0,1,0};
    const unsigned short idx[3] = {0, 1, 2};
    const unsigned char joints[12] = {0,0,0,0, 0,0,0,0, 0,0,0,0};
    const float weights[12] = {1,0,0,0, 1,0,0,0, 1,0,0,0};
    const float ibm[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float morph[9] = {0,0.5f,0, 0,0.5f,0, 0,0.5f,0};

    auto pad4 = [](size_t n) { return (4 - (n % 4)) % 4; };
    std::string jsonChunk(json);
    jsonChunk.append(pad4(jsonChunk.size()), ' ');
    std::vector<unsigned char> bin;
    bin.insert(bin.end(), (const unsigned char*)pos,
               (const unsigned char*)pos + 36);
    bin.insert(bin.end(), (const unsigned char*)idx,
               (const unsigned char*)idx + 6);
    bin.insert(bin.end(), pad4(bin.size()), 0);
    bin.insert(bin.end(), joints, joints + 12);
    bin.insert(bin.end(), (const unsigned char*)weights,
               (const unsigned char*)weights + 48);
    bin.insert(bin.end(), (const unsigned char*)ibm,
               (const unsigned char*)ibm + 64);
    bin.insert(bin.end(), (const unsigned char*)morph,
               (const unsigned char*)morph + 36);

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
            if (m.meshes[0].vertices.size() >= 3) {
                Check(std::fabs(m.meshes[0].vertices[1].position.x - 1.0f) <
                          1e-6f,
                      "POSITION accessor decoded");
            }
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

    // ---- 蒙皮：skin 解析 + joint palette + 姿勢編輯 + 重載不累積 ----
    {
        ModelData m;
        std::string glb = BuildSkinnedGlb();
        bool ok = GLTFLoader::LoadFromMemory(glb, m);
        Check(ok && m.skins.size() == 1 && m.skins[0].joints.size() == 1 &&
                  !m.meshes.empty() && !m.meshes[0].joints.empty(),
              "skinned GLB parses (skin + JOINTS_0/WEIGHTS_0)");
        if (ok && !m.skins.empty() &&
            !m.skins[0].inverseBindMatrices.empty()) {
            Check(std::fabs(m.skins[0].inverseBindMatrices[0].m[5] - 1.0f) <
                      1e-6f,
                  "inverse bind matrix decoded");

            Model model;
            Check(model.LoadFromData(m), "skinned Model::LoadFromData");
            Check(model.HasSkinning(), "HasSkinning");
            Check(model.GetJointCount(0) == 1, "joint count == 1");
            const auto& palette = model.GetJointPalette(0);
            Check(palette.size() == 1 &&
                      std::fabs(palette[0].m[5] - 1.0f) < 1e-6f,
                  "bind pose palette ≈ identity");

            // 姿勢編輯：繞 X 軸轉 90° → palette 不再是 identity
            Check(model.FindNodeIndexByName("bone") == 0,
                  "FindNodeIndexByName hit");
            Check(model.FindNodeIndexByName("") == -1 &&
                      model.FindNodeIndexByName("zzz") == -1,
                  "FindNodeIndexByName miss");
            const float h = 0.70710678f; // sin/cos(45°)
            Check(model.RotateNodeLocal(0, Quaternion(h, 0, 0, h)),
                  "RotateNodeLocal");
            Check(std::fabs(model.GetJointPalette(0)[0].m[5]) < 0.5f,
                  "palette reflects rotated pose");

            // 重載同一資料不累積
            model.LoadFromData(m);
            Check(model.GetJointCount(0) == 1 && model.HasSkinning(),
                  "LoadFromData reload does not accumulate");
        }
    }

    // ---- VRM 擴充：humanoid / expression / spring bone / MToon ----
    {
        ModelData m;
        std::string glb = BuildVrmGlb();
        bool ok = GLTFLoader::LoadFromMemory(glb, m);
        Check(ok && m.hasVrmExtension, "VRM GLB parses + extension detected");
        if (ok) {
            Check(m.vrm.humanoidBones.count("hips") == 1 &&
                      m.vrm.humanoidBones["hips"] == 0,
                  "humanoid bone map (hips→node0)");
            Check(m.vrm.expressions.size() == 1 &&
                      m.vrm.expressions[0].binds.size() == 1,
                  "blendShape expression parsed");
            Check(!m.meshes.empty() &&
                      m.meshes[0].morphTargets.size() == 1 &&
                      std::fabs(m.meshes[0].morphTargets[0]
                                    .positionDeltas[1].y - 0.5f) < 1e-6f,
                  "morph target delta decoded");
            Check(m.vrm.boneGroups.size() == 1 &&
                      m.vrm.colliderGroups.size() == 1,
                  "spring bone + collider groups parsed");
            auto mit = m.materials.find("FaceMat");
            Check(mit != m.materials.end() && mit->second.mtoon &&
                      std::fabs(mit->second.shadeToony - 0.9f) < 1e-6f,
                  "MToon material parsed");

            Model model;
            Check(model.LoadFromData(m) && model.IsVrm(), "Model IsVrm");
            Check(model.GetHumanoidBone("hips") == 0 &&
                      model.GetHumanoidBone("head") == -1,
                  "GetHumanoidBone");
            Check(model.GetExpressionCount() == 1 &&
                      model.GetExpressionName(0) == "Blink",
                  "expression name");
            Check(model.SetExpression("blink", 1.0f) &&
                      model.SetExpression("Blink", 0.5f),
                  "SetExpression by preset/name");
            Check(!model.SetExpression("nonexistent", 1.0f),
                  "SetExpression unknown name fails");
            model.ClearExpressions();

            // spring bone：重力沿 +x → node 0 應被轉動（palette 偏離 identity）
            model.UpdateAnimation(0.016f);
            const auto& p = model.GetJointPalette(0);
            Check(p.size() == 1 && std::fabs(p[0].m[1]) > 1e-3f,
                  "spring bone rotated the node");
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
