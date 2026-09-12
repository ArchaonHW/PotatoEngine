# Potato Engine - CMake 建置配置
# 完全獨立的遊戲引擎，不依賴 UE5

cmake_minimum_required(VERSION 3.15)
project(PotatoEngine VERSION 1.0.0 LANGUAGES CXX)

# 設置 C++ 標準
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 設置輸出目錄
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 編譯選項
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 查找依賴
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

# 包含目錄
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Core
    ${CMAKE_SOURCE_DIR}/Rendering
    ${CMAKE_SOURCE_DIR}/Physics
    ${CMAKE_SOURCE_DIR}/Audio
    ${CMAKE_SOURCE_DIR}/Input
    ${CMAKE_SOURCE_DIR}/Resources
    ${CMAKE_SOURCE_DIR}/ECS
    ${CMAKE_SOURCE_DIR}/GameObject
    ${CMAKE_SOURCE_DIR}/Events
    ${CMAKE_SOURCE_DIR}/FileSystem
    ${CMAKE_SOURCE_DIR}/Logging
    ${CMAKE_SOURCE_DIR}/Math
    ${CMAKE_SOURCE_DIR}/Memory
    ${CMAKE_SOURCE_DIR}/Platform
    ${CMAKE_SOURCE_DIR}/Scene
    ${CMAKE_SOURCE_DIR}/Serialization
    ${CMAKE_SOURCE_DIR}/Time
    ${OPENGL_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
)

# 項目文件
file(GLOB_RECURSE ENGINE_SOURCES 
    "Core/*.cpp"
    "Rendering/*.cpp"
    "Physics/*.cpp"
    "Audio/*.cpp"
    "Input/*.cpp"
    "Resources/*.cpp"
    "ECS/*.cpp"
    "GameObject/*.cpp"
    "Events/*.cpp"
    "FileSystem/*.cpp"
    "Logging/*.cpp"
    "Math/*.cpp"
    "Memory/*.cpp"
    "Platform/*.cpp"
    "Scene/*.cpp"
    "Serialization/*.cpp"
    "Time/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS
    "Core/*.h"
    "Rendering/*.h"
    "Physics/*.h"
    "Audio/*.h"
    "Input/*.h"
    "Resources/*.h"
    "ECS/*.h"
    "GameObject/*.h"
    "Events/*.h"
    "FileSystem/*.h"
    "Logging/*.h"
    "Math/*.h"
    "Memory/*.h"
    "Platform/*.h"
    "Scene/*.h"
    "Serialization/*.h"
    "Time/*.h"
)

# 創建引擎庫
add_library(PotatoEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# 鏈接庫
target_link_libraries(PotatoEngine
    ${OPENGL_LIBRARIES}
    glfw
    ${CMAKE_DL_LIBS}
)

# 預處理器定義
target_compile_definitions(PotatoEngine PRIVATE POTATO_ENGINE_VERSION="${PROJECT_VERSION}")

# 示例程序
add_executable(SimpleExample Examples/SimpleExample.cpp)
target_link_libraries(SimpleExample PotatoEngine)

# 測試程序
add_executable(FileSystemTest Examples/FileSystemTest.cpp)
target_link_libraries(FileSystemTest PotatoEngine)

add_executable(LoggerTest Examples/LoggerTest.cpp)
target_link_libraries(LoggerTest PotatoEngine)

add_executable(MathTest Examples/MathTest.cpp)
target_link_libraries(MathTest PotatoEngine)

add_executable(MemoryTest Examples/MemoryTest.cpp)
target_link_libraries(MemoryTest PotatoEngine)

# 安裝
install(TARGETS PotatoEngine SimpleExample
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 安裝頭文件
install(DIRECTORY Core/Rendering/Physics/Audio/Input/Resources/ECS/GameObject/Events/FileSystem/Logging/Math/Memory/Platform/Scene/Serialization/Time/
    DESTINATION include/PotatoEngine
    FILES_MATCHING PATTERN "*.h"
)

# CPack 打包配置
set(CPACK_PACKAGE_NAME "PotatoEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "完全獨立的遊戲引擎")
set(CPACK_PACKAGE_VENDOR "Potato Engine Team")
set(CPACK_GENERATOR "ZIP;TGZ")

include(CPack)