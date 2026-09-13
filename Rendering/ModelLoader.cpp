#include "ModelLoader.h"
#include "Logging/Logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

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
            // 解析面 (簡化版本)
            std::string vertexStr;
            while (lineStream >> vertexStr) {
                // 格式: v/vt/vn
                std::replace(vertexStr.begin(), vertexStr.end(), '/', ' ');
                std::istringstream vertexStream(vertexStr);
                
                uint32 vIndex = 0, vtIndex = 0, vnIndex = 0;
                vertexStream >> vIndex >> vtIndex >> vnIndex;
                
                ModelVertex vertex;
                if (vIndex > 0 && vIndex <= positions.size()) {
                    vertex.position = positions[vIndex - 1];
                }
                if (vnIndex > 0 && vnIndex <= normals.size()) {
                    vertex.normal = normals[vnIndex - 1];
                }
                if (vtIndex > 0 && vtIndex <= texCoords.size()) {
                    vertex.texCoord = texCoords[vtIndex - 1];
                }
                
                currentMesh.vertices.push_back(vertex);
                currentMesh.indices.push_back(static_cast<uint32>(currentMesh.vertices.size()) - 1);
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
    // 簡化實現：計算切線和副切線
    for (auto& mesh : modelData.meshes) {
        for (auto& vertex : mesh.vertices) {
            vertex.tangent = Vector3(1.0f, 0.0f, 0.0f);
            vertex.bitangent = Vector3(0.0f, 1.0f, 0.0f);
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

void Model::Draw(const AdvancedShader& shader) const {
    Matrix4 modelMatrix = GetModelMatrix();
    shader.SetMat4("model", modelMatrix);
    
    for (const auto& mesh : meshes) {
        mesh->Draw();
    }
}

void Model::DrawInstanced(const AdvancedShader& shader, int instanceCount) const {
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