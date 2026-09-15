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

# Project files - Gradually enable subsystems
file(GLOB_RECURSE ENGINE_SOURCES 
    "Core/*.cpp"
    # "Events/*.cpp"
    # "FileSystem/*.cpp"      # Temporarily disabled - implementation file has structural issues
    # "Logging/*.cpp"      # Temporarily disabled - depends on CoreTypes
    # "Memory/*.cpp"      # Temporarily disabled - implementation file has structural issues
    # "Platform/*.cpp"      # Temporarily disabled - depends on GLFW
    # "Scene/*.cpp"      # Temporarily disabled - depends on Matrix4/Quaternion
    # "Serialization/*.cpp"      # Temporarily disabled
    # "Time/*.cpp"      # Temporarily disabled - implementation file has structural issues
    # "Rendering/*.cpp"      # Temporarily disabled - depends on Math
    # "Physics/*.cpp"      # Temporarily disabled - depends on Math
    # "Audio/*.cpp"      # Temporarily disabled - depends on Math
    # "Input/*.cpp"      # Temporarily disabled - depends on Math
    # "Resources/*.cpp"      # Temporarily disabled - depends on Math
    # "ECS/*.cpp"      # Temporarily disabled - depends on Math
    # "GameObject/*.cpp"      # Temporarily disabled - depends on Math
    # "AI/*.cpp"      # Temporarily disabled - encoding issues need fixing
    # "GUI/*.cpp"      # Temporarily disabled
    # "MathUtils/*.cpp"      # Temporarily disabled
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

# AI 演示程序（編碼兼容版本）
add_executable(AIDemo Examples/AIDemo.cpp)
target_link_libraries(AIDemo)

# 優化 AI 演示程序（性能優化版本）
add_executable(OptimizedAIDemo Examples/OptimizedAIDemo.cpp)
target_link_libraries(OptimizedAIDemo)

# AI 測試套件（完整測試驗證）
add_executable(AITestSuite Examples/AITestSuite.cpp)
target_link_libraries(AITestSuite)

# AI 增強模塊 - 神經網絡
add_library(NeuralNetwork STATIC AI/NeuralNetwork.cpp)
target_include_directories(NeuralNetwork PUBLIC ${CMAKE_SOURCE_DIR}/AI)

# AI 增強模塊 - 強化學習
add_library(ReinforcementLearning STATIC AI/ReinforcementLearning.cpp)
target_include_directories(ReinforcementLearning PUBLIC ${CMAKE_SOURCE_DIR}/AI)
target_link_libraries(ReinforcementLearning NeuralNetwork)

# AI 增強模塊 - 自然語言處理
add_library(NaturalLanguageProcessing STATIC AI/NaturalLanguageProcessing.cpp)
target_include_directories(NaturalLanguageProcessing PUBLIC ${CMAKE_SOURCE_DIR}/AI)

# AI 增強模塊 - LLM 集成
add_library(LLMIntegration STATIC AI/LLMIntegration.cpp)
target_include_directories(LLMIntegration PUBLIC ${CMAKE_SOURCE_DIR}/AI)

# AI 增強模塊 - RAG 系統
add_library(RAGSystem STATIC AI/RAGSystem.cpp)
target_include_directories(RAGSystem PUBLIC ${CMAKE_SOURCE_DIR}/AI)
target_link_libraries(RAGSystem LLMIntegration)

# AI 增強模塊 - Agent 鏈
add_library(AgentChain STATIC AI/AgentChain.cpp)
target_include_directories(AgentChain PUBLIC ${CMAKE_SOURCE_DIR}/AI)
target_link_libraries(AgentChain LLMIntegration)

# AI 增強模塊 - 工具框架
add_library(ToolFramework STATIC AI/ToolFramework.cpp)
target_include_directories(ToolFramework PUBLIC ${CMAKE_SOURCE_DIR}/AI)
target_link_libraries(ToolFramework LLMIntegration)

# MingGoRTS IDE 智能建議系統
add_library(IntelligentSuggestion STATIC MingGoRTS_IDE/IntelligentSuggestion.cpp)
target_include_directories(IntelligentSuggestion PUBLIC 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/MingGoRTS_IDE
)

# AI 增強演示程序
add_executable(AIEnhancedDemo Examples/AIEnhancedDemo.cpp)
target_include_directories(AIEnhancedDemo PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AIEnhancedDemo NeuralNetwork ReinforcementLearning NaturalLanguageProcessing)

# 終極 AI Agent 演示程序
add_executable(UltimateAIAgentDemo Examples/UltimateAIAgentDemo.cpp)
target_include_directories(UltimateAIAgentDemo PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(UltimateAIAgentDemo AIAgentSystem LLMIntegration RAGSystem AgentChain ToolFramework NeuralNetwork NaturalLanguageProcessing)

# MingGoRTS IDE（AI Agent 整合 IDE - 命令行版本）
add_executable(MingGoRTS_IDE_CLI MingGoRTS_IDE/MingGoRTS_IDE_Main.cpp MingGoRTS_IDE/IDECore.cpp MingGoRTS_IDE/AIIntegration.cpp)
target_include_directories(MingGoRTS_IDE_CLI PRIVATE ${CMAKE_SOURCE_DIR})

# MingGoRTS IDE（GUI 版本）
add_executable(MingGoRTS_IDE_GUI 
    MingGoRTS_IDE/GUI/Main.cpp
    MingGoRTS_IDE/GUI/IDEGUI.cpp
    MingGoRTS_IDE/GUI/IDEGUI.h
    MingGoRTS_IDE/GUI/I18N.cpp
    MingGoRTS_IDE/GUI/I18N.h
    MingGoRTS_IDE/IDECore.cpp
    MingGoRTS_IDE/AIIntegration.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(MingGoRTS_IDE_GUI PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/imgui
    ${CMAKE_SOURCE_DIR}/external/imgui/backends
)
target_link_libraries(MingGoRTS_IDE_GUI glfw ${OPENGL_LIBRARIES})

# 數學測試程序
add_executable(MathTest Examples/MathTest.cpp)
target_include_directories(MathTest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(MathTest)

# 測試程序 (暫時禁用 - 依賴 PotatoEngine)
# add_executable(FileSystemTest Examples/FileSystemTest.cpp)
# target_link_libraries(FileSystemTest PotatoEngine)

# add_executable(LoggerTest Examples/LoggerTest.cpp)
# target_link_libraries(LoggerTest PotatoEngine)

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