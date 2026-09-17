#pragma once

// 蒙皮網格：在 Vertex 五組 attribute 之外附加 JOINTS_0/WEIGHTS_0。
// joint index 以 float attribute 上傳（GLSL 內 int() 取回），
// 避免 VertexArray 增加 integer-attrib 路徑。

#include "Rendering/OpenGLRenderer.h"
#include "MathUtils/Vector4.h"
#include <array>
#include <vector>

namespace Potato {

struct SkinnedVertex {
    Vector3 position;
    Vector3 normal;
    Vector2 texCoord;
    Vector3 tangent;
    Vector3 bitangent;
    Vector4 joints;   // attrib 5：實為 4 個 joint index
    Vector4 weights;  // attrib 6
};

class SkinnedMesh {
public:
    SkinnedMesh();
    ~SkinnedMesh();

    void SetVertices(const std::vector<SkinnedVertex>& vertices);
    void SetIndices(const std::vector<uint32>& indices);

    void Bind() const;
    void Unbind() const;

    void Draw() const;
    void DrawInstanced(int instanceCount) const;

    const VertexArray& GetVertexArray() const { return vertexArray; }

    // 此 mesh 使用的 skin（對應 ModelData::skins 索引）
    int GetSkinIndex() const { return skinIndex; }
    void SetSkinIndex(int idx) { skinIndex = idx; }

private:
    VertexArray vertexArray;
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32> indices;
    int skinIndex = -1;
};

} // namespace Potato
