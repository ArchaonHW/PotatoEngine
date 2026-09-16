#include "Camera.h"
#include "Logging/Logger.h"
#include <cmath>
#include <algorithm>

namespace Potato {

// ============================================================================
// Camera 實現
// ============================================================================

Camera::Camera()
    : position(Vector3(0.0f, 0.0f, 3.0f))
    , rotation(Quaternion::Identity())
    , target(Vector3(0.0f, 0.0f, 0.0f))
    , forward(Vector3(0.0f, 0.0f, -1.0f))
    , right(Vector3(1.0f, 0.0f, 0.0f))
    , up(Vector3(0.0f, 1.0f, 0.0f))
    , projectionType(CameraProjection::Perspective)
    , fov(45.0f)
    , aspect(16.0f / 9.0f)
    , nearPlane(0.1f)
    , farPlane(1000.0f)
    , orthoLeft(-1.0f)
    , orthoRight(1.0f)
    , orthoBottom(-1.0f)
    , orthoTop(1.0f)
    , viewportX(0)
    , viewportY(0)
    , viewportWidth(1920)
    , viewportHeight(1080)
    , movementSpeed(5.0f)
    , rotationSpeed(2.0f)
    , zoomSpeed(2.0f)
    , updateView(true)
    , updateProjection(true)
{
    UpdateVectors();
    UpdateProjectionMatrix();
}

void Camera::SetPosition(const Vector3& pos) {
    position = pos;
    updateView = true;
}

void Camera::SetRotation(const Quaternion& rot) {
    rotation = rot;
    useTarget = false;
    updateView = true;
}

void Camera::SetTarget(const Vector3& targ) {
    target = targ;
    useTarget = true;
    updateView = true;
}

void Camera::Move(const Vector3& delta) {
    position += delta;
    updateView = true;
}

void Camera::Rotate(float yaw, float pitch) {
    // 簡化實現：使用歐拉角旋轉
    Quaternion yawRot = Quaternion::FromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), yaw);
    Quaternion pitchRot = Quaternion::FromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), pitch);
    
    rotation = yawRot * pitchRot * rotation;
    useTarget = false;
    updateView = true;
}

void Camera::Zoom(float delta) {
    // 通過移動相機實現縮放
    Vector3 zoomDir = GetForward() * delta;
    position += zoomDir;
    updateView = true;
}

void Camera::SetProjection(CameraProjection projection) {
    projectionType = projection;
    updateProjection = true;
}

void Camera::SetPerspective(float fovDegrees, float aspectRatio, float near, float far) {
    projectionType = CameraProjection::Perspective;
    fov = fovDegrees;
    aspect = aspectRatio;
    nearPlane = near;
    farPlane = far;
    updateProjection = true;
}

void Camera::SetOrthographic(float left, float right, float bottom, float top, float near, float far) {
    projectionType = CameraProjection::Orthographic;
    orthoLeft = left;
    orthoRight = right;
    orthoBottom = bottom;
    orthoTop = top;
    nearPlane = near;
    farPlane = far;
    updateProjection = true;
}

Matrix4 Camera::GetViewMatrix() const {
    if (updateView) {
        const_cast<Camera*>(this)->UpdateVectors();
        const_cast<Camera*>(this)->updateView = false;
    }
    return viewMatrix;
}

Matrix4 Camera::GetProjectionMatrix() const {
    if (updateProjection) {
        const_cast<Camera*>(this)->UpdateProjectionMatrix();
        const_cast<Camera*>(this)->updateProjection = false;
    }
    return projectionMatrix;
}

Matrix4 Camera::GetViewProjectionMatrix() const {
    return GetProjectionMatrix() * GetViewMatrix();
}

Frustum Camera::GetFrustum() const {
    return Frustum::FromMatrix(GetViewProjectionMatrix());
}

Vector3 Camera::GetForward() const {
    return forward;
}

Vector3 Camera::GetRight() const {
    return right;
}

Vector3 Camera::GetUp() const {
    return up;
}

void Camera::SetViewport(int x, int y, int width, int height) {
    viewportX = x;
    viewportY = y;
    viewportWidth = width;
    viewportHeight = height;
    // width<=0 同樣要擋：否則 aspect 會是 0/負數,
    // 讓 projection matrix 產生 inf/NaN
    if (width > 0 && height > 0) {
        aspect = static_cast<float>(width) / static_cast<float>(height);
    }
    updateProjection = true;
}

void Camera::GetViewport(int& x, int& y, int& width, int& height) const {
    x = viewportX;
    y = viewportY;
    width = viewportWidth;
    height = viewportHeight;
}

void Camera::Update(float deltaTime) {
    // 相機可以在這裡實現自動移動或其他邏輯
}

void Camera::UpdateVectors() {
    if (useTarget) {
        // look-at 模式:由 position→target 求方向基底
        Vector3 dir = target - position;
        if (dir.LengthSquared() > 1e-8f) {
            forward = dir.Normalized();
        }
        Vector3 worldUp(0.0f, 1.0f, 0.0f);
        // 接近正俯視時改用 -Z 為參考上向量避免退化
        if (std::fabs(forward.y) > 0.999f) worldUp = Vector3(0.0f, 0.0f, -1.0f);
        right = forward.Cross(worldUp).Normalized();
        up = right.Cross(forward);
        viewMatrix = Matrix4::LookAt(position, target, up);
        return;
    }

    // 根據旋轉計算方向向量
    Matrix4 rotationMatrix = rotation.ToMatrix4();
    
    forward = Vector3(0.0f, 0.0f, -1.0f);
    right = Vector3(1.0f, 0.0f, 0.0f);
    up = Vector3(0.0f, 1.0f, 0.0f);
    
    // 應用旋轉（方向向量，不含位移）
    forward = rotationMatrix.TransformVector(forward);
    right = rotationMatrix.TransformVector(right);
    up = rotationMatrix.TransformVector(up);
    
    // 構建視圖矩陣
    viewMatrix = Matrix4::LookAt(position, position + forward, up);
}

void Camera::UpdateProjectionMatrix() {
    if (projectionType == CameraProjection::Perspective) {
        projectionMatrix = Matrix4::Perspective(fov, aspect, nearPlane, farPlane);
    } else {
        projectionMatrix = Matrix4::Orthographic(orthoLeft, orthoRight, orthoBottom, orthoTop, nearPlane, farPlane);
    }
}

// ============================================================================
// FPSCameraController 實現
// ============================================================================

FPSCameraController::FPSCameraController()
    : camera(nullptr)
    , sensitivity(0.1f)
    , moveForward(false)
    , moveBackward(false)
    , moveLeft(false)
    , moveRight(false)
    , moveUp(false)
    , moveDown(false)
    , mouseDelta(0.0f, 0.0f)
    , scrollDelta(0.0f)
{
}

void FPSCameraController::Update(float deltaTime) {
    if (!camera) return;
    
    Vector3 movement(0.0f, 0.0f, 0.0f);
    float speed = camera->GetMovementSpeed() * deltaTime;
    
    if (moveForward) {
        movement += camera->GetForward() * speed;
    }
    if (moveBackward) {
        movement -= camera->GetForward() * speed;
    }
    if (moveLeft) {
        movement -= camera->GetRight() * speed;
    }
    if (moveRight) {
        movement += camera->GetRight() * speed;
    }
    if (moveUp) {
        movement += camera->GetUp() * speed;
    }
    if (moveDown) {
        movement -= camera->GetUp() * speed;
    }
    
    camera->Move(movement);
    
    // 處理鼠標旋轉
    if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
        float yaw = mouseDelta.x * sensitivity;
        float pitch = mouseDelta.y * sensitivity;
        camera->Rotate(yaw, pitch);
        mouseDelta = Vector2(0.0f, 0.0f);
    }
    
    // 處理滾輪縮放
    if (scrollDelta != 0.0f) {
        camera->Zoom(scrollDelta * camera->GetZoomSpeed());
        scrollDelta = 0.0f;
    }
}

void FPSCameraController::OnMouseMove(float deltaX, float deltaY) {
    mouseDelta.x += deltaX;
    mouseDelta.y += deltaY;
}

void FPSCameraController::OnKeyDown(int key) {
    switch (key) {
        case 'W': moveForward = true; break;
        case 'S': moveBackward = true; break;
        case 'A': moveLeft = true; break;
        case 'D': moveRight = true; break;
        case 'Q': moveUp = true; break;
        case 'E': moveDown = true; break;
    }
}

void FPSCameraController::OnKeyUp(int key) {
    switch (key) {
        case 'W': moveForward = false; break;
        case 'S': moveBackward = false; break;
        case 'A': moveLeft = false; break;
        case 'D': moveRight = false; break;
        case 'Q': moveUp = false; break;
        case 'E': moveDown = false; break;
    }
}

void FPSCameraController::OnMouseScroll(float delta) {
    scrollDelta += delta;
}

// ============================================================================
// OrbitCameraController 實現
// ============================================================================

OrbitCameraController::OrbitCameraController()
    : camera(nullptr)
    , target(Vector3::Zero())
    , distance(5.0f)
    , minDistance(0.5f)
    , maxDistance(50.0f)
    , yaw(0.0f)
    , pitch(0.0f)
    , rotationSpeed(1.0f)
    , zoomSpeed(1.0f)
    , isDragging(false)
    , lastMousePosition(0.0f, 0.0f)
{
}

void OrbitCameraController::Update(float deltaTime) {
    if (!camera) return;
    
    // 計算相機位置
    float radYaw = yaw * 3.14159f / 180.0f;
    float radPitch = pitch * 3.14159f / 180.0f;
    
    Vector3 position;
    position.x = target.x + distance * cos(radPitch) * sin(radYaw);
    position.y = target.y + distance * sin(radPitch);
    position.z = target.z + distance * cos(radPitch) * cos(radYaw);
    
    camera->SetPosition(position);
    camera->SetTarget(target);
}

void OrbitCameraController::OnMouseMove(float deltaX, float deltaY) {
    if (isDragging) {
        yaw += deltaX * rotationSpeed;
        pitch += deltaY * rotationSpeed;
        
        // 限制俯仰角
        pitch = std::clamp(pitch, -89.0f, 89.0f);
    }
}

void OrbitCameraController::OnMouseScroll(float delta) {
    distance -= delta * zoomSpeed;
    distance = std::clamp(distance, minDistance, maxDistance);
}

// ============================================================================
// CameraManager 實現
// ============================================================================

CameraManager::CameraManager()
    : activeCamera(nullptr)
{
}

CameraManager::~CameraManager() {
    DestroyAll();
}

CameraManager& CameraManager::GetInstance() {
    static CameraManager instance;
    return instance;
}

Camera* CameraManager::CreateCamera(const std::string& name) {
    auto camera = MakeUnique<Camera>();
    Camera* cameraPtr = camera.get();
    cameras[name] = std::move(camera);
    
    if (!activeCamera) {
        activeCamera = cameraPtr;
    }
    
    LOG_INFO("Created camera: " + name);
    return cameraPtr;
}

Camera* CameraManager::GetCamera(const std::string& name) {
    auto it = cameras.find(name);
    if (it != cameras.end()) {
        return it->second.get();
    }
    return nullptr;
}

void CameraManager::SetActiveCamera(const std::string& name) {
    Camera* camera = GetCamera(name);
    if (camera) {
        activeCamera = camera;
        LOG_INFO("Set active camera: " + name);
    }
}

Camera* CameraManager::GetActiveCamera() {
    return activeCamera;
}

void CameraManager::DestroyCamera(const std::string& name) {
    auto it = cameras.find(name);
    if (it != cameras.end()) {
        if (it->second.get() == activeCamera) {
            activeCamera = nullptr;
        }
        cameras.erase(it);
        LOG_INFO("Destroyed camera: " + name);
    }
}

void CameraManager::DestroyAll() {
    cameras.clear();
    activeCamera = nullptr;
    LOG_INFO("Destroyed all cameras");
}

} // namespace Potato