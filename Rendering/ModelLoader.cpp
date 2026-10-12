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
    (void)content;
    (void)materials;
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

// VRM 0.x 擴充解析（定義在後）
void GltfParseVrmExtension(const tinygltf::Model& g, ModelData& modelData,
                           const std::vector<int>& gltfToModel);

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
                          int skinIndex, int sourceMesh, int sourcePrim,
                          ModelData& modelData) {
    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return; // 無位置資料的面片直接略過

    MeshData mesh;
    mesh.skinIndex = skinIndex;
    mesh.sourceMesh = sourceMesh;
    mesh.sourcePrim = sourcePrim;
    const int posAccIdx = posIt->second;
    // 經 GltfAccessorData 取得邊界驗證過的 count——直接用 accessor.count
    // 在畸形檔（巨大 count + 小 buffer）會 resize 失敗炸記憶體
    size_t vertCount; int posNc; int posCt; size_t posStride;
    if (!GltfAccessorData(g, posAccIdx, vertCount, posNc, posCt, posStride)) {
        return; // accessor 越界或引用無效
    }
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

    // morph targets（glTF targets[]）：POSITION/NORMAL delta
    for (const auto& target : prim.targets) {
        MeshData::MorphTarget mt;
        auto tp = target.find("POSITION");
        if (tp != target.end()) {
            mt.positionDeltas.reserve(vertCount);
            for (size_t i = 0; i < vertCount; ++i) {
                mt.positionDeltas.push_back(GltfReadVec3(g, tp->second, i));
            }
        }
        auto tn = target.find("NORMAL");
        if (tn != target.end()) {
            mt.normalDeltas.reserve(vertCount);
            for (size_t i = 0; i < vertCount; ++i) {
                mt.normalDeltas.push_back(GltfReadVec3(g, tn->second, i));
            }
        }
        if (!mt.positionDeltas.empty() || !mt.normalDeltas.empty()) {
            mesh.morphTargets.push_back(std::move(mt));
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
                uint32 ix = (uint32)GltfComponentToFloat(base + i * stride, ct, false);
                // 畸形索引（>= vertCount）會讓 glDrawElements 讀越界 → 丟棄
                if (ix < vertCount) {
                    mesh.indices.push_back(ix);
                }
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
                  std::vector<char>& visited, std::vector<int>& gltfToModel,
                  int depth = 0) {
    // 節點環保護：glTF 規格無環，但畸形/惡意檔案可能有——visited 防無窮遞迴；
    // 深度上限防極深線性鏈造成 stack overflow
    if (depth > 256 || visited[nodeIdx]) return;
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
        for (size_t pi = 0; pi < gm.primitives.size(); ++pi) {
            const tinygltf::Primitive& prim = gm.primitives[pi];
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;
            // JOINTS_0 與 WEIGHTS_0 必須成對存在才算蒙皮——缺 WEIGHTS_0
            // 時退回靜態烘焙（否則頂點留在 mesh space 又走不到 skinned 路徑）
            const bool skinned =
                prim.attributes.find("JOINTS_0") != prim.attributes.end() &&
                prim.attributes.find("WEIGHTS_0") != prim.attributes.end();
            GltfConvertPrimitive(g, prim, world, skinned, node.skin,
                                 node.mesh, (int)pi, modelData);
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
                         gltfToModel, depth + 1);
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
    if (sceneIdx >= (int)g.scenes.size()) sceneIdx = -1; // 畸形 defaultScene 防越界
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
            if (base && ct == TINYGLTF_COMPONENT_TYPE_FLOAT && nc == 16) {
                // IBM 必須是 MAT4（16 float）；VEC4 等會讓 stride 解讀錯位
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
                int outNc = tinygltf::GetNumComponentsInType(
                    static_cast<uint32_t>(outAcc.type));
                if (outNc == 1) {
                    // SCALAR 輸出：morph weights 通道——扁平存（每 key 數個權重）
                    smp.scalarValues.reserve(n);
                    size_t sc; int snc, sct; size_t sstride;
                    const unsigned char* outBase =
                        GltfAccessorData(g, s.output, sc, snc, sct, sstride);
                    if (outBase) {
                        for (size_t i = 0; i < n && i < sc; ++i) {
                            smp.scalarValues.push_back(GltfComponentToFloat(
                                outBase + i * sstride, sct, outAcc.normalized));
                        }
                    }
                } else {
                    smp.values.reserve(n);
                    for (size_t i = 0; i < n; ++i) {
                        if (outNc >= 4) {
                            smp.values.push_back(GltfReadVec4(g, s.output, i));
                        } else {
                            Vector3 v3 = GltfReadVec3(g, s.output, i);
                            smp.values.push_back(
                                Vector4(v3.x, v3.y, v3.z, 0.0f));
                        }
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
                // scalarValues（weights）：每 key N 個，N = count/keys
                const size_t sPerKey =
                    (!smp.scalarValues.empty() && !smp.times.empty())
                        ? smp.scalarValues.size() / smp.times.size() : 0;
                std::vector<float> sv2;
                if (sPerKey > 0 &&
                    smp.scalarValues.size() >= smp.times.size() * sPerKey) {
                    sv2.resize(smp.scalarValues.size());
                }
                for (size_t i = 0; i < order.size(); ++i) {
                    t2[i] = smp.times[order[i]];
                    for (size_t c = 0; c < mul && !v2.empty(); ++c) {
                        v2[i * mul + c] = smp.values[order[i] * mul + c];
                    }
                    for (size_t c = 0; c < sPerKey && !sv2.empty(); ++c) {
                        sv2[i * sPerKey + c] =
                            smp.scalarValues[order[i] * sPerKey + c];
                    }
                }
                smp.times.swap(t2);
                if (!v2.empty()) smp.values.swap(v2);
                if (!sv2.empty()) smp.scalarValues.swap(sv2);
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

    // VRM 0.x 擴充（humanoid/blendshape/spring bone/MToon）
    GltfParseVrmExtension(g, modelData, gltfToModel);

    if (modelData.meshes.empty()) {
        LOG_ERROR("glTF/VRM contains no triangle primitives: " + name);
        return false;
    }
    return true;
}

// ---- VRM 0.x 擴充解析（tinygltf::Value 樹）----

const tinygltf::Value* VrmFind(const tinygltf::Value& obj, const char* key) {
    if (!obj.IsObject()) return nullptr;
    const auto& o = obj.Get<tinygltf::Value::Object>();
    auto it = o.find(key);
    return it != o.end() ? &it->second : nullptr;
}

double VrmNum(const tinygltf::Value* v, double def = 0.0) {
    return (v && v->IsNumber()) ? v->GetNumberAsDouble() : def;
}

int VrmInt(const tinygltf::Value* v, int def = -1) {
    return (v && v->IsNumber()) ? v->GetNumberAsInt() : def;
}

std::string VrmStr(const tinygltf::Value* v) {
    return (v && v->IsString()) ? v->Get<std::string>() : std::string();
}

Vector3 VrmVec3(const tinygltf::Value* v, const Vector3& def) {
    if (!v || !v->IsObject()) return def;
    return Vector3((float)VrmNum(VrmFind(*v, "x"), def.x),
                   (float)VrmNum(VrmFind(*v, "y"), def.y),
                   (float)VrmNum(VrmFind(*v, "z"), def.z));
}

// vectorProperties 的顏色是 4 元素 array
Vector3 VrmColor3(const tinygltf::Value* arr, const Vector3& def) {
    if (!arr || !arr->IsArray() || arr->Size() < 3) return def;
    return Vector3((float)arr->Get(0).GetNumberAsDouble(),
                   (float)arr->Get(1).GetNumberAsDouble(),
                   (float)arr->Get(2).GetNumberAsDouble());
}

void GltfParseVrmExtension(const tinygltf::Model& g, ModelData& modelData,
                           const std::vector<int>& gltfToModel) {
    auto vit = g.extensions.find("VRM");
    if (vit == g.extensions.end() || !vit->second.IsObject()) return;
    const tinygltf::Value& vrm = vit->second;
    ModelData::VrmData& out = modelData.vrm;

    auto remapNode = [&](int gltfIdx) {
        return (gltfIdx >= 0 && gltfIdx < (int)gltfToModel.size())
                   ? gltfToModel[gltfIdx] : -1;
    };

    // humanoid.humanBones[]：{bone:"hips", node:N, useDefaultValues:true}
    if (const tinygltf::Value* humanoid = VrmFind(vrm, "humanoid")) {
        if (const tinygltf::Value* bones = VrmFind(*humanoid, "humanBones")) {
            if (bones->IsArray()) {
                for (size_t i = 0; i < bones->Size(); ++i) {
                    const tinygltf::Value& b = bones->Get(i);
                    std::string boneName = VrmStr(VrmFind(b, "bone"));
                    int ni = remapNode(VrmInt(VrmFind(b, "node")));
                    if (!boneName.empty() && ni >= 0) {
                        out.humanoidBones[boneName] = ni;
                    }
                }
            }
        }
    }

    // blendShapeMaster.blendShapeGroups[] → expressions
    if (const tinygltf::Value* bsm = VrmFind(vrm, "blendShapeMaster")) {
        if (const tinygltf::Value* groups =
                VrmFind(*bsm, "blendShapeGroups")) {
            if (groups->IsArray()) {
                for (size_t i = 0; i < groups->Size(); ++i) {
                    const tinygltf::Value& grp = groups->Get(i);
                    ModelData::VrmData::Expression expr;
                    expr.name = VrmStr(VrmFind(grp, "name"));
                    expr.presetName = VrmStr(VrmFind(grp, "presetName"));
                    if (const tinygltf::Value* binds = VrmFind(grp, "binds")) {
                        if (binds->IsArray()) {
                            for (size_t j = 0; j < binds->Size(); ++j) {
                                const tinygltf::Value& bd = binds->Get(j);
                                ModelData::VrmData::Expression::Bind b;
                                b.sourceMesh = VrmInt(VrmFind(bd, "mesh"));
                                b.targetIndex = VrmInt(VrmFind(bd, "index"));
                                b.weight = (float)VrmNum(
                                    VrmFind(bd, "weight"), 0.0);
                                expr.binds.push_back(b);
                            }
                        }
                    }
                    out.expressions.push_back(std::move(expr));
                }
            }
        }
    }

    // secondaryAnimation：colliderGroups + boneGroups（spring bone）
    if (const tinygltf::Value* sec = VrmFind(vrm, "secondaryAnimation")) {
        if (const tinygltf::Value* cgs = VrmFind(*sec, "colliderGroups")) {
            if (cgs->IsArray()) {
                for (size_t i = 0; i < cgs->Size(); ++i) {
                    const tinygltf::Value& cg = cgs->Get(i);
                    ModelData::VrmData::ColliderGroup grp;
                    grp.node = remapNode(VrmInt(VrmFind(cg, "node")));
                    if (const tinygltf::Value* cols =
                            VrmFind(cg, "colliders")) {
                        if (cols->IsArray()) {
                            for (size_t j = 0; j < cols->Size(); ++j) {
                                const tinygltf::Value& c = cols->Get(j);
                                ModelData::VrmData::Collider col;
                                col.offset =
                                    VrmVec3(VrmFind(c, "offset"), Vector3());
                                col.radius = (float)VrmNum(
                                    VrmFind(c, "radius"), 0.0);
                                grp.colliders.push_back(col);
                            }
                        }
                    }
                    out.colliderGroups.push_back(std::move(grp));
                }
            }
        }
        if (const tinygltf::Value* bgs = VrmFind(*sec, "boneGroups")) {
            if (bgs->IsArray()) {
                for (size_t i = 0; i < bgs->Size(); ++i) {
                    const tinygltf::Value& bg = bgs->Get(i);
                    ModelData::VrmData::BoneGroup grp;
                    grp.stiffness = (float)VrmNum(
                        VrmFind(bg, "stiffiness"), 1.0); // VRM 0.x 拼法
                    grp.gravityPower = (float)VrmNum(
                        VrmFind(bg, "gravityPower"), 0.0);
                    grp.gravityDir = VrmVec3(VrmFind(bg, "gravityDir"),
                                             Vector3(0, -1, 0));
                    grp.dragForce =
                        (float)VrmNum(VrmFind(bg, "dragForce"), 0.4f);
                    grp.hitRadius =
                        (float)VrmNum(VrmFind(bg, "hitRadius"), 0.0f);
                    if (const tinygltf::Value* bones = VrmFind(bg, "bones")) {
                        if (bones->IsArray()) {
                            for (size_t j = 0; j < bones->Size(); ++j) {
                                int ni = remapNode(bones->Get(j).GetNumberAsInt());
                                if (ni >= 0) grp.bones.push_back(ni);
                            }
                        }
                    }
                    if (const tinygltf::Value* cgs2 =
                            VrmFind(bg, "colliderGroups")) {
                        if (cgs2->IsArray()) {
                            for (size_t j = 0; j < cgs2->Size(); ++j) {
                                if (cgs2->Get(j).IsNumber()) {
                                    grp.colliderGroups.push_back(
                                        cgs2->Get(j).GetNumberAsInt());
                                }
                            }
                        }
                    }
                    out.boneGroups.push_back(std::move(grp));
                }
            }
        }
    }

    // materialProperties[] → MToon 參數（依 name 對應 glTF material）
    if (const tinygltf::Value* mps = VrmFind(vrm, "materialProperties")) {
        if (mps->IsArray()) {
            for (size_t i = 0; i < mps->Size(); ++i) {
                const tinygltf::Value& mp = mps->Get(i);
                std::string shader = VrmStr(VrmFind(mp, "shader"));
                if (shader.find("MToon") == std::string::npos) continue;
                // 依 name 對應；找不到就依序對應 materials[i]
                std::string mpName = VrmStr(VrmFind(mp, "name"));
                std::string matName = mpName;
                if (matName.empty() && i < g.materials.size()) {
                    matName = g.materials[i].name.empty()
                                  ? "gltf_mat_" + std::to_string(i)
                                  : g.materials[i].name;
                }
                auto mit = modelData.materials.find(matName);
                if (mit == modelData.materials.end() &&
                    i < g.materials.size()) {
                    std::string alt = g.materials[i].name.empty()
                                          ? "gltf_mat_" + std::to_string(i)
                                          : g.materials[i].name;
                    mit = modelData.materials.find(alt);
                }
                if (mit == modelData.materials.end()) continue;
                MaterialData& md = mit->second;
                md.mtoon = true;
                if (const tinygltf::Value* fp =
                        VrmFind(mp, "floatProperties")) {
                    md.shadeToony =
                        (float)VrmNum(VrmFind(*fp, "_ShadeToony"), 0.9);
                    if (const tinygltf::Value* ow =
                            VrmFind(*fp, "_OutlineWidth")) {
                        md.outlineWidth = (float)ow->GetNumberAsDouble();
                    }
                }
                if (const tinygltf::Value* vp =
                        VrmFind(mp, "vectorProperties")) {
                    md.shadeColor = VrmColor3(VrmFind(*vp, "_ShadeColor"),
                                              Vector3(0, 0, 0));
                    md.outlineColor = VrmColor3(VrmFind(*vp, "_OutlineColor"),
                                                Vector3(0, 0, 0));
                }
            }
        }
    }
}



} // namespace

bool GLTFLoader::LoadFromFile(const std::string& path, ModelData& modelData) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model gltf;
    std::string err, warn;
    // 大小寫不敏感副檔名比對：.GLTF 也走 ASCII 路徑
    std::string ext = path.size() >= 5 ? path.substr(path.size() - 5) : "";
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    const bool ascii = ext == ".gltf";
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
    hasVrm = false;
    humanoidBones.clear();
    vrmExpressions.clear();
    vrmColliderGroups.clear();
    vrmBoneGroups.clear();
    meshDataToMorph.clear();
    sourceMeshToMd.clear();
    morphRuntimes.clear();
    springStates.clear();

    // 創建網格：有 JOINTS_0/WEIGHTS_0 的走 SkinnedMesh，其餘走一般 Mesh
    // meshDataOut[mi] = 產出的 mesh（供 morph runtime 回寫頂點）
    std::vector<Mesh*> staticOut(modelData.meshes.size(), nullptr);
    std::vector<SkinnedMesh*> skinnedOut(modelData.meshes.size(), nullptr);
    std::vector<std::vector<SkinnedVertex>> skinnedVerts(modelData.meshes.size());
    for (size_t mi = 0; mi < modelData.meshes.size(); ++mi) {
        const MeshData& meshData = modelData.meshes[mi];
        if (!meshData.joints.empty()) {
            auto mesh = MakeUnique<SkinnedMesh>();
            std::vector<SkinnedVertex>& vertices = skinnedVerts[mi];
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
            skinnedOut[mi] = mesh.get();
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
        staticOut[mi] = mesh.get();
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

    // ---- VRM / morph / spring bone 執行期資料 ----
    hasVrm = modelData.hasVrmExtension;
    humanoidBones = modelData.vrm.humanoidBones;
    vrmExpressions = modelData.vrm.expressions;
    vrmColliderGroups = modelData.vrm.colliderGroups;
    vrmBoneGroups = modelData.vrm.boneGroups;

    // glTF mesh 索引 → meshData 索引（一個 glTF mesh 可拆多 primitive）
    for (size_t mi = 0; mi < modelData.meshes.size(); ++mi) {
        if (modelData.meshes[mi].sourceMesh >= 0) {
            sourceMeshToMd[modelData.meshes[mi].sourceMesh].push_back((int)mi);
        }
    }

    // morph target 執行期：保存原始頂點 + 每 target 權重
    meshDataToMorph.assign(modelData.meshes.size(), -1);
    for (size_t mi = 0; mi < modelData.meshes.size(); ++mi) {
        const MeshData& md = modelData.meshes[mi];
        if (md.morphTargets.empty()) continue;
        MorphRuntime rt;
        rt.meshDataIndex = (int)mi;
        rt.staticMesh = staticOut[mi];
        rt.skinnedMesh = skinnedOut[mi];
        rt.targets = md.morphTargets;
        rt.weights.assign(rt.targets.size(), 0.0f);
        rt.exprWeights.assign(rt.targets.size(), 0.0f);
        // base：保留完整頂點（skinned 含 joints/weights）
        rt.base.reserve(md.vertices.size());
        if (rt.skinnedMesh) {
            rt.base = skinnedVerts[mi];
        } else {
            for (const ModelVertex& mv : md.vertices) {
                SkinnedVertex v;
                v.position = mv.position;
                v.normal = mv.normal;
                v.texCoord = mv.texCoord;
                v.tangent = mv.tangent;
                v.bitangent = mv.bitangent;
                rt.base.push_back(v);
            }
        }
        meshDataToMorph[mi] = (int)morphRuntimes.size();
        morphRuntimes.push_back(std::move(rt));
    }

    // spring bone 初始狀態：bone 末端 = 第一個 child 的 local 平移
    // （無 child 時用 hitRadius 或 5cm 預設長度）
    for (size_t gi = 0; gi < vrmBoneGroups.size(); ++gi) {
        for (int bn : vrmBoneGroups[gi].bones) {
            if (bn < 0 || bn >= (int)animNodes.size()) continue;
            SpringState st;
            st.node = bn;
            st.groupIndex = (int)gi;
            Vector3 tailLocal(0.0f, 0.05f, 0.0f); // 預設 5cm
            // 優先取第一個 child 的平移當 bone 方向
            for (size_t ci = 0; ci < animNodes.size(); ++ci) {
                if (animNodes[ci].parent == bn) {
                    tailLocal = animNodes[ci].translation;
                    break;
                }
            }
            st.restLen = std::max(tailLocal.Length(), 0.001f);
            st.tailLocal = tailLocal.Normalized() * st.restLen;
            springStates.push_back(st);
        }
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
    if (mit != materialDefs.end()) {
        baseColor = mit->second.diffuse;
        if (mit->second.mtoon) {
            // MToon 陰影參數（builtin "mtoon" shader 使用）
            shader.SetVec3("shadeColor", mit->second.shadeColor);
            shader.SetFloat("shadeToony", mit->second.shadeToony);
        }
    }
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
    UpdateSpringBones(dt); // VRM spring bone（無則 no-op）
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
                if (ch.path == ModelData::AnimationClip::Path::Weights) {
                    // morph 權重：寫入該 node 掛的 mesh 的 morph runtime
                    std::vector<float> wv;
                    SampleWeightsChannel(*clip, ch, animationTime, wv);
                    const ModelData::NodeData& wnd = animNodes[i];
                    for (int m = wnd.mesh;
                         m >= 0 && m < wnd.mesh + wnd.meshCount &&
                         m < (int)meshDataToMorph.size(); ++m) {
                        int mi = meshDataToMorph[m];
                        if (mi < 0) continue;
                        MorphRuntime& rt = morphRuntimes[mi];
                        for (size_t j = 0;
                             j < rt.weights.size() && j < wv.size(); ++j) {
                            rt.weights[j] = wv[j];
                        }
                        rt.dirty = true;
                    }
                    continue;
                }
                Vector4 v = SampleChannel(*clip, ch, animationTime);
                switch (ch.path) {
                case ModelData::AnimationClip::Path::Translation:
                    t = Vector3(v.x, v.y, v.z); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Rotation:
                    r = Quaternion(v.x, v.y, v.z, v.w); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Scale:
                    s = Vector3(v.x, v.y, v.z); useMatrix = false; break;
                case ModelData::AnimationClip::Path::Weights:
                    break; // 已在上方處理
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

    ApplyMorphs(); // morph 權重有變動時回寫頂點
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

// ============================================================================
// VRM：humanoid / 表情 morph / spring bone
// ============================================================================

int Model::GetHumanoidBone(const std::string& vrmBoneName) const {
    auto it = humanoidBones.find(vrmBoneName);
    return it != humanoidBones.end() ? it->second : -1;
}

bool Model::RotateHumanoidBone(const std::string& vrmBoneName,
                               const Quaternion& q) {
    int ni = GetHumanoidBone(vrmBoneName);
    return ni >= 0 && RotateNodeLocal(ni, q);
}

const std::string& Model::GetExpressionName(int index) const {
    static const std::string empty;
    if (index < 0 || index >= (int)vrmExpressions.size()) return empty;
    const auto& e = vrmExpressions[index];
    return !e.name.empty() ? e.name : e.presetName;
}

bool Model::SetExpression(const std::string& exprName, float weight) {
    if (exprName.empty()) return false;
    auto lower = [](std::string s) {
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return s;
    };
    const std::string needle = lower(exprName);
    for (const auto& e : vrmExpressions) {
        if (lower(e.name) != needle && lower(e.presetName) != needle) {
            continue;
        }
        for (const auto& b : e.binds) {
            // VRM bind 參照 glTF mesh 索引，套到該 mesh 全部 primitive
            auto sit = sourceMeshToMd.find(b.sourceMesh);
            if (sit == sourceMeshToMd.end()) continue;
            // VRM 0.x bind weight 為 0~100 百分比；<=1.5 視為已是 0~1
            const float bindW =
                b.weight > 1.5f ? b.weight * 0.01f : b.weight;
            for (int mdIdx : sit->second) {
                int mi = (mdIdx >= 0 && mdIdx < (int)meshDataToMorph.size())
                             ? meshDataToMorph[mdIdx] : -1;
                if (mi < 0) continue;
                MorphRuntime& rt = morphRuntimes[mi];
                if (b.targetIndex >= 0 &&
                    b.targetIndex < (int)rt.exprWeights.size()) {
                    rt.exprWeights[b.targetIndex] = weight * bindW;
                    rt.dirty = true;
                }
            }
        }
        ApplyMorphs();
        return true;
    }
    return false;
}

void Model::ClearExpressions() {
    for (auto& rt : morphRuntimes) {
        if (std::any_of(rt.exprWeights.begin(), rt.exprWeights.end(),
                        [](float w) { return w != 0.0f; })) {
            std::fill(rt.exprWeights.begin(), rt.exprWeights.end(), 0.0f);
            rt.dirty = true;
        }
    }
    ApplyMorphs();
}

void Model::ApplyMorphs() {
    for (auto& rt : morphRuntimes) {
        if (!rt.dirty) continue;
        rt.dirty = false;
        std::vector<SkinnedVertex> out = rt.base;
        for (size_t t = 0; t < rt.targets.size(); ++t) {
            const float w =
                (t < rt.weights.size() ? rt.weights[t] : 0.0f) +
                (t < rt.exprWeights.size() ? rt.exprWeights[t] : 0.0f);
            if (w == 0.0f) continue;
            const auto& mt = rt.targets[t];
            for (size_t i = 0; i < out.size(); ++i) {
                if (i < mt.positionDeltas.size()) {
                    out[i].position += mt.positionDeltas[i] * w;
                }
                if (i < mt.normalDeltas.size()) {
                    out[i].normal += mt.normalDeltas[i] * w;
                }
            }
        }
        if (rt.skinnedMesh) {
            rt.skinnedMesh->SetVertices(out);
        } else if (rt.staticMesh) {
            std::vector<Vertex> sv;
            sv.reserve(out.size());
            for (const auto& v : out) {
                Vertex d;
                d.position = v.position;
                d.normal = v.normal;
                d.texCoord = v.texCoord;
                d.tangent = v.tangent;
                d.bitangent = v.bitangent;
                sv.push_back(d);
            }
            rt.staticMesh->SetVertices(sv);
        }
    }
}

void Model::SampleWeightsChannel(
    const ModelData::AnimationClip& clip,
    const ModelData::AnimationClip::Channel& channel, float time,
    std::vector<float>& out) const {
    out.clear();
    if (channel.sampler < 0 ||
        channel.sampler >= (int)clip.samplers.size()) return;
    const auto& smp = clip.samplers[channel.sampler];
    const size_t keys = smp.times.size();
    if (keys == 0 || smp.scalarValues.empty()) return;
    const size_t perKey = smp.scalarValues.size() / keys;
    if (perKey == 0) return;
    out.resize(perKey, 0.0f);

    auto readKey = [&](size_t k, std::vector<float>& dst) {
        for (size_t j = 0; j < perKey; ++j) {
            dst[j] = smp.scalarValues[k * perKey + j];
        }
    };
    if (time <= smp.times.front() || keys == 1) { readKey(0, out); return; }
    if (time >= smp.times.back()) { readKey(keys - 1, out); return; }
    size_t k = 0;
    while (k + 1 < keys && smp.times[k + 1] < time) ++k;
    const float t0 = smp.times[k], t1 = smp.times[k + 1];
    const float f = (time - t0) / std::max(t1 - t0, 1e-8f);
    if (smp.interpolation == ModelData::AnimationClip::Interpolation::Step) {
        readKey(k, out);
        return;
    }
    for (size_t j = 0; j < perKey; ++j) {
        const float a = smp.scalarValues[k * perKey + j];
        const float b = smp.scalarValues[(k + 1) * perKey + j];
        out[j] = a + (b - a) * f;
    }
}

// 兩向量間的最小旋轉（平行→identity；反向→任取垂直軸轉 180°）
static Quaternion QuatBetweenVectors(const Vector3& a, const Vector3& b) {
    const Vector3 na = a.Normalized();
    const Vector3 nb = b.Normalized();
    const float d = std::max(-1.0f, std::min(1.0f, na.Dot(nb)));
    if (d > 0.9999f) return Quaternion::Identity();
    if (d < -0.9999f) {
        Vector3 axis = na.Cross(Vector3(1, 0, 0));
        if (axis.LengthSquared() < 1e-6f) {
            axis = na.Cross(Vector3(0, 1, 0));
        }
        return Quaternion::FromAxisAngle(axis.Normalized(), 3.14159265f);
    }
    return Quaternion::FromAxisAngle(na.Cross(nb).Normalized(),
                                     std::acos(d));
}

void Model::UpdateSpringBones(float dt) {
    if (springStates.empty() || animNodes.empty()) return;
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    if (nodeWorld.size() != animNodes.size()) EvaluatePose();

    for (auto& st : springStates) {
        const int ni = st.node;
        const auto& grp = vrmBoneGroups[st.groupIndex];
        const Matrix4& world = nodeWorld[ni];
        const int parent = animNodes[ni].parent;
        const Matrix4 parentWorld =
            (parent >= 0 && parent < (int)nodeWorld.size())
                ? nodeWorld[parent] : Matrix4::Identity();
        const Matrix4 parentInv = parentWorld.Inverse();

        const Vector3 head(world.m[12], world.m[13], world.m[14]);
        const Vector3 tailCur = world.TransformPoint(st.tailLocal);
        if (!st.initialized) {
            st.tail = st.prevTail = tailCur;
            st.initialized = true;
        }

        // verlet：慣性 + stiffness 回拉 + 重力
        const Vector3 velocity = (st.tail - st.prevTail) * (1.0f - grp.dragForce);
        const Vector3 restDirW = world.TransformVector(st.tailLocal).Normalized();
        Vector3 next = tailCur + velocity
                       + restDirW * (grp.stiffness * dt)
                       + grp.gravityDir * (grp.gravityPower * dt);

        // 長度約束：|next-head| = restLen
        Vector3 dir = next - head;
        const float len = dir.Length();
        if (len < 1e-6f) continue;
        next = head + dir * (st.restLen / len);

        // collider 球推出（collider 掛在各自 node 的 world 位置）
        for (int cgi : grp.colliderGroups) {
            if (cgi < 0 || cgi >= (int)vrmColliderGroups.size()) continue;
            const auto& cg = vrmColliderGroups[cgi];
            if (cg.node < 0 || cg.node >= (int)nodeWorld.size()) continue;
            const Matrix4& cWorld = nodeWorld[cg.node];
            for (const auto& col : cg.colliders) {
                const Vector3 c = cWorld.TransformPoint(col.offset);
                const float r = col.radius + grp.hitRadius;
                Vector3 d = next - c;
                const float dl = d.Length();
                if (dl < r && dl > 1e-6f) {
                    next = c + d * (r / dl);
                }
            }
        }

        st.prevTail = st.tail;
        st.tail = next;

        // 旋轉 node 使 tail 指向新位置（在 parent 空間求 delta）
        const Vector3 curDir = (tailCur - head).Normalized();
        const Vector3 newDir = (next - head).Normalized();
        const Quaternion q = QuatBetweenVectors(
            parentInv.TransformVector(curDir),
            parentInv.TransformVector(newDir));

        auto& nd = animNodes[ni];
        if (nd.hasMatrix) {
            const Vector3 t(nd.matrix.m[12], nd.matrix.m[13], nd.matrix.m[14]);
            nd.matrix = Matrix4::Translation(t) * q.ToMatrix() *
                        Matrix4::Translation(-t.x, -t.y, -t.z) * nd.matrix;
        } else {
            nd.rotation = (q * nd.rotation).Normalized();
        }
    }

    EvaluatePose(); // 重算 world transform 與 joint palette
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