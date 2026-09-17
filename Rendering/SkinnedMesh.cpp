#include "SkinnedMesh.h"

#include <cstddef>

#include <glad/glad.h>

namespace Potato {

SkinnedMesh::SkinnedMesh() {
}

SkinnedMesh::~SkinnedMesh() {
}

void SkinnedMesh::SetVertices(const std::vector<SkinnedVertex>& verts) {
    vertices = verts;
    vertexArray.AddVertexBuffer(vertices.data(),
                                vertices.size() * sizeof(SkinnedVertex),
                                GL_STATIC_DRAW);

    // SkinnedVertex 屬性佈局: position(3), normal(3), texCoord(2),
    // tangent(3), bitangent(3), joints(4), weights(4)
    const size_t stride = sizeof(SkinnedVertex);
    vertexArray.SetVertexAttribute(0, 3, stride, offsetof(SkinnedVertex, position));
    vertexArray.SetVertexAttribute(1, 3, stride, offsetof(SkinnedVertex, normal));
    vertexArray.SetVertexAttribute(2, 2, stride, offsetof(SkinnedVertex, texCoord));
    vertexArray.SetVertexAttribute(3, 3, stride, offsetof(SkinnedVertex, tangent));
    vertexArray.SetVertexAttribute(4, 3, stride, offsetof(SkinnedVertex, bitangent));
    vertexArray.SetVertexAttribute(5, 4, stride, offsetof(SkinnedVertex, joints));
    vertexArray.SetVertexAttribute(6, 4, stride, offsetof(SkinnedVertex, weights));
}

void SkinnedMesh::SetIndices(const std::vector<uint32>& inds) {
    indices = inds;
    vertexArray.AddIndexBuffer(indices.data(), indices.size(), GL_STATIC_DRAW);
}

void SkinnedMesh::Bind() const {
    vertexArray.Bind();
}

void SkinnedMesh::Unbind() const {
    vertexArray.Unbind();
}

void SkinnedMesh::Draw() const {
    if (indices.empty() && vertices.empty()) return;
    vertexArray.Bind();
    if (!vertexArray.IsUploaded()) return; // headless/未上傳：不繪製
    if (!indices.empty()) {
        glDrawElements(GL_TRIANGLES, static_cast<int>(indices.size()),
                       GL_UNSIGNED_INT, 0);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(vertices.size()));
    }
    vertexArray.Unbind();
}

void SkinnedMesh::DrawInstanced(int instanceCount) const {
    if (instanceCount <= 0 || (indices.empty() && vertices.empty())) return;
    vertexArray.Bind();
    if (!vertexArray.IsUploaded()) return;
    if (!indices.empty()) {
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<int>(indices.size()),
                                GL_UNSIGNED_INT, 0, instanceCount);
    } else {
        glDrawArraysInstanced(GL_TRIANGLES, 0,
                              static_cast<int>(vertices.size()), instanceCount);
    }
    vertexArray.Unbind();
}

} // namespace Potato
