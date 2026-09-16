#include "ModelLoader.h"
#include "Logging/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cmath>

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
// GLTFLoader 實現
// ============================================================================

bool GLTFLoader::LoadFromFile(const std::string& path, ModelData& modelData) {
    LOG_WARNING("GLTF loading not fully implemented yet");
    return false;
}

bool GLTFLoader::LoadFromMemory(const std::string& content, ModelData& modelData) {
    LOG_WARNING("GLTF loading not fully implemented yet");
    return false;
}

bool GLTFLoader::ParseGLTF(const std::string& content, ModelData& modelData) {
    // 需要 tinygltf 庫
    return false;
}

bool GLTFLoader::ParseGLB(const std::string& content, ModelData& modelData) {
    // 需要 tinygltf 庫
    return false;
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
    } else if (path.find(".gltf") != std::string::npos || path.find(".glb") != std::string::npos) {
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
    
    // 創建網格
    for (const auto& meshData : modelData.meshes) {
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
        
        meshes.push_back(std::move(mesh));
    }
    
    LOG_INFO("Loaded model: " + name + " with " + std::to_string(meshes.size()) + " meshes");
    return true;
}

void Model::Draw(AdvancedShader& shader) const {
    Matrix4 modelMatrix = GetModelMatrix();
    shader.SetMat4("model", modelMatrix);
    
    for (const auto& mesh : meshes) {
        mesh->Draw();
    }
}

void Model::DrawInstanced(AdvancedShader& shader, int instanceCount) const {
    Matrix4 modelMatrix = GetModelMatrix();
    shader.SetMat4("model", modelMatrix);
    
    for (const auto& mesh : meshes) {
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