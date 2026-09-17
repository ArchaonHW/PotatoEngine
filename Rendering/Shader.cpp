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

const char* BuiltinShaders::SkinnedVertexShader() {
    // joint palette 上限 128——超出部分以 bind pose 頂點繪製（不會崩潰）
    return R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aTexCoord;
        layout (location = 5) in vec4 aJoints;
        layout (location = 6) in vec4 aWeights;

        out vec3 FragPos;
        out vec3 Normal;
        out vec2 TexCoord;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        uniform mat4 uJointMatrices[128];
        uniform int uJointCount;
        uniform bool uHasSkin;

        void main() {
            vec4 localPos = vec4(aPos, 1.0);
            vec3 localNormal = aNormal;

            if (uHasSkin) {
                mat4 skin = mat4(0.0);
                float wSum = 0.0;
                for (int i = 0; i < 4; ++i) {
                    int j = int(aJoints[i] + 0.5);
                    float w = aWeights[i];
                    if (w > 0.0 && j >= 0 && j < uJointCount && j < 128) {
                        skin += w * uJointMatrices[j];
                        wSum += w;
                    }
                }
                if (wSum > 0.0) {
                    // WEIGHTS_0 不保證總和為 1（截斷/匯出誤差）——歸一化
                    // 避免頂點被 wSum<1 縮向原點
                    skin = skin / wSum;
                    localPos = skin * localPos;
                    localNormal = mat3(skin) * aNormal;
                }
            }

            vec4 worldPos = model * localPos;
            FragPos = worldPos.xyz;
            Normal = mat3(transpose(inverse(model))) * localNormal;
            TexCoord = aTexCoord;
            gl_Position = projection * view * worldPos;
        }
    )";
}

const char* BuiltinShaders::SkinnedFragmentShader() {
    return R"(
        #version 330 core
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;

        uniform sampler2D texture1;
        uniform bool useTexture;
        uniform vec4 baseColorFactor;
        uniform vec3 lightPos;
        uniform vec3 viewPos;
        uniform vec3 lightColor;

        void main() {
            vec3 ambient = 0.2 * lightColor;
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float diff = max(dot(norm, lightDir), 0.0);
            vec3 diffuse = diff * lightColor;

            vec4 base = useTexture ? texture(texture1, TexCoord)
                                   : vec4(1.0);
            base *= baseColorFactor;
            vec3 result = (ambient + diffuse) * base.rgb;
            FragColor = vec4(result, base.a);
        }
    )";
}

const char* BuiltinShaders::MToonFragmentShader() {
    // VRM MToon 簡化版：lit × mix(shadeColor, lightColor, ramp(NdotL))
    return R"(
        #version 330 core
        out vec4 FragColor;

        in vec3 FragPos;
        in vec3 Normal;
        in vec2 TexCoord;

        uniform sampler2D texture1;
        uniform bool useTexture;
        uniform vec4 baseColorFactor;
        uniform vec3 shadeColor;   // MToon _ShadeColor
        uniform float shadeToony;  // MToon _ShadeToony（越大越硬邊）
        uniform vec3 lightPos;
        uniform vec3 lightColor;

        void main() {
            vec3 norm = normalize(Normal);
            vec3 lightDir = normalize(lightPos - FragPos);
            float nl = dot(norm, lightDir) * 0.5 + 0.5;

            // toon ramp：shadeToony 控制明暗交界硬度
            float edge = clamp(1.0 - shadeToony, 0.001, 1.0) * 0.5;
            float ramp = smoothstep(0.5 - edge, 0.5 + edge, nl);

            vec4 base = useTexture ? texture(texture1, TexCoord)
                                   : vec4(1.0);
            base *= baseColorFactor;
            vec3 lit = base.rgb * lightColor;
            vec3 shade = base.rgb * shadeColor;
            FragColor = vec4(mix(shade, lit, ramp), base.a);
        }
    )";
}

// ============================================================================
// ShaderCompiler 實現
// ============================================================================

bool ShaderCompiler::CompileGLSL(const std::string& source, ShaderType type, std::string& bytecode) {
    (void)type;
    // 簡化實現：直接返回源代碼
    bytecode = source;
    return true;
}

bool ShaderCompiler::CompileSPIRV(const std::string& source, ShaderType type, std::string& bytecode) {
    (void)source; (void)type; (void)bytecode;
    // 需要 Vulkan SDK 的 glslangValidator
    LOG_WARNING("SPIRV compilation not implemented yet");
    return false;
}

bool ShaderCompiler::Validate(const std::string& bytecode) {
    (void)bytecode;
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

bool AdvancedShader::LoadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc) {
    // 釋放舊的 GL 物件並重置 ID,避免重載時洩漏或留下懸垂 shader ID
    if (programID) { glDeleteProgram(programID); programID = 0; }
    if (vertexID) { glDeleteShader(vertexID); vertexID = 0; }
    if (fragmentID) { glDeleteShader(fragmentID); fragmentID = 0; }
    if (geometryID) { glDeleteShader(geometryID); geometryID = 0; }

    this->vertexSource = vertexSrc;
    this->fragmentSource = fragmentSrc;
    this->geometrySource.clear();

    uint32 vID, fID;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSrc, vID)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSrc, fID)) {
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

bool AdvancedShader::LoadFromSource(const std::string& vertexSrc, const std::string& fragmentSrc, const std::string& geometrySrc) {
    if (programID) { glDeleteProgram(programID); programID = 0; }
    if (vertexID) { glDeleteShader(vertexID); vertexID = 0; }
    if (fragmentID) { glDeleteShader(fragmentID); fragmentID = 0; }
    if (geometryID) { glDeleteShader(geometryID); geometryID = 0; }

    this->vertexSource = vertexSrc;
    this->fragmentSource = fragmentSrc;
    this->geometrySource = geometrySrc;

    uint32 vID, fID, gID;
    if (!CompileShader(GL_VERTEX_SHADER, vertexSrc, vID)) {
        return false;
    }
    if (!CompileShader(GL_FRAGMENT_SHADER, fragmentSrc, fID)) {
        glDeleteShader(vID);
        return false;
    }
    if (!CompileShader(GL_GEOMETRY_SHADER, geometrySrc, gID)) {
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
    (void)geometryPath;
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
    } else if (shaderName == "skinned") {
        return LoadFromSource(BuiltinShaders::SkinnedVertexShader(), BuiltinShaders::SkinnedFragmentShader());
    } else if (shaderName == "mtoon") {
        // MToon 共用蒙皮頂點 shader（avatar 皆為蒙皮網格）
        return LoadFromSource(BuiltinShaders::SkinnedVertexShader(), BuiltinShaders::MToonFragmentShader());
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
    glUniformMatrix4fv(uniformCache.GetUniformLocation(name), 1, GL_FALSE, matrix.m);
}

void AdvancedShader::SetIntArray(const std::string& name, const int* values, int count) {
    glUniform1iv(uniformCache.GetUniformLocation(name), count, values);
}

void AdvancedShader::SetFloatArray(const std::string& name, const float* values, int count) {
    glUniform1fv(uniformCache.GetUniformLocation(name), count, values);
}

void AdvancedShader::SetMat4Array(const std::string& name, const Matrix4* values, int count) {
    // Matrix4 為 column-major m[16]，可直接作為連續 float 陣列上傳
    if (glUniformMatrix4fv == nullptr || values == nullptr || count <= 0) {
        return; // headless 或空 palette
    }
    glUniformMatrix4fv(uniformCache.GetUniformLocation(name), count,
                       GL_FALSE, values[0].m);
}

bool AdvancedShader::Recompile() {
    if (vertexSource.empty() || fragmentSource.empty()) {
        return false;
    }
    
    // LoadFromSource 內部會清理並重置舊的 GL 物件
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

bool AdvancedShader::LinkProgram(uint32 vShader, uint32 fShader, uint32 gShader) {
    programID = glCreateProgram();
    glAttachShader(programID, vShader);
    glAttachShader(programID, fShader);
    if (gShader) {
        glAttachShader(programID, gShader);
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