#include "Math/Math.h"
#include <iostream>

using namespace Potato;

int main() {
    std::cout << "=== Potato Engine Math Library Test ===" << std::endl;
    
    // Vector2 測試
    std::cout << "\n--- Vector2 Tests ---" << std::endl;
    Vector2 v2a(3.0f, 4.0f);
    Vector2 v2b(1.0f, 2.0f);
    
    std::cout << "Vector2a: (" << v2a.x << ", " << v2a.y << ")" << std::endl;
    std::cout << "Vector2b: (" << v2b.x << ", " << v2b.y << ")" << std::endl;
    std::cout << "Length: " << v2a.Length() << std::endl;
    std::cout << "Normalized: (" << v2a.Normalized().x << ", " << v2a.Normalized().y << ")" << std::endl;
    std::cout << "Dot Product: " << v2a.Dot(v2b) << std::endl;
    std::cout << "Distance: " << Vector2::Distance(v2a, v2b) << std::endl;
    
    // Vector3 測試
    std::cout << "\n--- Vector3 Tests ---" << std::endl;
    Vector3 v3a(1.0f, 2.0f, 3.0f);
    Vector3 v3b(4.0f, 5.0f, 6.0f);
    
    std::cout << "Vector3a: (" << v3a.x << ", " << v3a.y << ", " << v3a.z << ")" << std::endl;
    std::cout << "Vector3b: (" << v3b.x << ", " << v3b.y << ", " << v3b.z << ")" << std::endl;
    std::cout << "Length: " << v3a.Length() << std::endl;
    std::cout << "Normalized: (" << v3a.Normalized().x << ", " << v3a.Normalized().y << ", " << v3a.Normalized().z << ")" << std::endl;
    std::cout << "Dot Product: " << v3a.Dot(v3b) << std::endl;
    std::cout << "Cross Product: (" << v3a.Cross(v3b).x << ", " << v3a.Cross(v3b).y << ", " << v3a.Cross(v3b).z << ")" << std::endl;
    std::cout << "Distance: " << Vector3::Distance(v3a, v3b) << std::endl;
    
    // Vector4 測試
    std::cout << "\n--- Vector4 Tests ---" << std::endl;
    Vector4 v4(1.0f, 0.5f, 0.25f, 1.0f);
    std::cout << "Vector4: (" << v4.x << ", " << v4.y << ", " << v4.z << ", " << v4.w << ")" << std::endl;
    std::cout << "Length: " << v4.Length() << std::endl;
    std::cout << "To Vector3: (" << v4.ToVector3().x << ", " << v4.ToVector3().y << ", " << v4.ToVector3().z << ")" << std::endl;
    
    // Matrix4 測試
    std::cout << "\n--- Matrix4 Tests ---" << std::endl;
    Matrix4 mat = Matrix4::Identity();
    Matrix4 translation = Matrix4::Translation(Vector3(1.0f, 2.0f, 3.0f));
    Matrix4 rotation = Matrix4::RotationY(MathUtils::DegToRad(45.0f));
    Matrix4 scale = Matrix4::Scale(Vector3(2.0f, 2.0f, 2.0f));
    
    Matrix4 transform = translation * rotation * scale;
    Vector3 transformedPoint = transform.TransformPoint(Vector3(1.0f, 0.0f, 0.0f));
    
    std::cout << "Transformed Point: (" << transformedPoint.x << ", " << transformedPoint.y << ", " << transformedPoint.z << ")" << std::endl;
    
    // 投影矩陣測試
    Matrix4 perspective = Matrix4::Perspective(MathUtils::DegToRad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    std::cout << "Perspective Matrix created successfully" << std::endl;
    
    // Quaternion 測試
    std::cout << "\n--- Quaternion Tests ---" << std::endl;
    Quaternion q = Quaternion::FromAxisAngle(Vector3::UnitY(), MathUtils::DegToRad(90.0f));
    std::cout << "Quaternion: (" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")" << std::endl;
    std::cout << "Length: " << q.Length() << std::endl;
    
    Vector3 rotatedPoint = q.RotateVector(Vector3::UnitX());
    std::cout << "Rotated Vector: (" << rotatedPoint.x << ", " << rotatedPoint.y << ", " << rotatedPoint.z << ")" << std::endl;
    
    // 四元數插值測試
    Quaternion q1 = Quaternion::Identity();
    Quaternion q2 = Quaternion::FromAxisAngle(Vector3::UnitY(), MathUtils::DegToRad(180.0f));
    Quaternion qSlerp = Quaternion::Slerp(q1, q2, 0.5f);
    std::cout << "Slerp Result: (" << qSlerp.x << ", " << qSlerp.y << ", " << qSlerp.z << ", " << qSlerp.w << ")" << std::endl;
    
    // 數學工具函數測試
    std::cout << "\n--- Math Utils Tests ---" << std::endl;
    std::cout << "PI: " << MathConstants::PI << std::endl;
    std::cout << "DegToRad(45): " << MathUtils::DegToRad(45.0f) << std::endl;
    std::cout << "RadToDeg(PI/4): " << MathUtils::RadToDeg(MathConstants::PI / 4.0f) << std::endl;
    std::cout << "Clamp(5, 0, 10): " << MathUtils::Clamp(5.0f, 0.0f, 10.0f) << std::endl;
    std::cout << "Lerp(0, 10, 0.5): " << MathUtils::Lerp(0.0f, 10.0f, 0.5f) << std::endl;
    std::cout << "SmoothStep(0, 1, 0.5): " << MathUtils::SmoothStep(0.0f, 1.0f, 0.5f) << std::endl;
    
    // 顏色轉換測試
    Vector3 color(1.0f, 0.0f, 0.0f); // 紅色
    Vector3 hsv = MathUtils::RGBToHSV(color);
    std::cout << "RGB to HSV: (" << hsv.x << ", " << hsv.y << ", " << hsv.z << ")" << std::endl;
    
    Vector3 backToRGB = MathUtils::HSVToRGB(hsv);
    std::cout << "HSV back to RGB: (" << backToRGB.x << ", " << backToRGB.y << ", " << backToRGB.z << ")" << std::endl;
    
    std::cout << "\n=== Math Library Test Complete ===" << std::endl;
    
    return 0;
}