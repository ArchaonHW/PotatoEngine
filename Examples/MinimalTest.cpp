#include <iostream>
#include "MathUtils/Vector3.h"

int main() {
    Potato::Vector3 v(1.0f, 2.0f, 3.0f);
    std::cout << "Vector3: " << v.x << ", " << v.y << ", " << v.z << std::endl;
    std::cout << "Length: " << v.Length() << std::endl;
    return 0;
}