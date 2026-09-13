#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Matrix4.h"
#include "Rendering/Shader.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Potato {

/**
 * 光源類型
 */
enum class LightType {
    Directional,
    Point,
    Spot,
    Ambient
};

/**
 * 光源基類
 */
class Light {
public:
    Light();
    virtual ~Light() = default;
    
    LightType GetType() const { return type; }
    
    void SetColor(const Vector3& color) { this->color = color; }
    Vector3 GetColor() const { return color; }
    
    void SetIntensity(float intensity) { this->intensity = intensity; }
    float GetIntensity() const { return intensity; }
    
    void SetEnabled(bool enabled) { this->enabled = enabled; }
    bool IsEnabled() const { return enabled; }
    
    virtual void ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) = 0;
    
protected:
    LightType type;
    Vector3 color;
    float intensity;
    bool enabled;
};

/**
 * 方向光（如太陽光）
 */
class DirectionalLight : public Light {
public:
    DirectionalLight();
    
    void SetDirection(const Vector3& direction);
    Vector3 GetDirection() const { return direction; }
    
    void ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) override;
    
private:
    Vector3 direction;
};

/**
 * 點光源
 */
class PointLight : public Light {
public:
    PointLight();
    
    void SetPosition(const Vector3& position);
    Vector3 GetPosition() const { return position; }
    
    void SetAttenuation(float constant, float linear, float quadratic);
    float GetConstantAttenuation() const { return constant; }
    float GetLinearAttenuation() const { return linear; }
    float GetQuadraticAttenuation() const { return quadratic; }
    
    void ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) override;
    
private:
    Vector3 position;
    float constant;
    float linear;
    float quadratic;
};

/**
 * 聚光燈
 */
class SpotLight : public PointLight {
public:
    SpotLight();
    
    void SetDirection(const Vector3& direction);
    Vector3 GetDirection() const { return direction; }
    
    void SetCutoff(float cutoff); // 弧度
    float GetCutoff() const { return cutoff; }
    
    void SetOuterCutoff(float outerCutoff); // 弧度
    float GetOuterCutoff() const { return outerCutoff; }
    
    void ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) override;
    
private:
    Vector3 direction;
    float cutoff;
    float outerCutoff;
};

/**
 * 環境光
 */
class AmbientLight : public Light {
public:
    AmbientLight();
    
    void ApplyToShader(const std::string& uniformPrefix, AdvancedShader& shader) override;
};

/**
 * 光照計算類
 */
class LightingCalculator {
public:
    static Vector3 CalculatePhong(
        const Vector3& normal,
        const Vector3& lightDir,
        const Vector3& viewDir,
        const Vector3& lightColor,
        const Vector3& objectColor,
        float shininess
    );
    
    static Vector3 CalculateBlinnPhong(
        const Vector3& normal,
        const Vector3& lightDir,
        const Vector3& viewDir,
        const Vector3& lightColor,
        const Vector3& objectColor,
        float shininess
    );
    
    static Vector3 CalculatePBR(
        const Vector3& normal,
        const Vector3& lightDir,
        const Vector3& viewDir,
        const Vector3& albedo,
        float metallic,
        float roughness
    );
};

/**
 * 光照管理器
 */
class LightingManager {
public:
    static LightingManager& GetInstance();
    
    // 光源管理
    DirectionalLight* CreateDirectionalLight(const std::string& name);
    PointLight* CreatePointLight(const std::string& name);
    SpotLight* CreateSpotLight(const std::string& name);
    AmbientLight* CreateAmbientLight(const std::string& name);
    
    Light* GetLight(const std::string& name);
    void DestroyLight(const std::string& name);
    void DestroyAllLights();
    
    // 光源應用
    void ApplyLights(AdvancedShader& shader);
    
    // 全局光照設置
    void SetAmbientColor(const Vector3& color);
    Vector3 GetAmbientColor() const { return ambientColor; }
    
    void SetAmbientIntensity(float intensity);
    float GetAmbientIntensity() const { return ambientIntensity; }
    
    // 環境貼圖
    void SetEnvironmentMap(const std::string& path);
    void SetIrradianceMap(const std::string& path);
    void SetPrefilterMap(const std::string& path);
    void SetBRDFLUT(const std::string& path);
    
    // 陰影設置
    void EnableShadows(bool enable);
    bool AreShadowsEnabled() const { return shadowsEnabled; }
    
    void SetShadowMapSize(int size);
    int GetShadowMapSize() const { return shadowMapSize; }
    
private:
    LightingManager();
    ~LightingManager();
    
    std::unordered_map<std::string, UniquePtr<Light>> lights;
    
    Vector3 ambientColor;
    float ambientIntensity;
    
    bool shadowsEnabled;
    int shadowMapSize;
    
    // 環境貼圖（簡化實現）
    uint32 environmentMap;
    uint32 irradianceMap;
    uint32 prefilterMap;
    uint32 brdfLUT;
};

/**
 * 材質系統
 */
class Material {
public:
    Material();
    
    void SetAlbedo(const Vector3& albedo) { this->albedo = albedo; }
    Vector3 GetAlbedo() const { return albedo; }
    
    void SetMetallic(float metallic) { this->metallic = metallic; }
    float GetMetallic() const { return metallic; }
    
    void SetRoughness(float roughness) { this->roughness = roughness; }
    float GetRoughness() const { return roughness; }
    
    void SetAO(float ao) { this->ao = ao; }
    float GetAO() const { return ao; }
    
    void ApplyToShader(AdvancedShader& shader) const;
    
private:
    Vector3 albedo;
    float metallic;
    float roughness;
    float ao;
    
    // 紋理貼圖（簡化實現）
    uint32 albedoMap;
    uint32 normalMap;
    uint32 metallicMap;
    uint32 roughnessMap;
    uint32 aoMap;
};

} // namespace Potato