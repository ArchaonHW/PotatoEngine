#include "Shader.h"
#include "Logging/Logger.h"
#include <fstream>
#include <sstream>
#include <glad/glad.h>

namespace Potato {

// ============================================================================
// BuiltinShaders 實現
// ============================================================================

const char* BuiltinShaders::BasicVertexShader() {
    return R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        void main() {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";
}

const char* BuiltinShaders::BasicFragmentShader() {
    return R"(
        #version 330 core
        out vec4 FragColor;
        
        uniform vec3 color;
        
        void main() {
            FragColor = vec4(color, 1.0);
        }
    )";
}

const char* BuiltinShaders::PhongVertexShader() {
    return R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        
        out vec3 FragPos;
        out vec3 Normal;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        void main() {
            vec4 worldPos = model * vec4(aPos, 1.0);
            FragPos = worldPos.xyz;
            
            Normal = mat3(transpose(inverse(model))) * aNormal;
            
            gl_Position = projection * view * worldPos;
        }
    )";
}

const char* BuiltinShaders::PhongFragmentShader() {
    return R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec3 FragPos;
        in vec3 Normal;
        
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform vec3 lightColor;
        uniform vec3 objectColor;
        
        void main() {
            // 環境光
            float ambientStrength = 0.1;
            vec3 ambient = ambientStrength * lightColor;
            
            // 漫反射
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;
            
            // 鏡面反射
            float specularStrength = 0.5;
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = specularStrength * spec * lightColor;
            
            vec3 result = (ambient + diffuse + specular) * objectColor;
            FragColor = vec4(result, 1.0);
        }
    )";
}

const char* BuiltinShaders::ViewSpaceVertexShader() {
    return R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        
        out VS_OUT {
            vec3 FragPos;
            vec3 Normal;
        } vs_out;
        
        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;
        
        void main() {
            vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
            mat3 normalMatrix = transpose(inverse(mat3(model)));
            vs_out.Normal = normalize(normalMatrix * aNormal);
            
            gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
        }
    )";
}

const char* BuiltinShaders::ViewSpaceFragmentShader() {
    return R"(
        #version 330 core
        out vec4 FragColor;
        
        in VS_OUT {
            vec3 FragPos;
            vec3 Normal;
        } fs_in;
        
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform vec3 lightColor;
        uniform vec3 objectColor;
        
        void main() {
            vec3 ambient = 0.1 * lightColor;
            
            vec3 norm = normalize(fs_in.Normal);
            vec3 lightDir = normalize(lightPos - fs_in.FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;
            
            vec3 viewDir = normalize(viewPos - fs_in.FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = 0.5 * spec * lightColor;
            
            vec3 result = (ambient + diffuse + specular) * objectColor;
            FragColor = vec4(result, 1.0);
        }
    )";
}

const char* BuiltinShaders::TextureVertexShader() {
    return R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aTexCoord;
        
        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;
        
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        
        void main() {
            vec4 worldPos = model * vec4(aPos, 1.0);
            FragPos = worldPos.xyz;
            
            Normal = mat3(transpose(inverse(model))) * aNormal;
            TexCoord = aTexCoord;
            
            gl_Position = projection * view * worldPos;
        }
    )";
}

const char* BuiltinShaders::TextureFragmentShader() {
    return R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;
        
        uniform sampler2D texture1;
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform vec3 lightColor;
        
        void main() {
            vec3 ambient = 0.1 * lightColor;
            
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;
            
            vec3 viewDir = normalize(viewPos - FragPos);
            vec3 reflectDir = reflect(-lightDir, norm);
            float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
            vec3 specular = 0.5 * spec * lightColor;
            
            vec3 textureColor = texture(texture1, TexCoord).rgb;
            vec3 result = (ambient + diffuse + specular) * textureColor;
            FragColor = vec4(result, 1.0);
        }
    )";
}

// ============================================================================
// ShaderCompiler 實現
// ============================================================================

bool ShaderCompiler::CompileGLSL(const std::string& source, ShaderType type, std::string& bytecode) {
    // 簡化實現：直接返回源代碼
    bytecode = source;
    return true;
}

bool ShaderCompiler::CompileSPIRV(const std::string& source, ShaderType type, std::string& bytecode) {
    // 需要 Vulkan SDK 的 glslangValidator
    LOG_WARNING("SPIRV compilation not implemented yet");
    return false;
}

bool ShaderCompiler::Validate(const std::string& bytecode) {
    // 簡化實現
    return true;
}

std::string ShaderCompiler::GetShaderTypeString(ShaderType type) {
    switch (type) {
        case ShaderType::Vertex: return "Vertex";
        case ShaderType::Fragment: return "Fragment";
        case ShaderType::Geometry: return "Geometry";
        case ShaderType::Compute: return "Compute";
        default: return "Unknown";
    }
}

// ============================================================================
// ShaderUniformCache 實現
// ============================================================================

ShaderUniformCache::ShaderUniformCache(uint32 programID)
    : programID(programID)
{
}

int ShaderUniformCache::GetUniformLocation(const std::string& name) {
    auto it = uniformLocationCache.find(name);
    if (it != uniformLocationCache.end()) {
        return it->second;
    }
    
    int location = glGetUniformLocation(programID, name.c_str());
    uniformLocationCache[name] = location;
    
    if (location == -1) {
        LOG_WARNING("Uniform '" + name + "' not found in shader");
    }
    
    return location;
}

void ShaderUniformCache::Invalidate() {
    uniformLocationCache.clear();
}

// ============================================================================
// AdvancedShader 實現
// ============================================================================

AdvancedShader::AdvancedShader()
    : programID(0)
    , vertexID(0)
    , fragmentID(0)
    , geometryID(0)
    , uniformCache(0)
{
}

AdvancedShader::~AdvancedShader() {
    if (programID) {
        glDeleteProgram(programID);
    }
    if (vertexID) {
        glDeleteShader(vertexID);
    }
    if (fragmentID) {
        glDeleteShader(fragmentID);
    }
    if (geometryID) {
        glDeleteShader(geometryID);
    }
}

bool AdvancedShader::LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource) {
    this->vertexSource = vertexSource;
    this->fragmentSource = fragmentSource;
    
    uint32 vID, fID;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSource, vID)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fID)) {
        glDeleteShader(vID);
        return false;
    }
    
    if (!LinkProgram(vID, fID)) {
        glDeleteShader(vID);
        glDeleteShader(fID);
        return false;
    }
    
    vertexID = vID;
    fragmentID = fID;
    uniformCache = ShaderUniformCache(programID);
    
    return true;
}

bool AdvancedShader::LoadFromSource(const std::string& vertexSource, const std::string& fragmentSource, const std::string& geometrySource) {
    this->vertexSource = vertexSource;
    this->fragmentSource = fragmentSource;
    this->geometrySource = geometrySource;
    
    uint32 vID, fID, gID;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSource, vID)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fID)) {
        glDeleteShader(vID);
        return false;
    }
    if (!CompileShader(GL_GEOMETRY_SHADER, geometrySource, gID)) {
        glDeleteShader(vID);
        glDeleteShader(fID);
        return false;
    }
    
    if (!LinkProgram(vID, fID, gID)) {
        glDeleteShader(vID);
        glDeleteShader(fID);
        glDeleteShader(gID);
        return false;
    }
    
    vertexID = vID;
    fragmentID = fID;
    geometryID = gID;
    uniformCache = ShaderUniformCache(programID);
    
    return true;
}

bool AdvancedShader::LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath) {
    // 簡化實現：讀取文件
    std::ifstream vFile(vertexPath);
    std::ifstream fFile(fragmentPath);
    
    if (!vFile.is_open() || !fFile.is_open()) {
        LOG_ERROR("Failed to open shader files: " + vertexPath + ", " + fragmentPath);
        return false;
    }
    
    std::stringstream vStream, fStream;
    vStream << vFile.rdbuf();
    fStream << fFile.rdbuf();
    
    return LoadFromSource(vStream.str(), fStream.str());
}

bool AdvancedShader::LoadFromFile(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath) {
    // 簡化實現
    LOG_WARNING("Geometry shader loading from file not implemented yet");
    return LoadFromFile(vertexPath, fragmentPath);
}

bool AdvancedShader::LoadBuiltin(const std::string& shaderName) {
    if (shaderName == "basic") {
        return LoadFromSource(BuiltinShaders::BasicVertexShader(), BuiltinShaders::BasicFragmentShader());
    } else if (shaderName == "phong") {
        return LoadFromSource(BuiltinShaders::PhongVertexShader(), BuiltinShaders::PhongFragmentShader());
    } else if (shaderName == "view_space") {
        return LoadFromSource(BuiltinShaders::ViewSpaceVertexShader(), BuiltinShaders::ViewSpaceFragmentShader());
    } else if (shaderName == "texture") {
        return LoadFromSource(BuiltinShaders::TextureVertexShader(), BuiltinShaders::TextureFragmentShader());
    }
    
    LOG_ERROR("Unknown builtin shader: " + shaderName);
    return false;
}

void AdvancedShader::Bind() const {
    glUseProgram(programID);
}

void AdvancedShader::Unbind() const {
    glUseProgram(0);
}

void AdvancedShader::SetBool(const std::string& name, bool value) {
    glUniform1i(uniformCache.GetUniformLocation(name), static_cast<int>(value));
}

void AdvancedShader::SetInt(const std::string& name, int value) {
    glUniform1i(uniformCache.GetUniformLocation(name), value);
}

void AdvancedShader::SetFloat(const std::string& name, float value) {
    glUniform1f(uniformCache.GetUniformLocation(name), value);
}

void AdvancedShader::SetVec2(const std::string& name, const Vector2& value) {
    glUniform2f(uniformCache.GetUniformLocation(name), value.x, value.y);
}

void AdvancedShader::SetVec3(const std::string& name, const Vector3& value) {
    glUniform3f(uniformCache.GetUniformLocation(name), value.x, value.y, value.z);
}

void AdvancedShader::SetVec4(const std::string& name, float x, float y, float z, float w) {
    glUniform4f(uniformCache.GetUniformLocation(name), x, y, z, w);
}

void AdvancedShader::SetMat2(const std::string& name, const float* matrix) {
    glUniformMatrix2fv(uniformCache.GetUniformLocation(name), 1, GL_FALSE, matrix);
}

void AdvancedShader::SetMat3(const std::string& name, const float* matrix) {
    glUniformMatrix3fv(uniformCache.GetUniformLocation(name), 1, GL_FALSE, matrix);
}

void AdvancedShader::SetMat4(const std::string& name, const Matrix4& matrix) {
    glUniformMatrix4fv(uniformCache.GetUniformLocation(name), 1, GL_FALSE, matrix.data);
}

void AdvancedShader::SetIntArray(const std::string& name, const int* values, int count) {
    glUniform1iv(uniformCache.GetUniformLocation(name), count, values);
}

void AdvancedShader::SetFloatArray(const std::string& name, const float* values, int count) {
    glUniform1fv(uniformCache.GetUniformLocation(name), count, values);
}

bool AdvancedShader::Recompile() {
    if (vertexSource.empty() || fragmentSource.empty()) {
        return false;
    }
    
    // 清理舊的程序
    if (programID) {
        glDeleteProgram(programID);
    }
    if (vertexID) {
        glDeleteShader(vertexID);
    }
    if (fragmentID) {
        glDeleteShader(fragmentID);
    }
    if (geometryID) {
        glDeleteShader(geometryID);
    }
    
    // 重新編譯
    if (geometrySource.empty()) {
        return LoadFromSource(vertexSource, fragmentSource);
    } else {
        return LoadFromSource(vertexSource, fragmentSource, geometrySource);
    }
}

bool AdvancedShader::CompileShader(uint32 type, const std::string& source, uint32& shaderID) {
    shaderID = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shaderID, 1, &src, nullptr);
    glCompileShader(shaderID);
    
    CheckCompileErrors(shaderID, static_cast<ShaderType>(type));
    
    int success;
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
    return success == GL_TRUE;
}

bool AdvancedShader::LinkProgram(uint32 vertexID, uint32 fragmentID, uint32 geometryID) {
    programID = glCreateProgram();
    glAttachShader(programID, vertexID);
    glAttachShader(programID, fragmentID);
    if (geometryID) {
        glAttachShader(programID, geometryID);
    }
    glLinkProgram(programID);
    
    CheckLinkErrors(programID);
    
    int success;
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    return success == GL_TRUE;
}

void AdvancedShader::CheckCompileErrors(uint32 shader, ShaderType type) {
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::string typeStr = ShaderCompiler::GetShaderTypeString(type);
        LOG_ERROR("Shader compilation error (" + typeStr + "): " + std::string(infoLog));
    }
}

void AdvancedShader::CheckLinkErrors(uint32 program) {
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        LOG_ERROR("Shader program linking error: " + std::string(infoLog));
    }
}

// ============================================================================
// ShaderManager 實現
// ============================================================================

ShaderManager::ShaderManager() {
}

ShaderManager::~ShaderManager() {
    UnloadAll();
}

ShaderManager& ShaderManager::GetInstance() {
    static ShaderManager instance;
    return instance;
}

AdvancedShader* ShaderManager::LoadShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath) {
    auto shader = MakeUnique<AdvancedShader>();
    if (shader->LoadFromFile(vertexPath, fragmentPath)) {
        shaders[name] = std::move(shader);
        LOG_INFO("Loaded shader: " + name);
        return shaders[name].get();
    }
    
    LOG_ERROR("Failed to load shader: " + name);
    return nullptr;
}

AdvancedShader* ShaderManager::GetShader(const std::string& name) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ShaderManager::UnloadShader(const std::string& name) {
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        shaders.erase(it);
        LOG_INFO("Unloaded shader: " + name);
    }
}

void ShaderManager::UnloadAll() {
    shaders.clear();
    LOG_INFO("Unloaded all shaders");
}

void ShaderManager::PreloadCommonShaders() {
    LOG_INFO("Preloading common shaders...");
    
    auto basicShader = MakeUnique<AdvancedShader>();
    if (basicShader->LoadBuiltin("basic")) {
        shaders["basic"] = std::move(basicShader);
    }
    
    auto phongShader = MakeUnique<AdvancedShader>();
    if (phongShader->LoadBuiltin("phong")) {
        shaders["phong"] = std::move(phongShader);
    }
    
    auto textureShader = MakeUnique<AdvancedShader>();
    if (textureShader->LoadBuiltin("texture")) {
        shaders["texture"] = std::move(textureShader);
    }
    
    LOG_INFO("Common shaders preloaded");
}

} // namespace Potato