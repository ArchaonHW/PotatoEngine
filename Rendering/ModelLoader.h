#pragma once

#include "Core/CoreTypes.h"
#include "Rendering/OpenGLRenderer.h"
#include "Rendering/Shader.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Quaternion.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Potato {

/**
 * 模型頂點結構
 */
struct ModelVertex {
    Vector3 position;
    Vector3 normal;
    Vector2 texCoord;
    Vector3 tangent;
    Vector3 bitangent;
};

/**
 * 網格數據
 */
struct MeshData {
    std::vector<ModelVertex> vertices;
    std::vector<uint32> indices;
    std::string materialName;
};

/**
 * 材質數據
 */
struct MaterialData {
    std::string name;
    Vector3 ambient;
    Vector3 diffuse;
    Vector3 specular;
    float shininess;
    std::string diffuseTexture;
    std::string normalTexture;
    std::string specularTexture;
};

/**
 * 模型數據
 */
struct ModelData {
    std::string name;
    std::vector<MeshData> meshes;
    std::unordered_map<std::string, MaterialData> materials;
};

/**
 * OBJ 模型加載器
 */
class OBJLoader {
public:
    static bool LoadFromFile(const std::string& path, ModelData& modelData);
    static bool LoadFromMemory(const std::string& content, ModelData& modelData);
    
private:
    static bool ParseOBJ(const std::string& content, ModelData& modelData);
    static bool ParseMTL(const std::string& content, std::unordered_map<std::string, MaterialData>& materials);
    static void OptimizeMeshes(ModelData& modelData);
};

/**
 * GLTF 模型加載器
 */
class GLTFLoader {
public:
    static bool LoadFromFile(const std::string& path, ModelData& modelData);
    static bool LoadFromMemory(const std::string& content, ModelData& modelData);
    
private:
    static bool ParseGLTF(const std::string& content, ModelData& modelData);
    static bool ParseGLB(const std::string& content, ModelData& modelData);
};

/**
 * 模型類
 */
class Model {
public:
    Model();
    ~Model();
    
    bool LoadFromFile(const std::string& path);
    bool LoadFromData(const ModelData& modelData);
    
    void Draw(AdvancedShader& shader) const;
    void DrawInstanced(AdvancedShader& shader, int instanceCount) const;
    
    const std::string& GetName() const { return name; }
    const std::vector<UniquePtr<Mesh>>& GetMeshes() const { return meshes; }
    
    void SetPosition(const Vector3& position);
    void SetRotation(const Quaternion& rotation);
    void SetScale(const Vector3& scale);
    
    Vector3 GetPosition() const { return position; }
    Quaternion GetRotation() const { return rotation; }
    Vector3 GetScale() const { return scale; }
    
    Matrix4 GetModelMatrix() const;
    
private:
    void ProcessNode();
    
private:
    std::string name;
    std::vector<UniquePtr<Mesh>> meshes;
    std::unordered_map<std::string, UniquePtr<Texture>> textures;
    
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
};

/**
 * 模型管理器
 */
class ModelManager {
public:
    static ModelManager& GetInstance();
    
    Model* LoadModel(const std::string& path);
    Model* GetModel(const std::string& name);
    
    void UnloadModel(const std::string& name);
    void UnloadAll();
    
    // 模型緩存設置
    void SetMaxCacheSize(size_t maxSize);
    void EnableAutoUnload(bool enable);
    
private:
    ModelManager();
    ~ModelManager();
    
    std::unordered_map<std::string, UniquePtr<Model>> models;
    size_t maxCacheSize;
    bool autoUnloadEnabled;
};

} // namespace Potato