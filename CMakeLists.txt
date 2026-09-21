# Potato Engine - CMake 建置配置
# 完全獨立的遊戲引擎，不依賴 UE5

cmake_minimum_required(VERSION 3.15)
project(PotatoEngine VERSION 1.0.0 LANGUAGES C CXX)

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
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# 安全強化旗標 (Security hardening)
# 可用 -DPOTATO_SECURITY_HARDENING=OFF 關閉
# ============================================================================
option(POTATO_SECURITY_HARDENING "Enable security hardening flags" ON)
if(POTATO_SECURITY_HARDENING)
    if(MSVC)
        # /sdl: SDL 安全檢查, /GS: 堆疊保護, /guard:cf: 控制流防護
        add_compile_options(/sdl /GS /guard:cf)
        # ASLR + DEP + 高位址熵 + CFG + CET 陰影堆疊
        add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /GUARD:CF /CETCOMPAT)
    else()
        # 堆疊保護 + PIE (位置無關執行檔，啟用 ASLR)
        add_compile_options(-fstack-protector-strong -fstack-clash-protection -fPIE)
        if(WIN32)
            # MinGW: PE 格式加固(-pie 只能套用於執行檔,SHARED/MODULE 庫加上會導致要求 WinMain)
            add_link_options(
                $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-pie>
                -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va)
        else()
            # Linux/macOS: ELF 加固
            add_compile_definitions(_FORTIFY_SOURCE=2)
            add_link_options(
                $<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:-pie>
                -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
        endif()
    endif()
endif()

# MinGW: 靜態連結 GCC 執行時（libgcc/libstdc++/winpthread），
# 讓產物不依賴 MinGW bin 目錄的 DLL,可直接執行
if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

# 查找依賴
find_package(OpenGL REQUIRED)

# GLFW - 窗口和輸入管理
# 優先使用本機安裝的 GLFW（設 GLFW_ROOT 或 CMAKE_PREFIX_PATH 指向安裝目錄）,
# 找不到才從 GitHub FetchContent
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    find_path(GLFW3_INCLUDE_DIR GLFW/glfw3.h
        HINTS ${GLFW_ROOT} $ENV{GLFW_ROOT}
        PATH_SUFFIXES include)
    # 依工具鏈選擇搜尋順序——MSVC 連 MinGW 的 libglfw3.a 會
    # 出現 __mingw_* 未解析符號；MinGW 連 VC 的 .lib 亦然
    if(MSVC)
        set(_glfw_lib_suffixes lib lib-vc2026 lib-vc2022 lib-vc2019 lib-vc2017)
    else()
        set(_glfw_lib_suffixes lib lib-mingw-w64 lib-static-ucrt)
    endif()
    find_library(GLFW3_LIBRARY NAMES glfw3 glfw
        HINTS ${GLFW_ROOT} $ENV{GLFW_ROOT}
        PATH_SUFFIXES ${_glfw_lib_suffixes})
    if(GLFW3_INCLUDE_DIR AND GLFW3_LIBRARY)
        # GLOBAL:讓 add_subdirectory 消費端（遊戲 repo）也能連結此 target
        add_library(glfw STATIC IMPORTED GLOBAL)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${GLFW3_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GLFW3_INCLUDE_DIR}")
        set(glfw3_FOUND TRUE)
        message(STATUS "Using local GLFW: ${GLFW3_LIBRARY}")
    endif()
endif()
if(NOT glfw3_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()
# 若 glfw 是 imported target（find_package/find_library 路徑），提升為 GLOBAL
# 讓 add_subdirectory 消費端可見
if(TARGET glfw)
    get_property(_glfw_imported TARGET glfw PROPERTY IMPORTED)
    if(_glfw_imported)
        set_target_properties(glfw PROPERTIES IMPORTED_GLOBAL TRUE)
    endif()
endif()

# GLAD - OpenGL 加載器
# 使用本地生成的 glad1 綁定（external/glad_gen）,提供 <glad/glad.h>
# 注意:glad v2.x 的頭文件是 <glad/gl.h>,與引擎代碼不相容,勿改回 FetchContent
add_library(glad STATIC external/glad_gen/src/glad.c)
target_include_directories(glad PUBLIC external/glad_gen/include)

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
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/Physics
    ${CMAKE_CURRENT_SOURCE_DIR}/Audio
    ${CMAKE_CURRENT_SOURCE_DIR}/Input
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources
    ${CMAKE_CURRENT_SOURCE_DIR}/ECS
    ${CMAKE_CURRENT_SOURCE_DIR}/GameObject
    ${CMAKE_CURRENT_SOURCE_DIR}/Events
    ${CMAKE_CURRENT_SOURCE_DIR}/FileSystem
    ${CMAKE_CURRENT_SOURCE_DIR}/Logging
    ${CMAKE_CURRENT_SOURCE_DIR}/MathUtils
    ${CMAKE_CURRENT_SOURCE_DIR}/Memory
    ${CMAKE_CURRENT_SOURCE_DIR}/Platform
    ${CMAKE_CURRENT_SOURCE_DIR}/Scene
    ${CMAKE_CURRENT_SOURCE_DIR}/Serialization
    ${CMAKE_CURRENT_SOURCE_DIR}/Time
    ${CMAKE_CURRENT_SOURCE_DIR}/AI
    ${CMAKE_CURRENT_SOURCE_DIR}/Security
    ${OPENGL_INCLUDE_DIRS}
)

# Project files - 所有引擎模組
file(GLOB_RECURSE ENGINE_SOURCES 
    "Core/*.cpp"
    "Security/*.cpp"      # 安全模組 - 自包含，無引擎依賴
    "Events/*.cpp"
    "FileSystem/*.cpp"
    "Logging/*.cpp"
    "Memory/*.cpp"
    "Platform/*.cpp"
    "Scene/*.cpp"
    "Serialization/*.cpp"
    "Time/*.cpp"
    "Rendering/*.cpp"
    "Physics/*.cpp"
    "Audio/*.cpp"
    "Input/*.cpp"
    "Resources/*.cpp"
    "ECS/*.cpp"
    "GameObject/*.cpp"
    # "GUI/*.cpp"      # AgentGUI 依賴遊戲側 AIAgentSystem,不進引擎
    # "MathUtils/*.cpp"      # 全 header-only
    # "AI/*.cpp"      # AI 模組另有獨立靜態庫目標,避免重複編譯
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
    "MathUtils/*.h"
)

# 創建引擎庫
add_library(PotatoEngine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(PotatoEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/MathUtils
)

# tinygltf 實作單元（Rendering/TinyGltfImpl.cpp）只需私有 include 路徑；
# PUBLIC 會把 header-only 庫的 include 污染給所有下游 target
target_include_directories(PotatoEngine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/tinygltf
)

target_link_libraries(PotatoEngine
    ${OPENGL_LIBRARIES}
    glfw
    glad
    ${CMAKE_DL_LIBS}
    NeuralNetwork # NeuralGraphics 深度學習繪圖依賴
)

# Windows: psapi.lib（Security 模組的模組枚舉用；MSVC 由 #pragma comment 處理，MinGW 需顯式連結）
# wintrust: Authenticode 簽章驗證;bcrypt: BCryptGenRandom 隨機數
if(WIN32 AND NOT MSVC)
    target_link_libraries(PotatoEngine psapi wintrust bcrypt)
endif()

target_compile_definitions(PotatoEngine PRIVATE POTATO_ENGINE_VERSION="${PROJECT_VERSION}")
target_compile_definitions(PotatoEngine PRIVATE _USE_MATH_DEFINES)
# 防止 GLFW 自動引入系統 gl.h,與 glad 衝突(引擎與所有連結目標都需要)
target_compile_definitions(PotatoEngine PUBLIC GLFW_INCLUDE_NONE)

# ============================================================================
# 引擎附屬函式庫（消費端可連結）——置於 standalone 守衛外
# ============================================================================

# AI 增強模塊 - 神經網絡
add_library(NeuralNetwork STATIC AI/NeuralNetwork.cpp)
target_include_directories(NeuralNetwork PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AI)

# 量子模擬層 - 態向量暫存器/qudit（疊加態/糾纏/Born rule 測量）
add_library(Quantum STATIC
    Quantum/QubitRegister.cpp
    Quantum/Qudit.cpp
)
target_include_directories(Quantum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 影音生成管線共用層（AD-1/AD-3/AD-4）
# HeadlessCapture.cpp 含 GL——static lib 只在 exe 實際參考時才拉入，
# 用到它的工具自行連 glad/glfw；lib 本身不宣告 GL 依賴 實際參考時才拉入，
# 用到它的工具自行連 glad/glfw；lib 本身不宣告 GL 依賴
add_library(Media STATIC
    Media/DatasetManifest.cpp
    Media/VideoEncoder.cpp
    Media/PipelineRunner.cpp
    Media/HeadlessCapture.cpp
)
target_include_directories(Media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Media PUBLIC PotatoEngine)

# ============================================================================
# Standalone 守衛：examples/tests/install 只在引擎 repo 獨立建置時註冊；
# 被遊戲 repo 以 add_subdirectory 消費時只產出函式庫 target。
# ============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)

# 示例程序
# add_executable(SimpleExample Examples/SimpleExample.cpp)
# target_link_libraries(SimpleExample PotatoEngine)

# 最小測試程序
add_executable(MinimalTest Examples/MinimalTest.cpp)
target_include_directories(MinimalTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MinimalTest)

# 引擎示例程序
add_executable(EngineExample Examples/EngineExample.cpp)
target_include_directories(EngineExample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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

# 數學測試程序
add_executable(MathTest Examples/MathTest.cpp)
target_include_directories(MathTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MathTest)

# 物理積分測試
add_executable(PhysicsTest Examples/PhysicsTest.cpp)
target_include_directories(PhysicsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 物理數學測試（headless：Verlet 精度、無散度湍流場、混沌敏感度）
add_executable(PhysicsMathTest Examples/PhysicsMathTest.cpp)
target_include_directories(PhysicsMathTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PhysicsMathTest PotatoEngine)
target_link_libraries(PhysicsTest PotatoEngine)

# 安全模組紅隊測試（對抗性驗證 Security/ 的偵測能力）
add_executable(SecurityRedTeamTest Examples/SecurityRedTeamTest.cpp)
target_include_directories(SecurityRedTeamTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SecurityRedTeamTest PotatoEngine)

# 紅隊測試用的無害 DLL（模擬被注入的作弊模組）
add_library(FakeCheatModule SHARED Examples/FakeCheatModule.cpp)
set_target_properties(FakeCheatModule PROPERTIES OUTPUT_NAME "FakeCheat" PREFIX "")
add_dependencies(SecurityRedTeamTest FakeCheatModule)

# 子系統測試程序
add_executable(FileSystemTest Examples/FileSystemTest.cpp)
target_include_directories(FileSystemTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(FileSystemTest PotatoEngine)

add_executable(LoggerTest Examples/LoggerTest.cpp)
target_include_directories(LoggerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LoggerTest PotatoEngine)

add_executable(MemoryTest Examples/MemoryTest.cpp)
target_include_directories(MemoryTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MemoryTest PotatoEngine)

add_executable(PlatformTest Examples/PlatformTest.cpp)
target_include_directories(PlatformTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PlatformTest PotatoEngine)

add_executable(SceneTest Examples/SceneTest.cpp)
target_include_directories(SceneTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SceneTest PotatoEngine)

add_executable(SerializationTest Examples/SerializationTest.cpp)
target_include_directories(SerializationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SerializationTest PotatoEngine)

add_executable(RenderPipelineTest Examples/RenderPipelineTest.cpp)
target_include_directories(RenderPipelineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RenderPipelineTest PotatoEngine)

# 影音管線 CPU 段無頭測試（GL/ffmpeg 段不進測試）
add_executable(MediaPipeTest Examples/MediaPipeTest.cpp)
target_include_directories(MediaPipeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(MediaPipeTest Media)

# RenderTarget 離屏 FBO 測試（GL-gated：無顯示環境 SKIP）
add_executable(RenderTargetTest Examples/RenderTargetTest.cpp)
target_include_directories(RenderTargetTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RenderTargetTest PotatoEngine glad glfw)

# B-1 Potato::Log 輕量日誌無頭測試（stderr 捕獲/級別/檔案 sink）
add_executable(LogTest Examples/LogTest.cpp)
target_include_directories(LogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(LogTest PotatoEngine)

# F-1 sprite atlas 索引/uv/降級無頭測試
add_executable(SpriteAtlasTest Examples/SpriteAtlasTest.cpp)
target_include_directories(SpriteAtlasTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SpriteAtlasTest PotatoEngine)

# 天使軍團程序化影片（工具性質：需 GL + ffmpeg,不進 POTATO_TESTS）
add_executable(AngelLegionVideo Examples/AngelLegionVideo.cpp)
target_include_directories(AngelLegionVideo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(AngelLegionVideo PotatoEngine glad glfw Media)

add_executable(ModelLoaderTest Examples/ModelLoaderTest.cpp)
target_include_directories(ModelLoaderTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ModelLoaderTest PotatoEngine)

# glTF/VRM 載入管線測試（assets/avatars/ 缺檔時自動略過真檔案例）
add_executable(VrmLoadTest Examples/VrmLoadTest.cpp)
target_include_directories(VrmLoadTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(VrmLoadTest PotatoEngine)

add_executable(InputTest Examples/InputTest.cpp)
target_include_directories(InputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(InputTest PotatoEngine)

add_executable(AudioTest Examples/AudioTest.cpp)
target_include_directories(AudioTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(AudioTest PotatoEngine)

add_executable(TimeTest Examples/TimeTest.cpp)
target_include_directories(TimeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(TimeTest PotatoEngine)

add_executable(ImageCodecTest Examples/ImageCodecTest.cpp)
target_include_directories(ImageCodecTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ImageCodecTest PotatoEngine)

# 核心系統無頭煙霧測試：ECS / EventBus / ResourceCache
add_executable(CoreSystemsTest Examples/CoreSystemsTest.cpp)
target_include_directories(CoreSystemsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CoreSystemsTest PotatoEngine)

# 深度學習繪圖：headless 測試（SR/降噪/上色/INR/地形/法線/LOD）
add_executable(NeuralGraphicsTest Examples/NeuralGraphicsTest.cpp)
target_include_directories(NeuralGraphicsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(NeuralGraphicsTest PotatoEngine NeuralNetwork)

# 深度學習繪圖離線 CLI：訓練產出 .pnn，推論讀寫 PNG
add_executable(NeuralArtTool Examples/NeuralArtTool.cpp)
target_include_directories(NeuralArtTool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(NeuralArtTool PotatoEngine NeuralNetwork)

# Collatz 雜湊/擁擠阻塞模型測試（headless,純引擎層）
add_executable(QuasiModelsTest Examples/QuasiModelsTest.cpp)
target_include_directories(QuasiModelsTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(QuasiModelsTest PotatoEngine)

# GL 煙霧測試：需要顯示環境,無顯示時自行 SKIP
add_executable(GLSmokeTest Examples/GLSmokeTest.cpp)
target_include_directories(GLSmokeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GLSmokeTest PotatoEngine glad glfw)

# 人物立繪產生器：VRM/glTF → 離屏渲染 → PNG（需 GL 環境，屬工具非測試）
add_executable(PortraitRenderer Examples/PortraitRenderer.cpp)
target_include_directories(PortraitRenderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(PortraitRenderer PotatoEngine glad glfw)

# 合成資料管線：離屏渲染 -> 標註資料集 -> NeuralNetwork 訓練驗證
# （需 GL 環境；無顯示時自行 SKIP，屬測試）
add_executable(SynthDataDemo Examples/SynthDataDemo.cpp)
target_include_directories(SynthDataDemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SynthDataDemo PotatoEngine NeuralNetwork glad glfw)

# 2D 分層立繪產生器（純 CPU 光栅；ImGui 只用 font atlas 點陣化，無 GL）
add_executable(PortraitBaker
    Examples/PortraitBaker.cpp
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
)
target_include_directories(PortraitBaker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
)
target_link_libraries(PortraitBaker PotatoEngine)

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

# ============================================================================
# CTest - 非互動測試可用 ctest 一鍵執行
# ============================================================================
enable_testing()

set(POTATO_TESTS
    MinimalTest
    MathTest
    PhysicsTest
    PhysicsMathTest
    FileSystemTest
    LoggerTest
    MemoryTest
    PlatformTest
    SceneTest
    SerializationTest
    RenderPipelineTest
    ModelLoaderTest
    VrmLoadTest
    InputTest
    AudioTest
    TimeTest
    ImageCodecTest
    CoreSystemsTest
    NeuralGraphicsTest
    GLSmokeTest
    SecurityRedTeamTest
    AITestSuite
    MediaPipeTest
    RenderTargetTest
    LogTest
    SpriteAtlasTest
    SynthDataDemo
    QuasiModelsTest
)
foreach(test_name IN LISTS POTATO_TESTS)
    if(TARGET ${test_name})
        add_test(NAME ${test_name} COMMAND ${test_name})
        # 測試需要同目錄的 DLL(如 FakeCheat.dll),工作目錄設為 bin
        set_tests_properties(${test_name} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endif()
endforeach()

# CPack 打包配置
set(CPACK_PACKAGE_NAME "PotatoEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "完全獨立的遊戲引擎")
set(CPACK_PACKAGE_VENDOR "Potato Engine Team")
set(CPACK_GENERATOR "ZIP;TGZ")

include(CPack)

endif() # STANDALONE
