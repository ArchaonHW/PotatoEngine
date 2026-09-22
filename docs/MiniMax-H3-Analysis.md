# MiniMax-H3 分析與引擎產物對接

來源：`C:\HWC\MiniMax-H3-main`（MiniMax Hailuo 3 開源釋出,Hugging Face
diffusers pipeline)。本文件記錄其架構分析,以及已轉入 PotatoEngine 的產物。

## 系統分析

H3 是**全模態視訊+音訊生成模型**：文字/圖像/視訊/音訊混合輸入 →
輸出最長 15 秒、最高 2K、24 FPS、32 kHz 立體聲的影片。

### 三段式管線

| 階段 | 模組 | 說明 |
|---|---|---|
| 1 | H3-Context-IR | 多模態指令理解→結構化中間表示（託管服務,未開源,走 API） |
| 2 | H3-Base | Omni-Transformer 生成 768p 影片+音訊 |
| 3 | H3-Regenerate-2K | 768p + 原始上下文回送 → 2K 重生成 |

### 開源元件（本地 `*/config.json` + safetensors index)

| 元件 | 規格 |
|---|---|
| `text_encoder/` | Qwen3VL（視覺語言編碼器）+ Qwen2TokenizerFast |
| `transformer/` | H3-Omni-Transformer：50 層 + 2 refiner、hidden 5376、
56 頭 × 128 維、FFN 14336、RoPE 時空位置編碼（約 16B 級 DiT） |
| `transformer_ref/` | Ref2VA 全參考模式變體 |
| `vae/` | 視覺 VAE（latent 編解碼） |
| `audio_vae/` | DAC 系音訊 VAE → 32 kHz 立體聲 |
| `scheduler/` `audio_scheduler/` | 擴散排程器 |
| `FL2VA/` | 首尾幀模式變體（0/1/2 張圖輸入） |
| `scripts/readme/` | 完整 2K 工作流 shell 範例（i2va / ref2va） |
| `skills/` | 9 個提示詞技能；`h3-prompt-writing` 為純 Markdown 可移植 |

### 執行需求

torch ≥2.4 + diffusers（`trust_remote_code=True`）+ GPU——
**引擎無法本地執行此模型**;本 repo 的價值在示範素材與架構概念。

## 已轉入引擎的產物

### `assets/neural/`（gitignore 本機產物,NeuralArtTool 可再生）

以 `assets/` 示範影片與風格 GIF 抽幀（128×128 PNG）訓練的
`Rendering/NeuralGraphics` `.pnn` 權重：

| 權重 | 來源 | 用途 | loss |
|---|---|---|---|
| `h3_sr2x.pnn` | h3_direct_768p.mp4 | 2× 超解析 | 0.00118 |
| `h3_denoise.pnn` | i2va.mp4 | 降噪 σ=0.15 | 0.00207 |
| `h3_colorize.pnn` | ref2va.mp4 | 灰階上色 | 0.00137 |
| `h3_texfield.pnn` | t2va.mp4 | INR 紋理場 | 0.00531 |
| `h3_terrain.pnn` | t2va.mp4 | 地形高度場 | 0.00529 |
| `h3_normalmap.pnn` | h3_direct_768p.mp4 | 法線貼圖 | 0.00005 |
| `h3_fl2va_texfield.pnn` | fl2va.mp4 | 風格紋理場 | 0.00310 |
| `h3_r2va_texfield.pnn` | r2va.mp4 | 風格紋理場 | 0.00239 |
| `h3_papercraft_texfield.pnn` | papercraft GIF | 紙藝風格場 | 0.00625 |
| `h3_handdrawn_texfield.pnn` | handdrawn GIF | 手繪風格場 | 0.00267 |
| `h3_3danim_texfield.pnn` | 3d-animation GIF | 3D 動畫風格場 | 0.00407 |

驗證：`Examples/NeuralArtDemo.cpp` 載入六個核心權重跑推論；
風格場可用 `NeuralArtTool texgen <out.png> <model.pnn> 64 64 <z...>` 產圖。

### 架構概念對映（H3 → 引擎已有能力）

| H3 | PotatoEngine |
|---|---|
| VisualVAE latent 空間生成 | `NeuralFieldImage`(INR 坐標網路=小型 latent 生成器） |
| AudioVAE → 波形 | `Audio` 模組 + miniaudio 後端（尚無神經音訊生成） |
| 擴散 scheduler | 無；`AI/NeuralNetwork` 為單次前饋，無疊代採樣 |
| FL2VA 首尾幀插值 | `Rendering` 有影像基元，可做 latent 插值近似 |
| H3-Context-IR 提示詞結構化 | `skills/h3-prompt-writing` 為純 Markdown,
可掛 `.devin/skills/` 供代理撰寫 H3 影片提示詞 |

## 後續可接點

- **風格場消費**：遊戲層可用 `NeuralFieldImage::Generate(latent)` 以
  非零 latent 在訓練風格附近採樣程序化貼圖（紙藝/手繪 UI 素材風格）
- **音訊 VAE 概念**：若需神經音訊生成，可在 `Audio` 增小型
  mel→waveform MLP（對應 DAC 思路）
- **API 接入**：H3 正式生成走 MiniMax 平台 API——引擎不內建 HTTP
  client，遊戲層（MingGoRTS `build-video`）才是接入點
