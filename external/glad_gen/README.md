# glad_gen（vendored）

glad 產生的 OpenGL 3.3 core profile 函式載入器——引擎 GL 入口點的唯一來源。

- 來源：https://github.com/Dav1dde/glad（產生器版本 0.1.36，產生時間見 `glad.h` 標頭註解）
- 組態：C/C++ generator、gl=3.3、core profile、extensions 見 `glad.h` 標頭清單
- License：
  - glad 產生檔（`include/glad/glad.h`、`src/glad.c`）：public domain——
    glad 產生器輸出不附授權義務
  - `include/KHR/khrplatform.h`：Khronos Group 授權條款（MIT 風格，
    已附於檔頭，Copyright (c) 2008-2018 The Khronos Group Inc.）

## 使用方式

- `PotatoEngine` lib 以 `GLFW_INCLUDE_NONE` 阻止 GLFW 帶入系統 `gl.h`；
  所有 GL 函式一律走 `#include <glad/glad.h>` 並於 context 建立後
  呼叫 `gladLoadGLLoader`。
- 需要系統 `gl.h` 的 target（如 IDE_GUI）不得連結引擎 lib——見
  AGENTS.md「消費關係」。
