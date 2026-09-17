#pragma once

#include "Core/CoreTypes.h"
#include "Rendering/OpenGLRenderer.h"
#include "Rendering/Shader.h"
#include "Rendering/SkinnedMesh.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Vector4.h"
#include "MathUtils/Quaternion.h"
#include "MathUtils/Matrix4.h"
#include <array>
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

    // 蒙皮資料（glTF JOINTS_0/WEIGHTS_0）；非空表示需要 skinned 渲染路徑
    std::vector<std::array<uint16, 4>> joints;
    std::vector<Vector4> weights;
    // 所屬 glTF skin（對應 ModelData::skins 索引）；-1 = 無蒙皮
    int skinIndex = -1;
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

    // glTF/VRM 內嵌貼圖：載入時已解碼為像素資料（非空時優先於檔案路徑）
    std::vector<unsigned char> embeddedDiffuse;
    int embeddedWidth = 0;
    int embeddedHeight = 0;
    int embeddedChannels = 0;
    // glTF alphaMode（OPAQUE/MASK/BLEND），供渲染層決定混合
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

/**
 * 模型數據
 */
struct ModelData {
    std::string name;
    std::vector<MeshData> meshes;
    std::unordered_map<std::string, MaterialData> materials;

    // ---- glTF 場景資料（Phase 2 蒙皮動畫用；OBJ 路徑留空）----
    // 節點階層：local TRS 與父節點索引；parent 一定小於自身索引
    struct NodeData {
        std::string name;
        int parent = -1;
        int mesh = -1;          // 對應 ModelData::meshes 起點索引（一個 node 可能拆成多 primitive）
        int meshCount = 0;
        int skin = -1;          // 對應 ModelData::skins 索引
        Vector3 translation;
        Quaternion rotation;
        Vector3 scale = Vector3(1.0f, 1.0f, 1.0f);
        // 節點以 16 欄 matrix 表示時（與 TRS 互斥）
        bool hasMatrix = false;
        Matrix4 matrix;
    };
    // skin：joint node 索引 + inverseBindMatrices
    struct SkinData {
        std::string name;
        std::vector<int> joints;                    // node 索引
        std::vector<Matrix4> inverseBindMatrices;
        int skeletonRoot = -1;                      // node 索引
    };
    // glTF 動畫：sampler = keyframe 時間/值，channel = 目標 node + 屬性
    struct AnimationClip {
        enum class Interpolation { Linear, Step, CubicSpline };
        enum class Path { Translation, Rotation, Scale, Weights };
        struct Sampler {
            std::vector<float> times;
            // translation/scale 用 xyz、rotation 用 xyzw；
            // CubicSpline 時每個 keyframe 佔三筆：inTangent/value/outTangent
            std::vector<Vector4> values;
            Interpolation interpolation = Interpolation::Linear;
        };
        struct Channel {
            int node = -1;
            Path path = Path::Translation;
            int sampler = -1;
        };
        std::string name;
        std::vector<Sampler> samplers;
        std::vector<Channel> channels;
        float duration = 0.0f;
    };

    std::vector<NodeData> nodes;
    std::vector<SkinData> skins;
    std::vector<AnimationClip> animations;
    bool hasVrmExtension = false;
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
 * GLTF 模型加載器（GLB 容器 + .gltf JSON）
 *
 * 支援：GLB/JSON 解析、accessor/bufferView/mesh primitive/node 階層、
 *       baseColor 貼圖（內嵌 PNG 解碼）、KHR_texture_transform。
 * 限制：僅靜態綁定姿勢（不跑 skinning/蒙皮變形）、僅 triangles、
 *       外部圖片 URI 不支援（僅 data: URI 與 bufferView 內嵌）。
 */
class GLTFLoader {
public:
    static bool LoadFromFile(const std::string& path, ModelData& modelData);
    static bool LoadFromMemory(const std::string& content, ModelData& modelData);

private:
    static bool ParseGLTF(const std::string& content, ModelData& modelData,
                          const std::string& baseDir = "");
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

    // ---- 蒙皮 / 動畫（glTF/VRM；OBJ 模型皆為 no-op）----
    bool HasSkinning() const { return !skinsData.empty(); }
    int GetAnimationCount() const { return (int)animations.size(); }
    const std::string& GetAnimationName(int index) const;
    bool PlayAnimation(int index);
    bool PlayAnimationByName(const std::string& animName);
    void StopAnimation();
    // 推進動畫時間（loop）並重新求值 pose；無動畫時僅求 bind pose
    void UpdateAnimation(float dt);
    // 依目前狀態求 node world transform + 各 skin 的 joint palette
    void EvaluatePose();
    // 求值結果（測試/除錯用）；skinIndex 越界回傳空陣列
    const std::vector<Matrix4>& GetJointPalette(int skinIndex) const;
    int GetJointCount(int skinIndex) const;
    // node 的 world transform（EvaluatePose 輸出）；越界回傳 identity
    Matrix4 GetNodeWorldTransform(int nodeIndex) const;

    // ---- 姿勢編輯（立繪擺位等）----
    // 依名稱子字串找 node（大小寫不敏感）；找不到回 -1
    int FindNodeIndexByName(const std::string& namePart) const;
    // 在 parent 空間疊加旋轉：local rotation = q * rotation，隨後重算 pose。
    // 節點被 active 動畫 channel 命中時 channel 仍優先。
    bool RotateNodeLocal(int nodeIndex, const Quaternion& q);

private:
    void ProcessNode();
    void BindMeshMaterial(size_t meshIndex, const std::string& matName,
                          AdvancedShader& shader) const;
    Vector4 SampleChannel(const ModelData::AnimationClip& clip,
                          const ModelData::AnimationClip::Channel& channel,
                          float time) const;
    
private:
    std::string name;
    std::vector<UniquePtr<Mesh>> meshes;
    std::vector<std::string> meshMaterialNames; // 每個 mesh 對應的材質名
    std::unordered_map<std::string, UniquePtr<Texture>> textures;
    std::unordered_map<std::string, MaterialData> materialDefs; // baseColor 等材質參數

    // ---- 蒙皮資料（glTF/VRM）----
    std::vector<UniquePtr<SkinnedMesh>> skinnedMeshes;
    std::vector<std::string> skinnedMeshMaterialNames;
    std::vector<ModelData::NodeData> animNodes;
    std::vector<ModelData::SkinData> skinsData;
    std::vector<ModelData::AnimationClip> animations;
    std::vector<Matrix4> nodeWorld;                    // EvaluatePose 輸出
    std::vector<std::vector<Matrix4>> jointPalettes;   // per skin
    int activeAnimation = -1;
    float animationTime = 0.0f;
    
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