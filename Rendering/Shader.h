#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector3.h"
#include "Math/Matrix4.h"
#include <string>
#include <unordered_map>

namespace Potato {

/**
 * 著色器類型
 */
enum class ShaderType {
    Vertex,
    Fragment,
    Geometry,
    Compute
};

/**
 * 內建著色器庫
 */
class BuiltinShaders {
public:
    // 基本頂點著色器
    static const char* BasicVertexShader();
    
    // 基本片段著色器
    static const char* BasicFragmentShader();
    
    // Phong 光照頂點著色器
    static const char* PhongVertexShader();
    
    // Phong 光照片段著色器
    static const char* PhongFragmentShader();
    
    // 視線空間頂點著色器
    static const char* ViewSpaceVertexShader();
    
    // 視線空間片段著色器
    static const char* ViewSpaceFragmentShader();
    
    // 紋理頂點著色器
    static const char* TextureVertexShader();
    
    // 紋理片段著色器
    static const char* TextureFragmentShader();
};

/**
 * 著色器編譯器
 */
class ShaderCompiler {
public:
    static bool CompileGLSL(const std::string& source, ShaderType type, std::string& bytecode);
    static bool CompileSPIRV(const std::string& source, ShaderType type, std::string& bytecode);
    static bool Validate(const std::string& bytecode);
    
    static std::string GetShaderTypeString(ShaderType type);
};

/**
 * 著色器統一變量位置緩存
 */
class ShaderUniformCache {
public:
    ShaderUniformCache(uint32 programID);
    
    int GetUniformLocation(const std::string& name);
    void Invalidate();
    
private:
    uint32 programID;
    std::unordered_map<std::string, int> uniformLocationCache;
};

/**
 * 高級著色器類
 */
class AdvancedShader {
public:
    AdvancedShader();
    ~AdvancedShader();
    
    // 從源代碼加載
    bool LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource);
    bool LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource, const std::string& geometrySource);
    
    // 從文件加載
    bool LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath);
    bool LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath);
    
    // 使用內建著色器
    bool LoadBuiltin(const std::string& shaderName);
    
    // 綁定和解除綁定
    void Bind() const;
    void Unbind() const;
    
    // 統一變量設置
    void SetBool(const std::string& name, bool value);
    void SetInt(const std::string& name, int value);
    void SetFloat(const std::string& name, float value);
    void SetVec2(const std::string& name, const Vector2& value);
    void SetVec3(const std::string& name, const Vector3& value);
    void SetVec4(const std::string& name, float x, float y, float z, float w);
    void SetMat2(const std::string& name, const float* matrix);
    void SetMat3(const std::string& name, const float* matrix);
    void SetMat4(const std::string& name, const Matrix4& matrix);
    
    // 統一變量數組設置
    void SetIntArray(const std::string& name, const int* values, int count);
    void SetFloatArray(const std::string& name, const float* values, int count);
    
    // 獲取程序 ID
    uint32 GetProgramID() const { return programID; }
    
    // 驗證著色器
    bool IsValid() const { return programID != 0; }
    
    // 重新編譯
    bool Recompile();
    
private:
    bool CompileShader(uint32 type, const std::string& source, uint32& shaderID);
    bool LinkProgram(uint32 vertexID, uint32 fragmentID, uint32 geometryID = 0);
    void CheckCompileErrors(uint32 shader, ShaderType type);
    void CheckLinkErrors(uint32 program);
    
private:
    uint32 programID;
    uint32 vertexID;
    uint32 fragmentID;
    uint32 geometryID;
    
    ShaderUniformCache uniformCache;
    
    std::string vertexSource;
    std::string fragmentSource;
    std::string geometrySource;
};

/**
 * 著色器管理器
 */
class ShaderManager {
public:
    static ShaderManager& GetInstance();
    
    // 加載著色器
    AdvancedShader* LoadShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);
    AdvancedShader* GetShader(const std::string& name);
    
    // 卸載著色器
    void UnloadShader(const std::string& name);
    void UnloadAll();
    
    // 預加載常用著色器
    void PreloadCommonShaders();
    
private:
    ShaderManager();
    ~ShaderManager();
    
    std::unordered_map<std::string, UniquePtr<AdvancedShader>> shaders;
};

} // namespace Potato