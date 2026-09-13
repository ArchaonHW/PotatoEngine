# Potato Engine - CMake 建置配置
# 完全獨立的遊戲引擎，不依賴 UE5

cmake_minimum_required(VERSION 3.15)
project(PotatoEngine VERSION 1.0.0 LANGUAGES CXX)

# 設置 C++ 標準
set(CMAKE_CXX_STANDARD 20)
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

# GLFW - 窗口和輸入管理
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# Disable GLFW tests
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# GLAD - OpenGL 加載器
include(FetchContent)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.4
)
FetchContent_MakeAvailable(glad)

# OpenAL - 音頻庫 (可選)
# include(FetchContent)
# FetchContent_Declare(
#     openal
#     GIT_REPOSITORY https://github.com/kcat/openal-soft.git
#     GIT_TAG 1.23.1
# )
# FetchContent_MakeAvailable(openal)

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
    ${CMAKE_SOURCE_DIR}/MathUtils
    ${CMAKE_SOURCE_DIR}/Memory
    ${CMAKE_SOURCE_DIR}/Platform
    ${CMAKE_SOURCE_DIR}/Scene
    ${CMAKE_SOURCE_DIR}/Serialization
    ${CMAKE_SOURCE_DIR}/Time
    ${CMAKE_SOURCE_DIR}/AI
    ${CMAKE_SOURCE_DIR}/GUI
    ${OPENGL_INCLUDE_DIRS}
)

# 项目文件 - 逐步启用子系统
file(GLOB_RECURSE ENGINE_SOURCES 
    "Core/*.cpp"
    # "Events/*.cpp"
    # "FileSystem/*.cpp"      # 暂时禁用 - 实现文件有结构问题
    # "Logging/*.cpp"      # 暂时禁用 - 依赖 CoreTypes
    # "Memory/*.cpp"      # 暂时禁用 - 实现文件有结构问题
    # "Platform/*.cpp"      # 暂时禁用 - 依赖 GLFW
    # "Scene/*.cpp"      # 暂时禁用 - 依赖 Matrix4/Quaternion
    # "Serialization/*.cpp"      # 暂时禁用
    # "Time/*.cpp"      # 暂时禁用 - 实现文件有结构问题
    # "Rendering/*.cpp"      # 暂时禁用 - 依赖 Math
    # "Physics/*.cpp"      # 暂时禁用 - 依赖 Math
    # "Audio/*.cpp"      # 暂时禁用 - 依赖 Math
    # "Input/*.cpp"      # 暂时禁用 - 依赖 Math
    # "Resources/*.cpp"      # 暂时禁用 - 依赖 Math
    # "ECS/*.cpp"      # 暂时禁用 - 依赖 Math
    # "GameObject/*.cpp"      # 暂时禁用 - 依赖 Math
    # "AI/*.cpp"      # 暂时禁用
    # "GUI/*.cpp"      # 暂时禁用
    # "MathUtils/*.cpp"      # 暂时禁用
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
    "Memory/*.h"
    "Platform/*.h"
    "Scene/*.h"
    "Serialization/*.h"
    "Time/*.h"
    "AI/*.h"
    "GUI/*.h"
    "MathUtils/*.h"
)

# 創建引擎庫
add_library(PotatoEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(PotatoEngine PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Core
    ${CMAKE_SOURCE_DIR}/Core/Interfaces
    ${CMAKE_SOURCE_DIR}/MathUtils
)

target_link_libraries(PotatoEngine
    ${OPENGL_LIBRARIES}
    # glfw
    # glad
    ${CMAKE_DL_LIBS}
)

target_compile_definitions(PotatoEngine PRIVATE POTATO_ENGINE_VERSION="${PROJECT_VERSION}")
target_compile_definitions(PotatoEngine PRIVATE _USE_MATH_DEFINES)

# 示例程序
# add_executable(SimpleExample Examples/SimpleExample.cpp)
# target_link_libraries(SimpleExample PotatoEngine)

# 最小測試程序
add_executable(MinimalTest Examples/MinimalTest.cpp)
target_include_directories(MinimalTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(MinimalTest)

# 引擎示例程序
add_executable(EngineExample Examples/EngineExample.cpp)
target_include_directories(EngineExample PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(EngineExample PotatoEngine)

# 数学测试程序
add_executable(MathTest Examples/MathTest.cpp)
target_include_directories(MathTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(MathTest)

# 測試程序 (暫時禁用 - 依賴 PotatoEngine)
# add_executable(FileSystemTest Examples/FileSystemTest.cpp)
# target_link_libraries(FileSystemTest PotatoEngine)

# add_executable(LoggerTest Examples/LoggerTest.cpp)
# target_link_libraries(LoggerTest PotatoEngine)

# add_executable(MathTest Examples/MathTest.cpp)
# target_link_libraries(MathTest PotatoEngine)

# add_executable(MemoryTest Examples/MemoryTest.cpp)
# target_link_libraries(MemoryTest PotatoEngine)

# add_executable(PlatformTest Examples/PlatformTest.cpp)
# target_link_libraries(PlatformTest PotatoEngine)

# AI Agent GUI 示例 (暫時禁用)
# add_executable(AIAgentGUIExample Examples/AIAgentGUIExample.cpp)
# target_link_libraries(AIAgentGUIExample PotatoEngine)

# 安裝 (暫時禁用)
# install(TARGETS PotatoEngine SimpleExample PlatformTest
#     RUNTIME DESTINATION bin
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
# )

# 安裝頭文件
install(DIRECTORY Core/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Rendering/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Physics/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Audio/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Input/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Resources/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY ECS/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY GameObject/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Events/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY FileSystem/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Logging/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY MathUtils/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Memory/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Platform/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Scene/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Serialization/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY Time/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY AI/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")
install(DIRECTORY GUI/ DESTINATION include/PotatoEngine FILES_MATCHING PATTERN "*.h")

# CPack 打包配置
set(CPACK_PACKAGE_NAME "PotatoEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "完全獨立的遊戲引擎")
set(CPACK_PACKAGE_VENDOR "Potato Engine Team")
set(CPACK_GENERATOR "ZIP;TGZ")

include(CPack)