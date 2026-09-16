#include "Lighting.h"
#include "Logging/Logger.h"
#include <cmath>

namespace Potato {

// ============================================================================
// Light 實現
// ============================================================================

Light::Light()
    : type(LightType::Ambient)
    , color(Vector3(1.0f, 1.0f, 1.0f))
    , intensity(1.0f)
    , enabled(true)
{
}

// ============================================================================
// DirectionalLight 實現
// ============================================================================

DirectionalLight::DirectionalLight()
    : Light()
    , direction(Vector3(0.0f, -1.0f, 0.0f))
{
    type = LightType::Directional;
}

void DirectionalLight::SetDirection(const Vector3& dir) {
    direction = dir.Normalized();
}

void DirectionalLight::ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) {
    if (!enabled) return;
    
    std::string colorPrefix = uniformPrefix + ".color";
    std::string dirPrefix = uniformPrefix + ".direction";
    std::string intensityPrefix = uniformPrefix + ".intensity";
    
    shader.SetVec3(colorPrefix, color * intensity);
    shader.SetVec3(dirPrefix, direction);
    shader.SetFloat(intensityPrefix, intensity);
}

// ============================================================================
// PointLight 實現
// ============================================================================

PointLight::PointLight()
    : Light()
    , position(Vector3(0.0f, 0.0f, 0.0f))
    , constant(1.0f)
    , linear(0.09f)
    , quadratic(0.032f)
{
    type = LightType::Point;
}

void PointLight::SetPosition(const Vector3& pos) {
    position = pos;
}

void PointLight::SetAttenuation(float c, float l, float q) {
    constant = c;
    linear = l;
    quadratic = q;
}

void PointLight::ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) {
    if (!enabled) return;
    
    std::string posPrefix = uniformPrefix + ".position";
    std::string colorPrefix = uniformPrefix + ".color";
    std::string intensityPrefix = uniformPrefix + ".intensity";
    std::string constantPrefix = uniformPrefix + ".constant";
    std::string linearPrefix = uniformPrefix + ".linear";
    std::string quadraticPrefix = uniformPrefix + ".quadratic";
    
    shader.SetVec3(posPrefix, position);
    shader.SetVec3(colorPrefix, color * intensity);
    shader.SetFloat(intensityPrefix, intensity);
    shader.SetFloat(constantPrefix, constant);
    shader.SetFloat(linearPrefix, linear);
    shader.SetFloat(quadraticPrefix, quadratic);
}

// ============================================================================
// SpotLight 實現
// ============================================================================

SpotLight::SpotLight()
    : PointLight()
    , direction(Vector3(0.0f, -1.0f, 0.0f))
    , cutoff(12.5f)
    , outerCutoff(17.5f)
{
    type = LightType::Spot;
}

void SpotLight::SetDirection(const Vector3& dir) {
    direction = dir.Normalized();
}

void SpotLight::SetCutoff(float cutoff) {
    this->cutoff = cutoff;
}

void SpotLight::SetOuterCutoff(float outerCutoff) {
    this->outerCutoff = outerCutoff;
}

void SpotLight::ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) {
    if (!enabled) return;
    
    // 首先應用點光源的屬性
    PointLight::ApplyToShader(uniformPrefix, shader);
    
    // 添加聚光燈特定屬性
    std::string dirPrefix = uniformPrefix + ".direction";
    std::string cutoffPrefix = uniformPrefix + ".cutoff";
    std::string outerCutoffPrefix = uniformPrefix + ".outerCutoff";
    
    shader.SetVec3(dirPrefix, direction);
    shader.SetFloat(cutoffPrefix, std::cos(cutoff * 3.14159f / 180.0f));
    shader.SetFloat(outerCutoffPrefix, std::cos(outerCutoff * 3.14159f / 180.0f));
}

// ============================================================================
// AmbientLight 實現
// ============================================================================

AmbientLight::AmbientLight()
    : Light()
{
    type = LightType::Ambient;
}

void AmbientLight::ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) {
    if (!enabled) return;
    
    std::string colorPrefix = uniformPrefix + ".color";
    std::string intensityPrefix = uniformPrefix + ".intensity";
    
    shader.SetVec3(colorPrefix, color * intensity);
    shader.SetFloat(intensityPrefix, intensity);
}

// ============================================================================
// LightingCalculator 實現
// ============================================================================

Vector3 LightingCalculator::CalculatePhong(
    const Vector3& normal,
    const Vector3& lightDir,
    const Vector3& viewDir,
    const Vector3& lightColor,
    const Vector3& objectColor,
    float shininess
) {
    // 環境光
    Vector3 ambient = 0.1f * lightColor;
    
    // 漫反射
    float diff = std::max(Vector3::Dot(normal, lightDir), 0.0f);
    Vector3 diffuse = diff * lightColor;
    
    // 鏡面反射
    Vector3 reflectDir = Vector3::Reflect(-lightDir, normal);
    float spec = std::pow(std::max(Vector3::Dot(viewDir, reflectDir), 0.0f), shininess);
    Vector3 specular = 0.5f * spec * lightColor;
    
    return (ambient + diffuse + specular) * objectColor;
}

Vector3 LightingCalculator::CalculateBlinnPhong(
    const Vector3& normal,
    const Vector3& lightDir,
    const Vector3& viewDir,
    const Vector3& lightColor,
    const Vector3& objectColor,
    float shininess
) {
    // 環境光
    Vector3 ambient = 0.1f * lightColor;
    
    // 漫反射
    float diff = std::max(Vector3::Dot(normal, lightDir), 0.0f);
    Vector3 diffuse = diff * lightColor;
    
    // Blinn-Phong 鏡面反射
    Vector3 halfwayDir = Vector3::Normalize(lightDir + viewDir);
    float spec = std::pow(std::max(Vector3::Dot(normal, halfwayDir), 0.0f), shininess);
    Vector3 specular = 0.5f * spec * lightColor;
    
    return (ambient + diffuse + specular) * objectColor;
}

Vector3 LightingCalculator::CalculatePBR(
    const Vector3& normal,
    const Vector3& lightDir,
    const Vector3& viewDir,
    const Vector3& albedo,
    float metallic,
    float roughness
) {
    // 簡化 PBR 實現
    Vector3 ambient = 0.1f * albedo;
    
    float diff = std::max(Vector3::Dot(normal, lightDir), 0.0f);
    Vector3 diffuse = diff * albedo * (1.0f - metallic);
    
    Vector3 specular = Vector3(0.0f, 0.0f, 0.0f); // 簡化
    
    return ambient + diffuse + specular;
}

// ============================================================================
// LightingManager 實現
// ============================================================================

LightingManager::LightingManager()
    : ambientColor(Vector3(0.1f, 0.1f, 0.1f))
    , ambientIntensity(1.0f)
    , shadowsEnabled(false)
    , shadowMapSize(1024)
    , environmentMap(0)
    , irradianceMap(0)
    , prefilterMap(0)
    , brdfLUT(0)
{
}

LightingManager::~LightingManager() {
    DestroyAllLights();
}

LightingManager& LightingManager::GetInstance() {
    static LightingManager instance;
    return instance;
}

DirectionalLight* LightingManager::CreateDirectionalLight(const std::string& name) {
    auto light = MakeUnique<DirectionalLight>();
    DirectionalLight* lightPtr = light.get();
    lights[name] = std::move(light);
    LOG_INFO("Created directional light: " + name);
    return lightPtr;
}

PointLight* LightingManager::CreatePointLight(const std::string& name) {
    auto light = MakeUnique<PointLight>();
    PointLight* lightPtr = light.get();
    lights[name] = std::move(light);
    LOG_INFO("Created point light: " + name);
    return lightPtr;
}

SpotLight* LightingManager::CreateSpotLight(const std::string& name) {
    auto light = MakeUnique<SpotLight>();
    SpotLight* lightPtr = light.get();
    lights[name] = std::move(light);
    LOG_INFO("Created spot light: " + name);
    return lightPtr;
}

AmbientLight* LightingManager::CreateAmbientLight(const std::string& name) {
    auto light = MakeUnique<AmbientLight>();
    AmbientLight* lightPtr = light.get();
    lights[name] = std::move(light);
    LOG_INFO("Created ambient light: " + name);
    return lightPtr;
}

Light* LightingManager::GetLight(const std::string& name) {
    auto it = lights.find(name);
    if (it != lights.end()) {
        return it->second.get();
    }
    return nullptr;
}

void LightingManager::DestroyLight(const std::string& name) {
    auto it = lights.find(name);
    if (it != lights.end()) {
        lights.erase(it);
        LOG_INFO("Destroyed light: " + name);
    }
}

void LightingManager::DestroyAllLights() {
    lights.clear();
    LOG_INFO("Destroyed all lights");
}

void LightingManager::ApplyLights(AdvancedShader& shader) {
    int dirLightCount = 0;
    int pointLightCount = 0;
    int spotLightCount = 0;
    
    for (const auto& [name, light] : lights) {
        if (!light->IsEnabled()) continue;
        
        std::string prefix;
        int index = 0;
        
        switch (light->GetType()) {
            case LightType::Directional:
                prefix = "dirLights[" + std::to_string(dirLightCount) + "]";
                index = dirLightCount++;
                break;
            case LightType::Point:
                prefix = "pointLights[" + std::to_string(pointLightCount) + "]";
                index = pointLightCount++;
                break;
            case LightType::Spot:
                prefix = "spotLights[" + std::to_string(spotLightCount) + "]";
                index = spotLightCount++;
                break;
            case LightType::Ambient:
                prefix = "ambientLight";
                break;
        }
        
        light->ApplyToShader(prefix, shader);
    }
    
    // 設置光源數量
    shader.SetInt("dirLightCount", dirLightCount);
    shader.SetInt("pointLightCount", pointLightCount);
    shader.SetInt("spotLightCount", spotLightCount);
    
    // 設置全局環境光
    shader.SetVec3("ambientColor", ambientColor * ambientIntensity);
}

void LightingManager::SetAmbientColor(const Vector3& color) {
    ambientColor = color;
}

void LightingManager::SetAmbientIntensity(float intensity) {
    ambientIntensity = intensity;
}

void LightingManager::SetEnvironmentMap(const std::string& path) {
    LOG_WARNING("Environment map loading not implemented yet");
}

void LightingManager::SetIrradianceMap(const std::string& path) {
    LOG_WARNING("Irradiance map loading not implemented yet");
}

void LightingManager::SetPrefilterMap(const std::string& path) {
    LOG_WARNING("Prefilter map loading not implemented yet");
}

void LightingManager::SetBRDFLUT(const std::string& path) {
    LOG_WARNING("BRDF LUT loading not implemented yet");
}

void LightingManager::EnableShadows(bool enable) {
    shadowsEnabled = enable;
    LOG_INFO("Shadows " + std::string(enable ? "enabled" : "disabled"));
}

void LightingManager::SetShadowMapSize(int size) {
    shadowMapSize = size;
}

// ============================================================================
// Material 實現
// ============================================================================

Material::Material()
    : albedo(Vector3(1.0f, 1.0f, 1.0f))
    , metallic(0.0f)
    , roughness(0.5f)
    , ao(1.0f)
    , albedoMap(0)
    , normalMap(0)
    , metallicMap(0)
    , roughnessMap(0)
    , aoMap(0)
{
}

void Material::ApplyToShader(AdvancedShader& shader) const {
    shader.SetVec3("albedo", albedo);
    shader.SetFloat("metallic", metallic);
    shader.SetFloat("roughness", roughness);
    shader.SetFloat("ao", ao);
}

} // namespace Potato