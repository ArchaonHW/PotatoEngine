# PotatoEngine

獨立 C++20 遊戲引擎 repo——由 `C:\HWC\MingGoRTS` monorepo 經 `git filter-repo`
歷史拆分產生（保留引擎相關路徑的完整提交歷史）。扁平模組目錄
（`<Module>/*.h|.cpp` 混放）與 monorepo 一致，不再做 include/src 重排。

## 模組清單

AI（NeuralNetwork + Agent 平台：AIAgentSystem/LLMIntegration/RAGSystem/
AgentChain/ToolFramework/NaturalLanguageProcessing/ReinforcementLearning/
IntelligentDevelopmentSystem——AgentChain 為真調度執行：
Agent/LLM/Tool/Condition/Loop/Parallel 節點共用 ExecCtx，
{var} 佔位餵 inputs+上游輸出，condition 運算式支援
==/!=/>/</>=/<=/!，被 branch/loopBody/parallel 引用的
子節點為子例程不佔主序列；AgentGraph 拓撲執行同一套
調度；ChainOrchestrator 以鏈/圖名註冊；RenderAgentTools 把
RenderingServer RID API 註冊成 render_* 工具供 agent 繪圖；
ZetaAgentTools 把 MathUtils/RiemannZeta（Potato::RZ——
EM ζ/Hurwitz/Γ/RS-Z/ξ/η/β/L(χ₅)/de Bruijn–Newman H_t/
Li/Mertens/Robin/ψ 篩法 + 統計/泛函形式：Montgomery
配對關聯、S(T) 計數殘差、最小間距、Chebyshev bias、
Báez–Duarte d_N、GUE 間距、顯式公式重建、Dedekind
Λ_K FE、Speiser ζ′、線上矩、Selberg CLT、Gram、
Euler 乘積）註冊成 zeta_* 工具；
zeta_audit 一鍵跑零點/輻角/Laguerre/H_λ/Mertens/
Robin/Li 審計電池，agent 可做 RH 有限驗證；
LLMToolBridge 把整個 ToolRegistry 序列化成 LLM
function-calling ToolDefinition 並將回傳 ToolCall 的
arguments JSON 解析回 string map 派發——LLM 直接
驅動 render/zeta 工具面）、
Audio、Core、ECS、Events、FileSystem、
GameObject、Input、Logging、MathUtils、Media、Memory、Networking、Physics、
Platform、Quantum、Rendering、Resources、Scene、Security、Serialization、
Simulation（SystemDynamics：存量-流量離散模擬器——stock/flow/aux/param/
  確定+機率事件/延遲鏈/KPI 探針，RTS 經濟與專案運營建模用；
  範例 Examples/ProjectOpsDemo.cpp）、
Time、build-video、GUI（AgentGUI 等引擎 GUI）、
Examples（standalone 建置時的範例/測試程式）、docs、
java（Spring Boot agent 微服務 + JNI 橋接：agent-service/
api-gateway/discovery-server/web3-service，
原生端 native/PotatoAgentJni.cpp + PotatoAgentExtJni.cpp）、
external（vendored：glad_gen/tinygltf/miniaudio/imgui submodule）

## 消費關係（重要）

- 遊戲 repo（MingGoRTS）以 sibling checkout + `add_subdirectory` 消費本 repo：
  `POTATO_ENGINE_ROOT`（預設 `../PotatoEngine`）指向本 checkout。
- 依賴方向恆為 `PotatoEngine ← Gameplay ← Campaign`——
  **本 repo 不得引用 Gameplay/Campaign/MingGoRTS_IDE 的檔案或標頭**。
- 引擎測試範例全在 standalone 守衛
  `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)` 內：
  被消費時只產出 `PotatoEngine`/`NeuralNetwork`/`ReinforcementLearning`/
  `NaturalLanguageProcessing`/`AIAgentSystem`/`LLMIntegration`/`RAGSystem`/
  `AgentChain`/`ToolFramework`/`RenderAgentTools`/`ZetaAgentTools`/`LLMToolBridge`/
  `IntelligentDevelopmentSystem`/`Quantum`/
  `Media`/`glad`/`glfw` 這些 lib target；examples/tests/install/CPack
  只在獨立建置時註冊。
- `glfw` 為 imported target 時必須 `GLOBAL`——imported target 預設是
  目錄範圍，不提升則消費端看不到（會退化成 `-lglfw` 裸連結失敗）。
- 消費端直編引擎源碼的路徑一律寫 `${POTATO_ENGINE_ROOT}/...`。
  注意 `PotatoEngine` PUBLIC 定義 `GLFW_INCLUDE_NONE`——
  需要系統 `gl.h` 的 target（如 IDE_GUI）不可連結引擎 lib，
  以直編所需源碼代替（現例：`Rendering/ImageCodec.cpp`、`Logging/Logger.cpp`）。

## 渲染管線

- 延遲管線（GL 3.3 core）：`GBuffer`（MRT view-space pos/normal/albedo/
  material(metallic,roughness,emissive)）
  → `AmbientOcclusion`（SSAO+blur）→ `ShadowMap` 或 `CascadedShadowMap`
  （方向光 PCF；CSM 為 GL_TEXTURE_2D_ARRAY 深度陣列，≤4 級，
  practical split λ 混合 + 切片緊密包圍 + texel snap，
  shader 端按視深選層，DeferredLighting 的 csm 三參數組優先於單張）
  → `DeferredLighting`（Cook-Torrance GGX PBR 合成——gMaterial.rg
  逐像素 metallic/roughness + Schlick F0 混 albedo，≤4 方向光/≤8 點光/
  ≤8 聚光；
  聚光 = 點光窗口 × inner/outer 錐形線性衰減，錐參數存 cos 半角；
  第一支 castShadow 聚光吃獨立透視 shadow map——
  `ShadowMap::ComputeSpotLightSpaceMatrix`；第一支 castShadow 點光吃
  `PointShadowMap` 立方體深度（6×90° 透視面，深度存
  世界距離/far，採樣端取主要軸向 cubemap 向量乘 farPlane 比較，
  bias 走法線坡度縮放世界單位），三種陰影可共存）
  → `HDRRenderTarget`（RGBA16F）→ 選配透明 forward pass
  （`QueueTransparent` 佇列自動依 view 深度遠→近排序，
  或 `DeferredFrameParams::drawTransparent` 回呼/手動
  BeginTransparencyPass/DrawTransparent/EndTransparencyPass：
  GBuffer 深度 blit 進 HDR depth、depth test 開/寫關、
  GL_SRC_ALPHA blend、光源/陰影 uniforms 與延遲合成同一份
  GLSL——`DeferredLighting::UploadLights`+`UploadIBL`+
  共享光源迴圈/IBL 環境項；透明物件不寫 GBuffer、無 AO）
  → `ScreenSpaceReflection`（選配：
  view-space raymarch + 二分細化，Fresnel×(1-roughness)×邊緣淡出，
  bloom 前疊入 HDR）→ `Bloom`（soft-knee + ping-pong Gaussian）
  → `ToneMapper`（ACES + gamma）→ 選配 `FxaaFilter`
  （enableFXAA 時 tone map 先進 RGBA8 中介 target,再 FXAA
  console 變體+對比門檻早退寫 outputFbo——GBuffer MRT 不吃
  MSAA,後處理 AA 是延遲管線唯一抗鋸齒路徑；FXAA 次像素
  取樣依賴輸入貼圖 LINEAR 過濾）。一次呼叫入口：`DeferredRenderer`
  （BeginShadowPass 或 useCascades 時 BeginCascadedShadowPass+BindCascade/
  聚光 BeginSpotShadowPass/點光 BeginPointShadowPass+BindPointShadowFace
  逐面渲染——共用 EndShadowPass/BeginGeometry/RenderFrame）。
- 不透明佇列 + 視錐剔除：`QueueGeometry(OpaqueDraw)` 排入平面材質
  幾何,BeginGeometry 後 `DrawGeometryQueue(view, proj)` 先以
  `CullGeometryQueue`（純 CPU 靜態、headless 可測）剔除——mesh
  本地 AABB（`Mesh::SetVertices` 自動算）經 `AABB::Transformed`
  8 角保守世界盒 × `Frustum::FromMatrix(proj*view)` 全在外才剔,
  再以內建 GBuffer shader 逐次繪製並回報實際繪製數。
  場景圖側：`SceneNode::SetRenderable` 在組件 `boundingRadius<0`
  時從 `mesh->HasBounds()` 自動推導（`|localCenter|+radius`
  保守包住偏離原點的 mesh）,手動值優先。
- 光源接軌：`LightingManager::GatherDeferredLights` 把註冊的
  DirectionalLight/PointLight/SpotLight 收成
  DeferredDirLight/DeferredPointLight/DeferredSpotLight——castShadow 用
  `Light::SetCastShadow`（方向光→單張/CSM；聚光→獨立透視 shadow map；
  點光→`PointShadowMap` 六面 cube）、三段式衰減經 `AttenuationToRadius`
  折成平滑窗口半徑、Spot 的 cutoff/outerCutoff（度）轉
  innerCos/outerCos、ambient 走 `GetDeferredAmbient`（Rec.709 亮度）。
- 各 pass 物件 Create 皆需 GL context；CPU 段（kernel/權重/光源打包/
  light-space 矩陣）headless 可測，GL 段測試無顯示自行 SKIP。
- IBL：`IBL::` 命名空間為純 CPU 生成鏈（equirect→cube face、
  irradiance 半球卷積、GGX prefilter、BRDF LUT、程序化天空），
  `EnvironmentMaps::Generate` 只做 GL 上傳；deferred 端經
  `DeferredFrameParams::environment` 啟用（useIBL 替換 flat ambient；
  透明 forward pass 同吃——`UploadIBL` 走同一組 irradiance/
  prefilter/LUT 貼圖單元 5/6/7，
  且背景像素採 env cubemap 當天空——`DeferredLighting::SkyDir`
  uv→世界方向，HDR 天空走同一條 bloom/tonemap 鏈）。
- 2D：`SpriteBatcher` GPU instanced 批次（unit quad + per-instance
  offset/size/uvRect/color/rotCosSin，單次 draw call）；接
  `SpriteAtlas::Find()` 的 SpriteFrame 經 `SetUV` 填 uv；
  投影用 `Matrix4::Orthographic(0,w,h,0,-1,1)`（像素左上原點）。
- 圖元繪製：`PrimitiveBatcher` 立即模式線段/三角形批次——
  Line/Circle/Box(AABB)/WireSphere/WireCylinder/WireCone/
  Grid/Axes/Rect/FrustumBox/Arrow（軸+鰭錐頭）/Disc/Bezier
  純 CPU 產生 LineVertex{pos,color}，`Flush(viewProj)` 單次
  draw；2D/3D 同一套（傳 ortho 或透視 viewProj），
  alpha blend + 可切 depthTest——除錯可視化（bounds/frustum/
  導航/物理）與程序化 2D 圖形共用基建。
- 程序化網格：`PrimitiveMeshes::MakeCube/UVSphere/Cylinder/
  Cone/Plane/Quad/Torus/Capsule`（capsule = south→north
  lathe：下半球+柱身+上半球，Y 軸對稱）產生標準 Vertex
  （pos/normal/uv/tangent），繞序已依外法線 CCW 校正，
  直接餵 GBuffer/forward 管線。
- 文字：`FontAtlas` 經 imgui 內嵌 imstb_truetype 烘焙 TTF→R8
  coverage 圖集 + 排版（Layout/MeasureWidth/換行/'?' fallback）；
  `TextRenderer::Create` 上傳 RGBA（白底+alpha coverage），
  `QueueText` 產生 `SpriteInstance` 直接餵 `SpriteBatcher`——
  同圖集文字 draw call 恆為 1，`SpriteInstance.color` 即染色。
- Server 層（Godot servers/rendering 同款分層，全部 RID 句柄+
  POD 參數，AI agent 可直接驅動）：`Core/RID.h`（RID 句柄
  index|validator<<32 + `RID_Alloc` slot 池，代際驗證防懸垂）
  → `RenderingServer`（singleton 抽象，snake_case API：
  texture_2d/font/mesh/canvas/canvas_item/scenario/instance/
  camera/light/viewport/free_rid/draw_frame）
  → `RenderingServerDefault`（持有資源池 +
  `RendererCanvasCull`（2D：canvas layer 排序、item 樹
  transform/z/modulate 疊加、Rect2 視錐剔除、
  Rect/Line/PolyLine/Circle/Polygon/TextureRect/Text 指令 variant）
  與 `RendererSceneCull`（3D：scenario 扁平 instance 集、
  instance_set_base 指 mesh/light、光源姿態走 instance
  transform 的 -Z 軸與平移、AABB×Frustum 剔除、打包成
  Deferred*Light））
  → `Rasterizer{Canvas,Scene}` 後端介面：`GL` 後端
  （PrimitiveBatcher+SpriteBatcher+TextRenderer / 橋接
  DeferredRenderer 佇列+方向光 shadow pass）或 `Dummy`
  （只記錄指令流——headless 測試與 agent 模擬執行用）。
  入口：`RenderingServerDefault::CreateGL()/CreateHeadless()`
  建構即註冊 `RenderingServer::GetSingleton()`。
- Agent 繪圖工具：`AI/RenderAgentTools`（獨立 lib
  `RenderAgentTools`，連結 ToolFramework+PotatoEngine）——
  `RegisterRenderTools(registry, server)` 註冊 25 支
  `render_*` 工具（viewport/canvas/item 指令/mesh primitive/
  scenario/instance/light/camera/紋理/INR 神經紋理
  `render_texture_generate_neural`/draw_frame/free_rid），
  參數全 string map、RID 以 uint64 字串往返，headless
  server 下 agent 可在無 GL 環境組裝並驗證場景。
  測試 `Examples/RenderingServerTest.cpp`（全 headless）。

## Java 微服務層（AI Agent + Web3）

- 拓撲：discovery-server（Eureka :8761）← api-gateway
  （Spring Cloud Gateway :8080）→ agent-service（:8081）/
  web3-service（:8082）。gateway 路由前綴是 SCG 5.x 的
  `spring.cloud.gateway.server.webflux.routes`，
  順序：ws `/ws/**` → `/api/web3/**` → `/api/**` → `/**`。
- JNI 橋：`java/native/PotatoAgentJni.cpp`（RL/GridWorld 原有）+
  `PotatoAgentExtJni.cpp`（LLM/AgentChain/RAG/Render/AuditLedger，
  一律 JSON 字串往返，原生端用 `Serialization/JsonParser.h`）。
  agent-service 的 `jni/Native{Chain,Ledger,Llm,Rag,Render}.java`
  對應宣告；DLL 經 `-Dpotato.jni.dll`/`POTATO_JNI_DLL` 載入。
- REST 域：/api/agent/*（RL session+WS）、/api/llm/chat、
  /api/chain/*（/run 即時、/chains 註冊複用、/async+輪詢、
  /ws/chain 逐節點進度推播）、/api/rag/sessions/*
  （含 /export|/import 知識庫持久化）、/api/render/*（headless
  render_* 工具直驅）、/api/ledger/sessions/*（AuditLedger）。
  chain spec 欄位名：expr/true/false/body/nodes/params/llm/
  renderTools/zetaTools/inputs（zetaTools 掛 zeta_* 數值
  工具——無狀態免鎖,renderTools 才需 g_renderMutex）。
- LLM 傳輸：OpenAIClient/AnthropicClient 走
  `Networking/HttpClient`（WinHTTP, HTTPS+HTTP）真 POST；
  config 帶 `baseURL` 可指 proxy/Ollama 等 OpenAI-compatible
  端點；LocalModelClient 有 baseURL 時也走 HTTP,無則回傳
  確定性模擬回應（測試用 fallback）。apiKey 只進 Authorization
  header,不落 log/檔案。
- web3-service：secp256k1 錢包/地址/公鑰、personal_sign 簽章與
  ecrecover 驗證、`did:potato:<address>` 身份；具名 agent
  DID（POST/GET /agents{name},GET/POST /agents/{id} 解析與
  agent 私鑰簽章——id 接受 name/did/0x 地址）；受管錢包持久化到
  `potato.web3.wallet-file`（預設 `web3-wallets.json`,gitignored,
  passphrase 有值時 AES-GCM(SHA-256(passphrase)) 加密私鑰,
  無則明文 hex 僅開發用）；
  `POST /api/web3/anchors` 帶 `ledgerSessionId` 會經 Eureka 取
  agent-service 的 ledger headHash 做外部錨定
  （backend=local：服務錢包簽名收據寫 `web3-anchors.jsonl`；
  backend=evm：EIP-155 自簽 tx 送 `potato.web3.rpc-url`）。
- Java 建置：parent `java/pom.xml` 固定 `release=25`+
  `<parameters>true`（Spring MVC 反射參數名必需；
  改 compiler 設定後要 `clean`——incremental 不看 flag）。
  Boot 4 HTTP converter 是 Jackson 3（`tools.jackson`），
  新端點原生 JSON 一律 String 透傳不經 Jackson 2 JsonNode；
  `AnchorService` 對外呼叫也吃 `Map` 不吃 JsonNode。
  `@LoadBalanced RestClient.Builder` 會攔截 Eureka 註冊請求
  （把 localhost 當服務名）——服務間呼叫用 `LoadBalancerClient`
  手動解析實例。
  Boot 4 的 @WebMvcTest 切片在獨立 starter：
  `spring-boot-starter-webmvc-test`（test scope）;
  `java.net.http.WebSocket` client 的 onText 覆寫後要
  `request(1)` 才會繼續收訊息。

## 建置與驗證

- 需求：CMake ≥3.15、C++20 編譯器、OpenGL；GLFW 優先本機
  （`-DGLFW_ROOT`），找不到走 FetchContent 3.3.8。
- `external/imgui` 是 git submodule：`git submodule update --init` 後才可建置
  （PortraitBaker 等 target 直編 imgui 源碼）。
- MinGW：`cmake -B build-mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release`
  ；`if(MINGW)` 已加 `-static-libgcc -static-libstdc++ -static`。
- MSVC：`cmake -B build -G "Visual Studio 18 2026" -A x64`。
- 測試：`ctest -C Release`（白名單 `POTATO_TESTS`，64 項引擎層測試；
  GL 依賴測試無顯示環境自行 SKIP）。
- 本地驗證標準：MSVC 與 MinGW 皆建置+ctest 全綠。
- 注意：repo 路徑含 CJK 時 `mingw32-make`（`Illegal byte sequence`）與
  ninja 的 ANSI stat 都會失敗——用 junction 指到 ASCII 路徑再 configure：
  `mklink /J F:\potato_pe F:\民國史詩\HWC\PotatoEngine`，
  `cmake -S F:\potato_pe -B F:\potato_pe\build-ninja -G Ninja`
  （GLFW 可重用 `-DFETCHCONTENT_SOURCE_DIR_GLFW=.../_deps/glfw-src`）。
- `assets/neural/` 為 gitignore 排除的本機產物：`NeuralArtTool`
  以 MiniMax-H3 示範影片/風格 GIF 抽幀重訓 `.pnn`（sr2x/denoise/
  colorize/terrain/normalmap + 六種 INR 紋理場含 papercraft/handdrawn/
  3danim 風格變體）；`NeuralArtDemo` 從 repo 根目錄執行載入推論。
  分析與產物清單見 `docs/MiniMax-H3-Analysis.md`。

## 規範

- Commit message 用 conventional commits（CI commitlint）。
- `external/` 既有第三方內容不改動；新增第三方僅限新增子目錄並附
  `README.md`（來源/版本/license）；實作巨集集中單一 TU。
- 不引第三方 JSON 庫——用 `Serialization/JsonParser.h`。
- 回覆與註解用繁體中文；識別符與檔名保持英文。
