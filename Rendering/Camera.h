#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Quaternion.h"

namespace Potato {

/**
 * 相機投影類型
 */
enum class CameraProjection {
    Perspective,
    Orthographic
};

/**
 * 相機類
 */
class Camera {
public:
    Camera();
    
    // 位置和方向
    void SetPosition(const Vector3& position);
    void SetRotation(const Quaternion& rotation);
    void SetTarget(const Vector3& target);
    
    Vector3 GetPosition() const { return position; }
    Quaternion GetRotation() const { return rotation; }
    Vector3 GetTarget() const { return target; }
    
    // 相機移動
    void Move(const Vector3& delta);
    void Rotate(float yaw, float pitch);
    void Zoom(float delta);
    
    // 投影設置
    void SetProjection(CameraProjection projection);
    void SetPerspective(float fov, float aspect, float nearPlane, float farPlane);
    void SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
    
    // 視錐體設置
    void SetFOV(float fov) { this->fov = fov; updateProjection = true; }
    void SetAspectRatio(float aspect) { this->aspect = aspect; updateProjection = true; }
    void SetNearPlane(float nearPlane) { this->nearPlane = nearPlane; updateProjection = true; }
    void SetFarPlane(float farPlane) { this->farPlane = farPlane; updateProjection = true; }
    
    float GetFOV() const { return fov; }
    float GetAspectRatio() const { return aspect; }
    float GetNearPlane() const { return nearPlane; }
    float GetFarPlane() const { return farPlane; }
    
    // 獲取矩陣
    Matrix4 GetViewMatrix() const;
    Matrix4 GetProjectionMatrix() const;
    Matrix4 GetViewProjectionMatrix() const;
    
    // 相機方向
    Vector3 GetForward() const;
    Vector3 GetRight() const;
    Vector3 GetUp() const;
    
    // 視口設置
    void SetViewport(int x, int y, int width, int height);
    void GetViewport(int& x, int& y, int& width, int& height) const;
    
    // 速度控制
    void SetMovementSpeed(float speed) { movementSpeed = speed; }
    void SetRotationSpeed(float speed) { rotationSpeed = speed; }
    void SetZoomSpeed(float speed) { zoomSpeed = speed; }
    
    float GetMovementSpeed() const { return movementSpeed; }
    float GetRotationSpeed() const { return rotationSpeed; }
    float GetZoomSpeed() const { return zoomSpeed; }
    
    // 更新
    void Update(float deltaTime);
    
private:
    void UpdateVectors();
    void UpdateProjectionMatrix();
    
private:
    Vector3 position;
    Quaternion rotation;
    Vector3 target;
    Vector3 forward;
    Vector3 right;
    Vector3 up;
    
    CameraProjection projectionType;
    float fov;
    float aspect;
    float nearPlane;
    float farPlane;
    
    // 正交投影參數
    float orthoLeft;
    float orthoRight;
    float orthoBottom;
    float orthoTop;
    
    Matrix4 viewMatrix;
    Matrix4 projectionMatrix;
    
    int viewportX, viewportY, viewportWidth, viewportHeight;
    
    float movementSpeed;
    float rotationSpeed;
    float zoomSpeed;
    
    bool updateView;
    bool updateProjection;
};

/**
 * FPS 相機控制器
 */
class FPSCameraController {
public:
    FPSCameraController();
    
    void SetCamera(Camera* camera) { this->camera = camera; }
    Camera* GetCamera() const { return camera; }
    
    void SetSensitivity(float sensitivity) { this->sensitivity = sensitivity; }
    float GetSensitivity() const { return sensitivity; }
    
    void Update(float deltaTime);
    
    // 輸入處理
    void OnMouseMove(float deltaX, float deltaY);
    void OnKeyDown(int key);
    void OnKeyUp(int key);
    void OnMouseScroll(float delta);
    
private:
    Camera* camera;
    float sensitivity;
    
    bool moveForward;
    bool moveBackward;
    bool moveLeft;
    bool moveRight;
    bool moveUp;
    bool moveDown;
    
    Vector2 mouseDelta;
    float scrollDelta;
};

/**
 * 軌道相機控制器
 */
class OrbitCameraController {
public:
    OrbitCameraController();
    
    void SetCamera(Camera* camera) { this->camera = camera; }
    void SetTarget(const Vector3& target) { this->target = target; }
    void SetDistance(float distance) { this->distance = distance; }
    
    Vector3 GetTarget() const { return target; }
    float GetDistance() const { return distance; }
    
    void SetMinDistance(float minDist) { minDistance = minDist; }
    void SetMaxDistance(float maxDist) { maxDistance = maxDist; }
    
    void SetRotationSpeed(float speed) { rotationSpeed = speed; }
    void SetZoomSpeed(float speed) { zoomSpeed = speed; }
    
    void Update(float deltaTime);
    
    // 輸入處理
    void OnMouseMove(float deltaX, float deltaY);
    void OnMouseScroll(float delta);
    
private:
    Camera* camera;
    Vector3 target;
    float distance;
    float minDistance;
    float maxDistance;
    
    float yaw;
    float pitch;
    
    float rotationSpeed;
    float zoomSpeed;
    
    bool isDragging;
    Vector2 lastMousePosition;
};

/**
 * 相機管理器
 */
class CameraManager {
public:
    static CameraManager& GetInstance();
    
    Camera* CreateCamera(const std::string& name);
    Camera* GetCamera(const std::string& name);
    void SetActiveCamera(const std::string& name);
    Camera* GetActiveCamera();
    
    void DestroyCamera(const std::string& name);
    void DestroyAll();
    
private:
    CameraManager();
    ~CameraManager();
    
    std::unordered_map<std::string, UniquePtr<Camera>> cameras;
    Camera* activeCamera;
};

} // namespace Potato