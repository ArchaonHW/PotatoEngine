#include "MathUtils/MathUtils.h"
#include "MathUtils/Vector2.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Vector4.h"
#include "MathUtils/Matrix4.h"
#include "MathUtils/Quaternion.h"
#include "MathUtils/Frustum.h"
#include <iostream>
#include <cstdlib>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine Math Test ===" << std::endl << std::endl;

    // Test MathUtils
    std::cout << "--- MathUtils ---" << std::endl;
    std::cout << "PI: " << PI << std::endl;
    std::cout << "DegToRad(90): " << DegToRad(90.0f) << std::endl;
    std::cout << "RadToDeg(PI/2): " << RadToDeg(HALF_PI) << std::endl;
    std::cout << "Clamp(5, 0, 10): " << Clamp(5.0f, 0.0f, 10.0f) << std::endl;
    std::cout << "Lerp(0, 10, 0.5): " << Lerp(0.0f, 10.0f, 0.5f) << std::endl;
    std::cout << std::endl;

    // Test Vector2
    std::cout << "--- Vector2 ---" << std::endl;
    Vector2 v2a(1.0f, 2.0f);
    Vector2 v2b(3.0f, 4.0f);
    std::cout << "v2a: " << v2a.x << ", " << v2a.y << std::endl;
    std::cout << "v2b: " << v2b.x << ", " << v2b.y << std::endl;
    std::cout << "v2a + v2b: " << (v2a + v2b).x << ", " << (v2a + v2b).y << std::endl;
    std::cout << "v2a.Dot(v2b): " << v2a.Dot(v2b) << std::endl;
    std::cout << "v2a.Length(): " << v2a.Length() << std::endl;
    std::cout << std::endl;

    // Test Vector3
    std::cout << "--- Vector3 ---" << std::endl;
    Vector3 v3a(1.0f, 2.0f, 3.0f);
    Vector3 v3b(4.0f, 5.0f, 6.0f);
    std::cout << "v3a: " << v3a.x << ", " << v3a.y << ", " << v3a.z << std::endl;
    std::cout << "v3b: " << v3b.x << ", " << v3b.y << ", " << v3b.z << std::endl;
    std::cout << "v3a + v3b: " << (v3a + v3b).x << ", " << (v3a + v3b).y << ", " << (v3a + v3b).z << std::endl;
    std::cout << "v3a.Dot(v3b): " << v3a.Dot(v3b) << std::endl;
    std::cout << "v3a.Cross(v3b): " << v3a.Cross(v3b).x << ", " << v3a.Cross(v3b).y << ", " << v3a.Cross(v3b).z << std::endl;
    std::cout << "v3a.Length(): " << v3a.Length() << std::endl;
    std::cout << "v3a.Normalized(): " << v3a.Normalized().x << ", " << v3a.Normalized().y << ", " << v3a.Normalized().z << std::endl;
    std::cout << std::endl;

    // Test Vector4
    std::cout << "--- Vector4 ---" << std::endl;
    Vector4 v4(1.0f, 2.0f, 3.0f, 4.0f);
    std::cout << "v4: " << v4.x << ", " << v4.y << ", " << v4.z << ", " << v4.w << std::endl;
    std::cout << "v4.Length(): " << v4.Length() << std::endl;
    std::cout << std::endl;

    // Test Matrix4
    std::cout << "--- Matrix4 ---" << std::endl;
    Matrix4 identity = Matrix4::Identity();
    std::cout << "Identity matrix created" << std::endl;
    
    Matrix4 translation = Matrix4::Translation(1.0f, 2.0f, 3.0f);
    std::cout << "Translation matrix created" << std::endl;
    
    Matrix4 rotation = Matrix4::RotationY(PI / 4);
    std::cout << "Rotation matrix created" << std::endl;
    
    Matrix4 scale = Matrix4::Scale(2.0f, 2.0f, 2.0f);
    std::cout << "Scale matrix created" << std::endl;
    
    Matrix4 combined = translation * rotation * scale;
    std::cout << "Combined matrix: translation * rotation * scale" << std::endl;
    
    Vector3 point(1.0f, 0.0f, 0.0f);
    Vector3 transformed = combined.TransformPoint(point);
    std::cout << "Transformed point: " << transformed.x << ", " << transformed.y << ", " << transformed.z << std::endl;
    std::cout << std::endl;

    // Test Quaternion
    std::cout << "--- Quaternion ---" << std::endl;
    Quaternion qIdentity = Quaternion::Identity();
    std::cout << "Identity quaternion: " << qIdentity.x << ", " << qIdentity.y << ", " << qIdentity.z << ", " << qIdentity.w << std::endl;
    
    Quaternion qRotation = Quaternion::FromEulerAngles(PI / 4, PI / 4, 0.0f);
    std::cout << "Rotation quaternion from Euler angles" << std::endl;
    
    Vector3 axis(0.0f, 1.0f, 0.0f);
    Quaternion qAxisAngle = Quaternion::FromAxisAngle(axis, PI / 4);
    std::cout << "Rotation quaternion from axis-angle" << std::endl;
    
    Vector3 rotated = qAxisAngle * Vector3(1.0f, 0.0f, 0.0f);
    std::cout << "Rotated vector: " << rotated.x << ", " << rotated.y << ", " << rotated.z << std::endl;
    
    Quaternion qSlerp = Quaternion::Slerp(qIdentity, qRotation, 0.5f);
    std::cout << "Slerp quaternion created" << std::endl;
    
    Matrix4 qMatrix = qRotation.ToMatrix4();
    std::cout << "Quaternion to Matrix4 conversion" << std::endl;
    std::cout << std::endl;

    // Test Frustum (視錐剔除)
    std::cout << "--- Frustum ---" << std::endl;
    {
        // 相機在原點朝 -Z 看,fov 60,near 0.1,far 100
        Matrix4 proj = Matrix4::Perspective(60.0f * PI / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        Matrix4 view = Matrix4::LookAt(Vector3(0, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
        Frustum frustum = Frustum::FromMatrix(proj * view);

        auto check = [](bool ok, const char* name) {
            std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name << std::endl;
            if (!ok) { std::cout << "FRUSTUM TEST FAILED" << std::endl; exit(1); }
        };

        // 視錐內的點(正前方)
        check(frustum.ContainsPoint(Vector3(0, 0, -10)), "前方點在視錐內");
        // 相機後方的點
        check(!frustum.ContainsPoint(Vector3(0, 0, 10)), "後方點被剔除");
        // 超出 far plane
        check(!frustum.ContainsPoint(Vector3(0, 0, -200)), "超過 far 被剔除");
        // near plane 之前
        check(!frustum.ContainsPoint(Vector3(0, 0, -0.01f)), "near 之前被剔除");
        // 側面超出 fov
        check(!frustum.ContainsPoint(Vector3(50, 0, -10)), "側面超出 fov 被剔除");

        // 球體:部分相交算可見
        check(frustum.ContainsSphere(Vector3(0, 0, -10), 2.0f), "球體在視錐內");
        check(!frustum.ContainsSphere(Vector3(0, 0, 20), 2.0f), "後方球體被剔除");
        check(frustum.ContainsSphere(Vector3(0, 0, -101), 3.0f), "球體跨越 far 仍算可見(相交)");

        // AABB
        check(frustum.ContainsAABB(AABB(Vector3(-1, -1, -6), Vector3(1, 1, -4))), "AABB 在視錐內");
        check(!frustum.ContainsAABB(AABB(Vector3(90, -1, -11), Vector3(91, 1, -9))), "側面 AABB 被剔除");
    }
    std::cout << std::endl;

    std::cout << "=== All Math Tests Passed ===" << std::endl;
    return 0;
}
