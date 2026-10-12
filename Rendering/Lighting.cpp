#include "Lighting.h"
#include "Rendering/DeferredShading.h"
#include "Logging/Logger.h"
#include <algorithm>
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

void SpotLight::SetCutoff(float newCutoff) {
    this->cutoff = newCutoff;
}

void SpotLight::SetOuterCutoff(float newOuterCutoff) {
    this->outerCutoff = newOuterCutoff;
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
    (void)viewDir;
    (void)roughness;
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
    (void)path;
    LOG_WARNING("Environment map loading not implemented yet");
}

void LightingManager::SetIrradianceMap(const std::string& path) {
    (void)path;
    LOG_WARNING("Irradiance map loading not implemented yet");
}

void LightingManager::SetPrefilterMap(const std::string& path) {
    (void)path;
    LOG_WARNING("Prefilter map loading not implemented yet");
}

void LightingManager::SetBRDFLUT(const std::string& path) {
    (void)path;
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
// Deferred 管線接軌
// ============================================================================

float LightingManager::AttenuationToRadius(float constant, float linear,
                                           float quadratic, float cutoff) {
    // 1/(c + l d + q d²) = cutoff → q d² + l d + (c - K) = 0，K = 1/cutoff
    const float K = 1.0f / cutoff;
    if (constant >= K) {
        return 0.0f; // 初始亮度已低於 cutoff——半徑零
    }
    if (quadratic > 1e-6f) {
        const float disc = linear * linear - 4.0f * quadratic * (constant - K);
        // disc > l² 恒成立（constant < K）→ 正根必為正
        return (-linear + std::sqrt(disc)) / (2.0f * quadratic);
    }
    if (linear > 1e-6f) {
        return (K - constant) / linear;
    }
    return 1.0e4f; // 無衰減——給足夠大的半徑（平滑窗口內近乎等強度）
}

void LightingManager::GatherDeferredLights(
    std::vector<DeferredDirLight>& dirs,
    std::vector<DeferredPointLight>& points,
    std::vector<DeferredSpotLight>& spots) const {
    for (const auto& [name, light] : lights) {
        if (!light->IsEnabled()) continue;
        switch (light->GetType()) {
            case LightType::Directional: {
                const auto* dl =
                    static_cast<const DirectionalLight*>(light.get());
                DeferredDirLight d;
                d.direction = dl->GetDirection();
                d.color = dl->GetColor();
                d.intensity = dl->GetIntensity();
                d.castShadow = dl->IsCastShadow();
                dirs.push_back(d);
                break;
            }
            case LightType::Point: {
                const auto* pl =
                    static_cast<const PointLight*>(light.get());
                DeferredPointLight p;
                p.position = pl->GetPosition();
                p.color = pl->GetColor();
                p.intensity = pl->GetIntensity();
                p.radius = AttenuationToRadius(pl->GetConstantAttenuation(),
                                               pl->GetLinearAttenuation(),
                                               pl->GetQuadraticAttenuation());
                p.castShadow = pl->IsCastShadow();
                points.push_back(p);
                break;
            }
            case LightType::Spot: {
                const auto* sl =
                    static_cast<const SpotLight*>(light.get());
                DeferredSpotLight s;
                s.position = sl->GetPosition();
                s.direction = sl->GetDirection();
                s.color = sl->GetColor();
                s.intensity = sl->GetIntensity();
                s.radius = AttenuationToRadius(sl->GetConstantAttenuation(),
                                               sl->GetLinearAttenuation(),
                                               sl->GetQuadraticAttenuation());
                // 錐角（度）→ cos 半角；較小角為 inner，夾到 [0,90)
                const float kDeg = 3.14159265f / 180.0f;
                const float a = std::min(sl->GetCutoff(),
                                         sl->GetOuterCutoff());
                const float b = std::max(sl->GetCutoff(),
                                         sl->GetOuterCutoff());
                s.innerCos = std::cos(
                    std::min(std::max(a, 0.0f), 89.0f) * kDeg);
                s.outerCos = std::cos(
                    std::min(std::max(b, 0.0f), 89.0f) * kDeg);
                s.castShadow = sl->IsCastShadow();
                spots.push_back(s);
                break;
            }
            case LightType::Ambient:
                break; // ambient 走 GetDeferredAmbient
        }
    }
}

float LightingManager::GetDeferredAmbient() const {
    const Vector3 a = ambientColor * ambientIntensity;
    // Rec.709 亮度——deferred 的 ambient 是單一純量
    return a.x * 0.2126f + a.y * 0.7152f + a.z * 0.0722f;
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