#include "ModelLoader.h"
#include "Logging/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <numeric>
// 第三方標頭必須在 namespace Potato 之外 include——
// 放在 Potato 內會把 std 巢狀成 Potato::std（GCC 直接編譯失敗）
#include "tiny_gltf.h"

namespace Potato {

// ============================================================================
// OBJLoader 實現
// ============================================================================

bool OBJLoader::LoadFromFile(const std::string& path, ModelData& modelData) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open OBJ file: " + path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return LoadFromMemory(content, modelData);
}

bool OBJLoader::LoadFromMemory(const std::string& content, ModelData& modelData) {
    return ParseOBJ(content, modelData);
}

bool OBJLoader::ParseOBJ(const std::string& content, ModelData& modelData) {
    std::istringstream stream(content);
    std::string line;
    
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texCoords;
    
    MeshData currentMesh;
    currentMesh.materialName = "default";
    
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream lineStream(line);
        std::string type;
        lineStream >> type;
        
        if (type == "v") {
            Vector3 pos;
            lineStream >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        } else if (type == "vn") {
            Vector3 normal;
            lineStream >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (type == "vt") {
            Vector2 tex;
            lineStream >> tex.x >> tex.y;
            texCoords.push_back(tex);
        } else if (type == "f") {
            // 解析面 — 支援 v、v/vt、v//vn、v/vt/vn 四種格式
            // 先收集本面全部頂點，再 fan-triangulate（quad/ngon 才不會產生破面）
            std::vector<ModelVertex> faceVerts;
            std::string vertexStr;
            while (lineStream >> vertexStr) {
                // 依 '/' 位置切分,空欄位保留(避免 v//vn 的 vn 被誤讀成 vt)
                int indices[3] = {0, 0, 0};
                int field = 0;
                std::string token;
                for (char ch : vertexStr) {
                    if (ch == '/') {
                        if (field < 3 && !token.empty()) {
                            indices[field] = std::atoi(token.c_str());
                        }
                        token.clear();
                        ++field;
                        if (field > 2) break;
                    } else {
                        token += ch;
                    }
                }
                if (field < 3 && !token.empty()) {
                    indices[field] = std::atoi(token.c_str());
                }

                // OBJ 支援負索引(相對於檔尾);轉為絕對索引,0 = 缺欄位
                // 注意上限檢查：f 9999/1/1 不得越界；INT_MIN 取負前先轉型避免 UB
                auto resolve = [](int idx, size_t count) -> size_t {
                    constexpr size_t kInvalid = static_cast<size_t>(-1);
                    if (idx > 0) {
                        return (static_cast<size_t>(idx) <= count)
                            ? static_cast<size_t>(idx) - 1 : kInvalid;
                    }
                    if (idx < 0) {
                        auto absIdx = static_cast<size_t>(
                            -(static_cast<long long>(idx)));
                        return (absIdx <= count) ? count - absIdx : kInvalid;
                    }
                    return kInvalid;
                };

                size_t vIdx = resolve(indices[0], positions.size());
                size_t vtIdx = resolve(indices[1], texCoords.size());
                size_t vnIdx = resolve(indices[2], normals.size());

                if (vIdx == static_cast<size_t>(-1)) continue; // 頂點索引無效,跳過

                ModelVertex vertex;
                vertex.position = positions[vIdx];
                if (vtIdx != static_cast<size_t>(-1)) {
                    vertex.texCoord = texCoords[vtIdx];
                }
                if (vnIdx != static_cast<size_t>(-1)) {
                    vertex.normal = normals[vnIdx];
                }
                faceVerts.push_back(vertex);
            }

            // Fan triangulation：(v0, vi, vi+1) — 三角形面原樣輸出，quad/ngon 切成多個三角形
            const uint32 base = static_cast<uint32>(currentMesh.vertices.size());
            for (size_t i = 1; i + 1 < faceVerts.size(); ++i) {
                currentMesh.vertices.push_back(faceVerts[0]);
                currentMesh.vertices.push_back(faceVerts[i]);
                currentMesh.vertices.push_back(faceVerts[i + 1]);
                currentMesh.indices.push_back(base + static_cast<uint32>((i - 1) * 3));
                currentMesh.indices.push_back(base + static_cast<uint32>((i - 1) * 3 + 1));
                currentMesh.indices.push_back(base + static_cast<uint32>((i - 1) * 3 + 2));
            }
        } else if (type == "usemtl") {
            // 開始新網格
            if (!currentMesh.vertices.empty()) {
                modelData.meshes.push_back(currentMesh);
                currentMesh = MeshData();
            }
            lineStream >> currentMesh.materialName;
        } else if (type == "mtllib") {
            // 加載材質文件
            std::string mtlPath;
            lineStream >> mtlPath;
            // 簡化：跳過材質文件加載
        }
    }
    
    // 添加最後一個網格
    if (!currentMesh.vertices.empty()) {
        modelData.meshes.push_back(currentMesh);
    }
    
    OptimizeMeshes(modelData);
    
    LOG_INFO("Loaded OBJ model with " + std::to_string(modelData.meshes.size()) + " meshes");
    return true;
}

bool OBJLoader::ParseMTL(const std::string& content, std::unordered_map<std::string, MaterialData>& materials) {
    // 簡化實現：跳過材質解析
    return true;
}

void OBJLoader::OptimizeMeshes(ModelData& modelData) {
    // 計算 tangent/bitangent：逐三角形累積再正規化；
    // 無 texcoord 的面維持預設值（法線貼圖本來就用不到它們）
    for (auto& mesh : modelData.meshes) {
        std::vector<Vector3> tanAcc(mesh.vertices.size(), Vector3::Zero());
        std::vector<Vector3> bitAcc(mesh.vertices.size(), Vector3::Zero());

        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint32 i0 = mesh.indices[i];
            const uint32 i1 = mesh.indices[i + 1];
            const uint32 i2 = mesh.indices[i + 2];
            ModelVertex& v0 = mesh.vertices[i0];
            ModelVertex& v1 = mesh.vertices[i1];
            ModelVertex& v2 = mesh.vertices[i2];

            Vector3 e1 = v1.position - v0.position;
            Vector3 e2 = v2.position - v0.position;
            float du1 = v1.texCoord.x - v0.texCoord.x;
            float dv1 = v1.texCoord.y - v0.texCoord.y;
            float du2 = v2.texCoord.x - v0.texCoord.x;
            float dv2 = v2.texCoord.y - v0.texCoord.y;

            float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) < 1e-8f) continue; // UV 退化，無法定義切線空間
            float f = 1.0f / det;

            Vector3 tangent(
                f * (dv2 * e1.x - dv1 * e2.x),
                f * (dv2 * e1.y - dv1 * e2.y),
                f * (dv2 * e1.z - dv1 * e2.z));
            Vector3 bitangent(
                f * (-du2 * e1.x + du1 * e2.x),
                f * (-du2 * e1.y + du1 * e2.y),
                f * (-du2 * e1.z + du1 * e2.z));

            tanAcc[i0] = tanAcc[i0] + tangent;
            tanAcc[i1] = tanAcc[i1] + tangent;
            tanAcc[i2] = tanAcc[i2] + tangent;
            bitAcc[i0] = bitAcc[i0] + bitangent;
            bitAcc[i1] = bitAcc[i1] + bitangent;
            bitAcc[i2] = bitAcc[i2] + bitangent;
        }

        for (size_t i = 0; i < mesh.vertices.size(); ++i) {
            ModelVertex& v = mesh.vertices[i];
            Vector3 t = tanAcc[i];
            Vector3 b = bitAcc[i];
            float tl = t.Length();
            float bl = b.Length();
            if (tl > 1e-8f) v.tangent = t * (1.0f / tl);
            if (bl > 1e-8f) v.bitangent = b * (1.0f / bl);
        }
    }
}

// ============================================================================
// GLTFLoader 實現（tinygltf；.vrm 本體即 GLB，走同一路徑）
// ============================================================================

namespace {

// 讀取 accessor 的原始指標與元素資訊；失敗回傳 nullptr
const unsigned char* GltfAccessorData(const tinygltf::Model& g, int accessorIdx,
                                      size_t& count, int& numComp,
                                      int& compType, size_t& stride) {
    if (accessorIdx < 0 || accessorIdx >= (int)g.accessors.size()) return nullptr;
    const tinygltf::Accessor& acc = g.accessors[accessorIdx];
    if (acc.bufferView < 0 || acc.bufferView >= (int)g.bufferViews.size()) return nullptr;
    const tinygltf::BufferView& bv = g.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)g.buffers.size()) return nullptr;
    const tinygltf::Buffer& buf = g.buffers[bv.buffer];

    count = acc.count;
    numComp = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(acc.type));
    compType = acc.componentType;
    const size_t elemSize =
        static_cast<size_t>(numComp) * tinygltf::GetComponentSizeInBytes(
                                           static_cast<uint32_t>(compType));
    stride = bv.byteStride ? bv.byteStride : elemSize;

    const size_t offset = bv.byteOffset + acc.byteOffset;
    if (count > 0 && offset + (count - 1) * stride + elemSize > buf.data.size()) {
        return nullptr; // 越界
    }
    return buf.data.data() + offset;
}

// 把單一 component 轉成 float（支援 float / 正規化整數 / 非正規化整數）
float GltfComponentToFloat(const unsigned char* p, int compType, bool normalized) {
    switch (compType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        float f; memcpy(&f, p, 4); return f;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        uint8 v; memcpy(&v, p, 1);
        return normalized ? v / 255.0f : (float)v;
    }
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
        int8 v; memcpy(&v, p, 1);
        return normalized ? std::max(v / 127.0f, -1.0f) : (float)v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        uint16 v; memcpy(&v, p, 2);
        return normalized ? v / 65535.0f : (float)v;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
        int16 v; memcpy(&v, p, 2);
        return normalized ? std::max(v / 32767.0f, -1.0f) : (float)v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        uint32 v; memcpy(&v, p, 4); return (float)v;
    }
    default: return 0.0f;
    }
}

Vector3 GltfReadVec3(const tinygltf::Model& g, int accessorIdx, size_t i) {
    size_t count; int nc, ct; size_t stride;
    const unsigned char* base = GltfAccessorData(g, accessorIdx, count, nc, ct, stride);
    Vector3 out = Vector3::Zero();
    if (!base || i >= count || nc < 3) return out;
    const tinygltf::Accessor& acc = g.accessors[accessorIdx];
    const unsigned char* p = base + i * stride;
    const int cs = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(ct));
    out.x = GltfComponentToFloat(p, ct, acc.normalized);
    out.y = GltfComponentToFloat(p + cs, ct, acc.normalized);
    out.z = GltfComponentToFloat(p + 2 * cs, ct, acc.normalized);
    return out;
}

Vector4 GltfReadVec4(const tinygltf::Model& g, int accessorIdx, size_t i) {
    size_t count; int nc, ct; size_t stride;
    const unsigned char* base = GltfAccessorData(g, accessorIdx, count, nc, ct, stride);
    Vector4 out(0, 0, 0, 0);
    if (!base || i >= count || nc < 4) return out;
    const tinygltf::Accessor& acc = g.accessors[accessorIdx];
    const unsigned char* p = base + i * stride;
    const int cs = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(ct));
    out.x = GltfComponentToFloat(p, ct, acc.normalized);
    out.y = GltfComponentToFloat(p + cs, ct, acc.normalized);
    out.z = GltfComponentToFloat(p + 2 * cs, ct, acc.normalized);
    out.w = GltfComponentToFloat(p + 3 * cs, ct, acc.normalized);
    return out;
}

// 節點 local transform：glTF 要嘛給 16 欄 matrix（column-major，與 Matrix4 同），
// 要嘛給 TRS 分量
Matrix4 GltfNodeLocalMatrix(const tinygltf::Node& node, ModelData::NodeData& out) {
    if (node.matrix.size() == 16) {
        float m[16];
        for (int i = 0; i < 16; ++i) m[i] = (float)node.matrix[i];
        out.hasMatrix = true;
        out.matrix = Matrix4(m);
        return out.matrix;
    }
    if (node.translation.size() == 3) {
        out.translation = Vector3((float)node.translation[0],
                                  (float)node.translation[1],
                                  (float)node.translation[2]);
    }
    if (node.rotation.size() == 4) {
        out.rotation = Quaternion((float)node.rotation[0], (float)node.rotation[1],
                                  (float)node.rotation[2], (float)node.rotation[3]);
    }
    if (node.scale.size() == 3) {
        out.scale = Vector3((float)node.scale[0], (float)node.scale[1],
                            (float)node.scale[2]);
    }
    return Matrix4::Translation(out.translation) * out.rotation.ToMatrix() *
           Matrix4::Scale(out.scale);
}

void GltfConvertPrimitive(const tinygltf::Model& g,
                          const tinygltf::Primitive& prim,
                          const Matrix4& world, bool skinned,
                          int skinIndex, ModelData& modelData) {
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return; // 無位置資料的面片直接略過

    MeshData mesh;
    mesh.skinIndex = skinIndex;
    const int posAccIdx = posIt->second;
    const size_t vertCount = g.accessors[posAccIdx].count;
    mesh.vertices.resize(vertCount);

    const bool bake = !skinned; // glTF 規範：skinned mesh 忽略節點 transform
    const Matrix4 normalM = world.Inverse().Transposed();

    for (size_t i = 0; i < vertCount; ++i) {
        ModelVertex& v = mesh.vertices[i];
        v.position = bake ? world.TransformPoint(GltfReadVec3(g, posAccIdx, i))
                          : GltfReadVec3(g, posAccIdx, i);
    }

    auto attr = prim.attributes.find("NORMAL");
    if (attr != prim.attributes.end()) {
        for (size_t i = 0; i < vertCount; ++i) {
            Vector3 n = GltfReadVec3(g, attr->second, i);
            mesh.vertices[i].normal = bake ? normalM.TransformVector(n) : n;
        }
    }
    attr = prim.attributes.find("TEXCOORD_0");
    if (attr != prim.attributes.end()) {
        size_t count; int nc, ct; size_t stride;
        if (GltfAccessorData(g, attr->second, count, nc, ct, stride)) {
            const tinygltf::Accessor& acc = g.accessors[attr->second];
            const unsigned char* base =
                g.buffers[g.bufferViews[acc.bufferView].buffer].data.data() +
                g.bufferViews[acc.bufferView].byteOffset + acc.byteOffset;
            const int cs = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(ct));
            for (size_t i = 0; i < vertCount && i < count; ++i) {
                const unsigned char* p = base + i * stride;
                // glTF UV 原點在左上，OpenGL 在左下 → 翻轉 v
                mesh.vertices[i].texCoord.x = GltfComponentToFloat(p, ct, acc.normalized);
                mesh.vertices[i].texCoord.y =
                    1.0f - GltfComponentToFloat(p + cs, ct, acc.normalized);
            }
        }
    }
    attr = prim.attributes.find("TANGENT");
    if (attr != prim.attributes.end()) {
        for (size_t i = 0; i < vertCount; ++i) {
            Vector4 t = GltfReadVec4(g, attr->second, i);
            Vector3 t3(t.x, t.y, t.z);
            mesh.vertices[i].tangent = bake ? normalM.TransformVector(t3) : t3;
        }
    }

    // 蒙皮屬性（Phase 2 使用；此處先完整解析進資料結構）
    auto jit = prim.attributes.find("JOINTS_0");
    auto wit = prim.attributes.find("WEIGHTS_0");
    if (jit != prim.attributes.end() && wit != prim.attributes.end()) {
        mesh.joints.resize(vertCount);
        mesh.weights.resize(vertCount);
        for (size_t i = 0; i < vertCount; ++i) {
            Vector4 jf = GltfReadVec4(g, jit->second, i);
            mesh.joints[i] = {(uint16)jf.x, (uint16)jf.y, (uint16)jf.z, (uint16)jf.w};
            mesh.weights[i] = GltfReadVec4(g, wit->second, i);
        }
    }

    // indices（無索引面片採順序索引）
    if (prim.indices >= 0) {
        size_t count; int nc, ct; size_t stride;
        const unsigned char* base =
            GltfAccessorData(g, prim.indices, count, nc, ct, stride);
        if (base) {
            mesh.indices.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                mesh.indices.push_back(
                    (uint32)GltfComponentToFloat(base + i * stride, ct, false));
            }
        }
    } else {
        mesh.indices.reserve(vertCount);
        for (size_t i = 0; i < vertCount; ++i) mesh.indices.push_back((uint32)i);
    }

    // 材質
    std::string matName;
    if (prim.material >= 0 && prim.material < (int)g.materials.size()) {
        const tinygltf::Material& gm = g.materials[prim.material];
        matName = gm.name.empty()
                      ? "gltf_mat_" + std::to_string(prim.material)
                      : gm.name;
        if (modelData.materials.find(matName) == modelData.materials.end()) {
            MaterialData md;
            md.name = matName;
            const auto& pbr = gm.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() == 4) {
                md.diffuse = Vector3((float)pbr.baseColorFactor[0],
                                     (float)pbr.baseColorFactor[1],
                                     (float)pbr.baseColorFactor[2]);
            }
            // 內嵌 diffuse 貼圖：texture → image（tinygltf 已用 stb 解碼）
            int texIdx = pbr.baseColorTexture.index;
            if (texIdx >= 0 && texIdx < (int)g.textures.size()) {
                int imgIdx = g.textures[texIdx].source;
                if (imgIdx >= 0 && imgIdx < (int)g.images.size() &&
                    !g.images[imgIdx].image.empty()) {
                    const tinygltf::Image& img = g.images[imgIdx];
                    md.embeddedDiffuse = img.image;
                    md.embeddedWidth = img.width;
                    md.embeddedHeight = img.height;
                    md.embeddedChannels = img.component;
                    md.diffuseTexture = "embedded:" + std::to_string(imgIdx);
                }
            }
            md.alphaMode = gm.alphaMode;
            md.alphaCutoff = (float)gm.alphaCutoff;
            md.doubleSided = gm.doubleSided;
            modelData.materials[matName] = std::move(md);
        }
    } else {
        matName = "default";
    }
    mesh.materialName = matName;
    modelData.meshes.push_back(std::move(mesh));
}

void GltfWalkNode(const tinygltf::Model& g, int nodeIdx, int parentIdx,
                  const Matrix4& parentWorld, ModelData& modelData,
                  std::vector<char>& visited, std::vector<int>& gltfToModel) {
    // 節點環保護：glTF 規格無環，但畸形/惡意檔案可能有——visited 防無窮遞迴
    if (visited[nodeIdx]) return;
    visited[nodeIdx] = 1;
    const tinygltf::Node& node = g.nodes[nodeIdx];
    ModelData::NodeData nd;
    nd.name = node.name;
    nd.parent = parentIdx;
    nd.skin = node.skin;
    Matrix4 world = parentWorld * GltfNodeLocalMatrix(node, nd);

    nd.mesh = (int)modelData.meshes.size();
    if (node.mesh >= 0 && node.mesh < (int)g.meshes.size()) {
        const tinygltf::Mesh& gm = g.meshes[node.mesh];
        for (const auto& prim : gm.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
            // JOINTS_0 與 WEIGHTS_0 必須成對存在才算蒙皮——缺 WEIGHTS_0
            // 時退回靜態烘焙（否則頂點留在 mesh space 又走不到 skinned 路徑）
            const bool skinned =
                prim.attributes.find("JOINTS_0") != prim.attributes.end() &&
                prim.attributes.find("WEIGHTS_0") != prim.attributes.end();
            GltfConvertPrimitive(g, prim, world, skinned, node.skin, modelData);
        }
    }
    nd.meshCount = (int)modelData.meshes.size() - nd.mesh;
    if (nd.meshCount == 0) nd.mesh = -1;
    modelData.nodes.push_back(nd);

    const int selfIdx = (int)modelData.nodes.size() - 1;
    gltfToModel[nodeIdx] = selfIdx;
    for (int child : node.children) {
        if (child >= 0 && child < (int)g.nodes.size()) {
            GltfWalkNode(g, child, selfIdx, world, modelData, visited,
                         gltfToModel);
        }
    }
}

bool GltfConvert(const tinygltf::Model& g, ModelData& modelData,
                 const std::string& name) {
    modelData.name = name;
    modelData.hasVrmExtension = g.extensions.count("VRM") > 0 ||
                                g.extensions.count("VRMC_vrm") > 0;

    // skin/animation 的 node 參照是 glTF 原始索引，
    // modelData.nodes 採走訪序 → 需要 gltfToModel 重映射
    std::vector<char> visited(g.nodes.size(), 0);
    std::vector<int> gltfToModel(g.nodes.size(), -1);
    int sceneIdx = g.defaultScene >= 0 ? g.defaultScene
                                       : (g.scenes.empty() ? -1 : 0);
    if (sceneIdx >= 0) {
        for (int root : g.scenes[sceneIdx].nodes) {
            if (root >= 0 && root < (int)g.nodes.size()) {
                GltfWalkNode(g, root, -1, Matrix4::Identity(), modelData,
                             visited, gltfToModel);
            }
        }
    } else {
        // 無 scene：所有無父節點者視為 root
        std::vector<bool> isChild(g.nodes.size(), false);
        for (const auto& n : g.nodes)
            for (int c : n.children)
                if (c >= 0 && c < (int)g.nodes.size()) isChild[c] = true;
        for (size_t i = 0; i < g.nodes.size(); ++i) {
            if (!isChild[i])
                GltfWalkNode(g, (int)i, -1, Matrix4::Identity(), modelData,
                             visited, gltfToModel);
        }
    }

    // skins：joint node 索引（重映射為 modelData.nodes 索引）+ inverseBindMatrices
    for (const auto& gs : g.skins) {
        ModelData::SkinData sd;
        sd.name = gs.name;
        sd.joints.reserve(gs.joints.size());
        for (int j : gs.joints) {
            sd.joints.push_back(j >= 0 && j < (int)gltfToModel.size()
                                    ? gltfToModel[j] : -1);
        }
        sd.skeletonRoot = (gs.skeleton >= 0 && gs.skeleton < (int)gltfToModel.size())
                              ? gltfToModel[gs.skeleton] : -1;
        if (gs.inverseBindMatrices >= 0) {
            size_t count; int nc, ct; size_t stride;
            const unsigned char* base = GltfAccessorData(
                g, gs.inverseBindMatrices, count, nc, ct, stride);
            if (base && ct == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                sd.inverseBindMatrices.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    Matrix4 m(reinterpret_cast<const float*>(base + i * stride));
                    sd.inverseBindMatrices.push_back(m);
                }
            }
        }
        modelData.skins.push_back(std::move(sd));
    }

    // 動畫：sampler input=時間(float scalar)、output=vec3/vec4
    for (const auto& anim : g.animations) {
        ModelData::AnimationClip clip;
        clip.name = anim.name;
        for (const auto& s : anim.samplers) {
            ModelData::AnimationClip::Sampler smp;
            if (s.interpolation == "STEP") {
                smp.interpolation = ModelData::AnimationClip::Interpolation::Step;
            } else if (s.interpolation == "CUBICSPLINE") {
                smp.interpolation = ModelData::AnimationClip::Interpolation::CubicSpline;
            }
            size_t count; int nc, ct; size_t stride;
            const unsigned char* base =
                GltfAccessorData(g, s.input, count, nc, ct, stride);
            if (base && ct == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                smp.times.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    float t; memcpy(&t, base + i * stride, 4);
                    smp.times.push_back(t);
                }
                if (count > 0) {
                    clip.duration = std::max(clip.duration, smp.times.back());
                }
            }
            if (s.output >= 0 && s.output < (int)g.accessors.size()) {
                const auto& outAcc = g.accessors[s.output];
                size_t n = outAcc.count;
                smp.values.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    int outNc = tinygltf::GetNumComponentsInType(
                        static_cast<uint32_t>(outAcc.type));
                    if (outNc >= 4) {
                        smp.values.push_back(GltfReadVec4(g, s.output, i));
                    } else {
                        Vector3 v3 = GltfReadVec3(g, s.output, i);
                        smp.values.push_back(Vector4(v3.x, v3.y, v3.z, 0.0f));
                    }
                }
            }
            // 畸形 glTF：sampler 時間軸非遞增會讓取樣找到錯誤區間——重排
            // （CUBICSPLINE 的 values 是每 key 3 元組，需整組搬移）
            if (!std::is_sorted(smp.times.begin(), smp.times.end())) {
                LOG_WARNING("glTF animation sampler times unsorted; reordering");
                const size_t mul =
                    smp.interpolation ==
                            ModelData::AnimationClip::Interpolation::CubicSpline
                        ? 3 : 1;
                std::vector<size_t> order(smp.times.size());
                std::iota(order.begin(), order.end(), size_t(0));
                std::stable_sort(order.begin(), order.end(),
                                 [&](size_t a, size_t b) {
                                     return smp.times[a] < smp.times[b];
                                 });
                std::vector<float> t2(smp.times.size());
                std::vector<Vector4> v2;
                if (smp.values.size() >= smp.times.size() * mul) {
                    v2.resize(smp.times.size() * mul);
                }
                for (size_t i = 0; i < order.size(); ++i) {
                    t2[i] = smp.times[order[i]];
                    for (size_t c = 0; c < mul && !v2.empty(); ++c) {
                        v2[i * mul + c] = smp.values[order[i] * mul + c];
                    }
                }
                smp.times.swap(t2);
                if (!v2.empty()) smp.values.swap(v2);
            }
            clip.samplers.push_back(std::move(smp));
        }
        for (const auto& ch : anim.channels) {
            ModelData::AnimationClip::Channel c;
            c.node = (ch.target_node >= 0 &&
                      ch.target_node < (int)gltfToModel.size())
                         ? gltfToModel[ch.target_node] : -1;
            c.sampler = ch.sampler;
            if (ch.target_path == "rotation") {
                c.path = ModelData::AnimationClip::Path::Rotation;
            } else if (ch.target_path == "scale") {
                c.path = ModelData::AnimationClip::Path::Scale;
            } else if (ch.target_path == "weights") {
                c.path = ModelData::AnimationClip::Path::Weights;
            }
            clip.channels.push_back(c);
        }
        modelData.animations.push_back(std::move(clip));
    }

    if (modelData.meshes.empty()) {
        LOG_ERROR("glTF/VRM contains no triangle primitives: " + name);
        return false;
    }
    return true;
}

} // namespace

bool GLTFLoader::LoadFromFile(const std::string& path, ModelData& modelData) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string err, warn;
    const bool ascii = path.size() >= 5 &&
        path.compare(path.size() - 5, 5, ".gltf") == 0;
    const bool ok = ascii
        ? loader.LoadASCIIFromFile(&gltf, &err, &warn, path)
        : loader.LoadBinaryFromFile(&gltf, &err, &warn, path);
    if (!warn.empty()) LOG_WARNING("glTF: " + warn);
    if (!ok) {
        LOG_ERROR("glTF load failed: " + path + " — " + err);
        return false;
    }
    size_t slash = path.find_last_of("/\\");
    return GltfConvert(gltf, modelData,
                       slash == std::string::npos ? path : path.substr(slash + 1));
}

bool GLTFLoader::LoadFromMemory(const std::string& content, ModelData& modelData) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string err, warn;
    // GLB magic "glTF" 開頭 → binary；否則視為 ASCII .gltf JSON
    const bool binary = content.size() >= 4 &&
        std::memcmp(content.data(), "glTF", 4) == 0;
    const bool ok = binary
        ? loader.LoadBinaryFromMemory(
              &gltf, &err, &warn,
              reinterpret_cast<const unsigned char*>(content.data()),
              static_cast<unsigned int>(content.size()))
        : loader.LoadASCIIFromString(&gltf, &err, &warn, content.c_str(),
                                     static_cast<unsigned int>(content.size()),
                                     "");
    if (!warn.empty()) LOG_WARNING("glTF: " + warn);
    if (!ok) {
        LOG_ERROR("glTF parse failed: " + err);
        return false;
    }
    return GltfConvert(gltf, modelData, "memory-model");
}

bool GLTFLoader::ParseGLTF(const std::string& content, ModelData& modelData,
                           const std::string& baseDir) {
    (void)baseDir; // LoadFromMemory 已內嵌 baseDir 處理
    return LoadFromMemory(content, modelData);
}

bool GLTFLoader::ParseGLB(const std::string& content, ModelData& modelData) {
    return LoadFromMemory(content, modelData);
}

// ============================================================================
// Model 實現
// ============================================================================

Model::Model()
    : position(Vector3::Zero())
    , rotation(Quaternion::Identity())
    , scale(Vector3::One())
{
}

Model::~Model() {
}

bool Model::LoadFromFile(const std::string& path) {
    ModelData modelData;
    
    // 根�據擴展名選擇加載器
    if (path.find(".obj") != std::string::npos) {
        if (!OBJLoader::LoadFromFile(path, modelData)) {
            return false;
        }
    } else if (path.find(".gltf") != std::string::npos || path.find(".glb") != std::string::npos ||
               path.find(".vrm") != std::string::npos) {
        if (!GLTFLoader::LoadFromFile(path, modelData)) {
            return false;
        }
    } else {
        LOG_ERROR("Unsupported model format: " + path);
        return false;
    }
    
    return LoadFromData(modelData);
}

bool Model::LoadFromData(const ModelData& modelData) {
    name = modelData.name;

    // 重新載入時清掉舊狀態，避免 mesh/骨架資料累積
    meshes.clear();
    meshMaterialNames.clear();
    skinnedMeshes.clear();
    skinnedMeshMaterialNames.clear();
    textures.clear();
    materialDefs.clear();
    animNodes.clear();
    skinsData.clear();
    animations.clear();
    jointPalettes.clear();
    nodeWorld.clear();
    activeAnimation = -1;
    animationTime = 0.0f;

    // 創建網格：有 JOINTS_0/WEIGHTS_0 的走 SkinnedMesh，其餘走一般 Mesh
    for (const auto& meshData : modelData.meshes) {
        if (!meshData.joints.empty()) {
            auto mesh = MakeUnique<SkinnedMesh>();
            std::vector<SkinnedVertex> vertices;
            vertices.reserve(meshData.vertices.size());
            for (size_t i = 0; i < meshData.vertices.size(); ++i) {
                const ModelVertex& mv = meshData.vertices[i];
                SkinnedVertex v;
                v.position = mv.position;
                v.normal = mv.normal;
                v.texCoord = mv.texCoord;
                v.tangent = mv.tangent;
                v.bitangent = mv.bitangent;
                if (i < meshData.joints.size()) {
                    const auto& j = meshData.joints[i];
                    v.joints = Vector4((float)j[0], (float)j[1],
                                       (float)j[2], (float)j[3]);
                }
                if (i < meshData.weights.size()) {
                    v.weights = meshData.weights[i];
                }
                vertices.push_back(v);
            }
            mesh->SetVertices(vertices);
            mesh->SetIndices(meshData.indices);
            mesh->SetSkinIndex(meshData.skinIndex);
            skinnedMeshMaterialNames.push_back(meshData.materialName);
            skinnedMeshes.push_back(std::move(mesh));
            continue;
        }

        auto mesh = MakeUnique<Mesh>();

        // 轉換頂點數據
        std::vector<Vertex> vertices;
        for (const auto& modelVertex : meshData.vertices) {
            Vertex vertex;
            vertex.position = modelVertex.position;
            vertex.normal = modelVertex.normal;
            vertex.texCoord = modelVertex.texCoord;
            vertex.tangent = modelVertex.tangent;
            vertex.bitangent = modelVertex.bitangent;
            vertices.push_back(vertex);
        }

        mesh->SetVertices(vertices);
        mesh->SetIndices(meshData.indices);

        meshMaterialNames.push_back(meshData.materialName);
        meshes.push_back(std::move(mesh));
    }

    // 蒙皮/動畫資料
    animNodes = modelData.nodes;
    skinsData = modelData.skins;
    animations = modelData.animations;
    if (!animNodes.empty()) {
        nodeWorld.assign(animNodes.size(), Matrix4::Identity());
        jointPalettes.resize(skinsData.size());
        for (size_t s = 0; s < skinsData.size(); ++s) {
            jointPalettes[s].assign(skinsData[s].joints.size(),
                                    Matrix4::Identity());
        }
        EvaluatePose(); // bind pose 預設
    }

    // 內嵌貼圖（glTF/VRM）→ GPU texture；檔案路徑貼圖維持原行為
    materialDefs = modelData.materials;
    for (const auto& pair : modelData.materials) {
        const MaterialData& md = pair.second;
        if (!md.embeddedDiffuse.empty() && md.embeddedWidth > 0 &&
            md.embeddedHeight > 0) {
            auto tex = MakeUnique<Texture>();
            if (tex->LoadFromMemory(md.embeddedDiffuse.data(), md.embeddedWidth,
                                    md.embeddedHeight, md.embeddedChannels)) {
                textures[md.name] = std::move(tex);
            }
        }
    }

    LOG_INFO("Loaded model: " + name + " with " +
             std::to_string(meshes.size()) + " meshes, " +
             std::to_string(skinnedMeshes.size()) + " skinned");
    return true;
}

// builtin "skinned" shader 的 uJointMatrices 陣列大小
static constexpr int kMaxShaderJoints = 128;

void Model::BindMeshMaterial(size_t meshIndex, const std::string& matName,
                             AdvancedShader& shader) const {
    (void)meshIndex;
    // skinned fragment shader 的 baseColorFactor 預設全 1——GL uniform
    // 初始為 0，不設會讓整個模型變黑/透明
    Vector3 baseColor(1.0f, 1.0f, 1.0f);
    auto mit = materialDefs.find(matName);
    if (mit != materialDefs.end()) baseColor = mit->second.diffuse;
    shader.SetVec4("baseColorFactor", baseColor.x, baseColor.y, baseColor.z,
                   1.0f);

    auto it = textures.find(matName);
    if (it != textures.end() && it->second) {
        it->second->Bind(0);
        // 兩種慣例都設：builtin "texture" 用 texture1，"skinned" 亦用 texture1
        shader.SetInt("texture1", 0);
        shader.SetInt("baseColorTexture", 0);
        shader.SetBool("useTexture", true);
    } else {
        shader.SetBool("useTexture", false);
    }
}

void Model::Draw(AdvancedShader& shader) const {
    if (!shader.IsValid()) return; // headless 或未編譯的 shader
    Matrix4 modelMatrix = GetModelMatrix();
    shader.SetMat4("model", modelMatrix);

    for (size_t i = 0; i < meshes.size(); ++i) {
        const std::string& matName =
            i < meshMaterialNames.size() ? meshMaterialNames[i] : name;
        BindMeshMaterial(i, matName, shader);
        // 靜態 mesh 不吃蒙皮——關掉避免沿用上一個模型的 palette
        shader.SetBool("uHasSkin", false);
        shader.SetInt("uJointCount", 0);
        meshes[i]->Draw();
    }
    for (size_t i = 0; i < skinnedMeshes.size(); ++i) {
        const std::string& matName = i < skinnedMeshMaterialNames.size()
                                         ? skinnedMeshMaterialNames[i] : name;
        BindMeshMaterial(i, matName, shader);

        // 上傳此 mesh 所屬 skin 的 joint palette；shader 無此 uniform 時 no-op
        int skinIdx = skinnedMeshes[i]->GetSkinIndex();
        shader.SetBool("uHasSkin", skinIdx >= 0);
        if (skinIdx >= 0 && skinIdx < (int)jointPalettes.size() &&
            !jointPalettes[skinIdx].empty()) {
            const auto& palette = jointPalettes[skinIdx];
            // shader 陣列上限 128——超過的 joint 在 shader 內落回 bind pose
            const int n = std::min((int)palette.size(), kMaxShaderJoints);
            shader.SetInt("uJointCount", n);
            shader.SetMat4Array("uJointMatrices", palette.data(), n);
        } else {
            shader.SetInt("uJointCount", 0);
        }
        skinnedMeshes[i]->Draw();
    }
}

void Model::DrawInstanced(AdvancedShader& shader, int instanceCount) const {
    if (!shader.IsValid()) return;
    Matrix4 modelMatrix = GetModelMatrix();
    shader.SetMat4("model", modelMatrix);
    
    for (const auto& mesh : meshes) {
        shader.SetBool("uHasSkin", false);
        shader.SetInt("uJointCount", 0);
        mesh->DrawInstanced(instanceCount);
    }
    for (const auto& mesh : skinnedMeshes) {
        int skinIdx = mesh->GetSkinIndex();
        shader.SetBool("uHasSkin", skinIdx >= 0);
        if (skinIdx >= 0 && skinIdx < (int)jointPalettes.size() &&
            !jointPalettes[skinIdx].empty()) {
            const auto& palette = jointPalettes[skinIdx];
            const int n = std::min((int)palette.size(), kMaxShaderJoints);
            shader.SetInt("uJointCount", n);
            shader.SetMat4Array("uJointMatrices", palette.data(), n);
        } else {
            shader.SetInt("uJointCount", 0);
        }
        mesh->DrawInstanced(instanceCount);
    }
}

void Model::SetPosition(const Vector3& pos) {
    position = pos;
}

void Model::SetRotation(const Quaternion& rot) {
    rotation = rot;
}

void Model::SetScale(const Vector3& s) {
    scale = s;
}

Matrix4 Model::GetModelMatrix() const {
    Matrix4 translation = Matrix4::Translation(position);
    Matrix4 rotationMatrix = rotation.ToMatrix();
    Matrix4 scaleMatrix = Matrix4::Scale(scale);
    
    return translation * rotationMatrix * scaleMatrix;
}

void Model::ProcessNode() {
    // 處理節點層次結構（簡化版本）
}

// ============================================================================
// 蒙皮 / 動畫求值
// ============================================================================

const std::string& Model::GetAnimationName(int index) const {
    static const std::string empty;
    if (index < 0 || index >= (int)animations.size()) return empty;
    return animations[index].name;
}

bool Model::PlayAnimation(int index) {
    if (index < 0 || index >= (int)animations.size()) return false;
    activeAnimation = index;
    animationTime = 0.0f;
    EvaluatePose();
    return true;
}

bool Model::PlayAnimationByName(const std::string& animName) {
    for (size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == animName) return PlayAnimation((int)i);
    }
    return false;
}

void Model::StopAnimation() {
    activeAnimation = -1;
    animationTime = 0.0f;
    EvaluatePose(); // 回到 bind pose
}

void Model::UpdateAnimation(float dt) {
    if (!std::isfinite(dt)) return; // NaN/Inf 會讓 fmod 卡在 NaN
    if (activeAnimation >= 0 && activeAnimation < (int)animations.size()) {
        float dur = animations[activeAnimation].duration;
        animationTime += dt;
        if (dur > 0.0f) {
            // loop；倒帶（dt<0）時 clamp 回 0
            animationTime = std::fmod(animationTime, dur);
            if (animationTime < 0.0f) animationTime += dur;
        }
    }
    EvaluatePose();
}

// 在 sampler 的 keyframe 時間軸上取樣；回傳值語意依 channel path 而定
Vector4 Model::SampleChannel(
    const ModelData::AnimationClip& clip,
    const ModelData::AnimationClip::Channel& channel, float time) const {
    using Sampler = ModelData::AnimationClip::Sampler;
    if (channel.sampler < 0 || channel.sampler >= (int)clip.samplers.size()) {
        return Vector4(0, 0, 0, 0);
    }
    const Sampler& smp = clip.samplers[channel.sampler];
    const bool cubic =
        smp.interpolation == ModelData::AnimationClip::Interpolation::CubicSpline;
    const size_t keys = smp.times.size();
    if (keys == 0) return Vector4(0, 0, 0, 0);
    // 畸形 glTF：output accessor 數量不足（linear 需 ≥keys、cubic 需 ≥3*keys）
    if (smp.values.size() < (cubic ? keys * 3 : keys)) {
        return Vector4(0, 0, 0, 0);
    }

    // cubic spline 每個 key 三筆（in/value/out），取值索引時除以 3
    auto valueAt = [&](size_t key) -> const Vector4& {
        return smp.values[cubic ? key * 3 + 1 : key];
    };

    if (time <= smp.times.front() || keys == 1) return valueAt(0);
    if (time >= smp.times.back()) return valueAt(keys - 1);

    // 找區間 [k, k+1]
    size_t k = 0;
    while (k + 1 < keys && smp.times[k + 1] < time) ++k;
    const float t0 = smp.times[k], t1 = smp.times[k + 1];
    const float span = std::max(t1 - t0, 1e-8f);
    const float f = (time - t0) / span;

    const Vector4& a = valueAt(k);
    const Vector4& b = valueAt(k + 1);

    if (smp.interpolation == ModelData::AnimationClip::Interpolation::Step) {
        return a;
    }
    // CUBICSPLINE 簡化為 Hermite：tangent 已是時間域斜率，需乘區間長；
    // rotation 也走 Hermite 再歸一化（近似 cubicspline quaternion）
    if (cubic) {
        const Vector4& m0 = smp.values[k * 3 + 2];       // out-tangent(k)
        const Vector4& m1 = smp.values[(k + 1) * 3];     // in-tangent(k+1)
        const float f2 = f * f, f3 = f2 * f;
        const float h00 = 2 * f3 - 3 * f2 + 1;
        const float h10 = f3 - 2 * f2 + f;
        const float h01 = -2 * f3 + 3 * f2;
        const float h11 = f3 - f2;
        Vector4 v(
            h00 * a.x + h10 * span * m0.x + h01 * b.x + h11 * span * m1.x,
            h00 * a.y + h10 * span * m0.y + h01 * b.y + h11 * span * m1.y,
            h00 * a.z + h10 * span * m0.z + h01 * b.z + h11 * span * m1.z,
            h00 * a.w + h10 * span * m0.w + h01 * b.w + h11 * span * m1.w);
        if (channel.path == ModelData::AnimationClip::Path::Rotation) {
            Quaternion q(v.x, v.y, v.z, v.w);
            q = q.Normalized();
            v = Vector4(q.x, q.y, q.z, q.w);
        }
        return v;
    }
    if (channel.path == ModelData::AnimationClip::Path::Rotation) {
        Quaternion qa(a.x, a.y, a.z, a.w), qb(b.x, b.y, b.z, b.w);
        Quaternion q = Quaternion::Slerp(qa, qb, f);
        return Vector4(q.x, q.y, q.z, q.w);
    }
    // Linear
    return Vector4(
        a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f,
        a.z + (b.z - a.z) * f, a.w + (b.w - a.w) * f);
}

void Model::EvaluatePose() {
    if (animNodes.empty()) return;

    const ModelData::AnimationClip* clip =
        (activeAnimation >= 0 && activeAnimation < (int)animations.size())
            ? &animations[activeAnimation] : nullptr;

    // 每個 node 的 local transform：被動畫 channel 命中的分量用取樣值覆蓋
    nodeWorld.resize(animNodes.size());
    for (size_t i = 0; i < animNodes.size(); ++i) {
        const ModelData::NodeData& nd = animNodes[i];
        Vector3 t = nd.translation;
        Quaternion r = nd.rotation;
        Vector3 s = nd.scale;
        bool useMatrix = nd.hasMatrix;

        if (clip) {
            for (const auto& ch : clip->channels) {
                if (ch.node != (int)i) continue;
                Vector4 v = SampleChannel(*clip, ch, animationTime);
                switch (ch.path) {
                case ModelData::AnimationClip::Path::Translation:
                    t = Vector3(v.x, v.y, v.z); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Rotation:
                    r = Quaternion(v.x, v.y, v.z, v.w); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Scale:
                    s = Vector3(v.x, v.y, v.z); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Weights:
                    break; // morph target 權重——Phase 3
                }
            }
        }

        Matrix4 local = useMatrix
            ? nd.matrix
            : Matrix4::Translation(t) * r.ToMatrix() * Matrix4::Scale(s);
        if (nd.parent >= (int)i) {
            // 手建的 ModelData 可能不符合 parent-before-child 序——
            // 這樣的 node 會被當 root，world transform 會錯
            LOG_WARNING("Model node '" + nd.name +
                        "' has parent index >= self; treated as root");
        }
        nodeWorld[i] = (nd.parent >= 0 && nd.parent < (int)i)
            ? nodeWorld[nd.parent] * local   // nodes 為 parent-before-child 序
            : local;
    }

    // joint palette = world(jointNode) * inverseBindMatrix
    for (size_t si = 0; si < skinsData.size(); ++si) {
        const auto& skin = skinsData[si];
        auto& palette = jointPalettes[si];
        palette.resize(skin.joints.size());
        for (size_t j = 0; j < skin.joints.size(); ++j) {
            int ni = skin.joints[j];
            const Matrix4& world =
                (ni >= 0 && ni < (int)nodeWorld.size())
                    ? nodeWorld[ni] : Matrix4::Identity();
            const Matrix4& ibm =
                j < skin.inverseBindMatrices.size()
                    ? skin.inverseBindMatrices[j] : Matrix4::Identity();
            palette[j] = world * ibm;
        }
    }
}

const std::vector<Matrix4>& Model::GetJointPalette(int skinIndex) const {
    static const std::vector<Matrix4> empty;
    if (skinIndex < 0 || skinIndex >= (int)jointPalettes.size()) return empty;
    return jointPalettes[skinIndex];
}

int Model::GetJointCount(int skinIndex) const {
    if (skinIndex < 0 || skinIndex >= (int)skinsData.size()) return 0;
    return (int)skinsData[skinIndex].joints.size();
}

Matrix4 Model::GetNodeWorldTransform(int nodeIndex) const {
    if (nodeIndex < 0 || nodeIndex >= (int)nodeWorld.size()) {
        return Matrix4::Identity();
    }
    return nodeWorld[nodeIndex];
}

int Model::FindNodeIndexByName(const std::string& namePart) const {
    if (namePart.empty()) return -1; // 空 needle 會誤中 node 0
    auto lower = [](std::string s) {
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return s;
    };
    const std::string needle = lower(namePart);
    for (size_t i = 0; i < animNodes.size(); ++i) {
        if (lower(animNodes[i].name).find(needle) != std::string::npos)
            return (int)i;
    }
    return -1;
}

bool Model::RotateNodeLocal(int nodeIndex, const Quaternion& q) {
    if (nodeIndex < 0 || nodeIndex >= (int)animNodes.size()) return false;
    if (!std::isfinite(q.x + q.y + q.z + q.w)) return false; // 擋 NaN/Inf
    ModelData::NodeData& nd = animNodes[nodeIndex];
    if (nd.hasMatrix) {
        // matrix 節點：以自身平移為軸心旋轉（M' = T·Rq·T⁻¹·M），
        // 與 TRS 路徑的 q*rotation 語意一致，且不丟 matrix 資訊
        const Vector3 t(nd.matrix.m[12], nd.matrix.m[13], nd.matrix.m[14]);
        nd.matrix = Matrix4::Translation(t) * q.ToMatrix() *
                    Matrix4::Translation(-t.x, -t.y, -t.z) * nd.matrix;
    } else {
        // parent 空間疊加：先套姿勢旋轉再乘 bind rotation
        nd.rotation = (q * nd.rotation).Normalized();
    }
    EvaluatePose();
    return true;
}

// ============================================================================
// ModelManager 實現
// ============================================================================

ModelManager::ModelManager()
    : maxCacheSize(100)
    , autoUnloadEnabled(true)
{
}

ModelManager::~ModelManager() {
    UnloadAll();
}

ModelManager& ModelManager::GetInstance() {
    static ModelManager instance;
    return instance;
}

Model* ModelManager::LoadModel(const std::string& path) {
    // 從路徑提取名稱
    size_t lastSlash = path.find_last_of("/\\");
    std::string name = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    
    // 檢查是否已加載
    auto it = models.find(name);
    if (it != models.end()) {
        return it->second.get();
    }
    
    // 加載新模型
    auto model = MakeUnique<Model>();
    if (model->LoadFromFile(path)) {
        models[name] = std::move(model);
        LOG_INFO("Cached model: " + name);
        return models[name].get();
    }
    
    LOG_ERROR("Failed to load model: " + path);
    return nullptr;
}

Model* ModelManager::GetModel(const std::string& name) {
    auto it = models.find(name);
    if (it != models.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ModelManager::UnloadModel(const std::string& name) {
    auto it = models.find(name);
    if (it != models.end()) {
        models.erase(it);
        LOG_INFO("Unloaded model: " + name);
    }
}

void ModelManager::UnloadAll() {
    models.clear();
    LOG_INFO("Unloaded all models");
}

void ModelManager::SetMaxCacheSize(size_t maxSize) {
    maxCacheSize = maxSize;
}

void ModelManager::EnableAutoUnload(bool enable) {
    autoUnloadEnabled = enable;
}

} // namespace Potato