# Third-Party Notices

PotatoEngine 使用下列第三方程式碼與函式庫，各依其授權條款散佈。
本檔彙整歸屬聲明；各元件完整授權文本見其原目錄或下文附錄。

## Godot Engine（參考實作／部分移植）

- 來源：https://github.com/godotengine/godot
- License：MIT
- 使用範圍：
  - `Core/RID.h`——`RID` / `RID_Alloc` 移植自 Godot
    `core/templates/rid.h` 與 `core/object/rid_owner.h`，
    保留其 validator／代際／free-list 語義（衍生作品，
    已於檔頭附 Godot 版權聲明）。
  - `Rendering/RenderingServer*.h/.cpp`、`Rendering/Renderer{Canvas,Scene}Cull.*`、
    `Rendering/Rasterizer{Canvas,Scene}.*`、`Rendering/RenderTypes.h`——
    分層與 API 語義（viewport/canvas/canvas_item/scenario/instance/
    light/camera RID API、z-order、modulate 疊加、剔除流程）
    參照 Godot `servers/rendering/`，實作為本專案重寫。

```
MIT License

Copyright (c) 2014-present Godot Engine contributors.
Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Dear ImGui（git submodule）

- 路徑：`external/imgui/`
- 來源：https://github.com/ocornut/imgui
- License：MIT（`external/imgui/LICENSE.txt`）

## glad（vendored 產生檔）

- 路徑：`external/glad_gen/`（glad 0.1.36 產生的 GL 3.3 core loader）
- 來源：https://github.com/Dav1dde/glad
- License：產生檔為 public domain；`KHR/khrplatform.h`
  為 Khronos Group 授權（MIT 風格條款，已附於檔頭）

## tinygltf 及其隨附檔（vendored）

- 路徑：`external/tinygltf/`，詳見其 `README.md`
- `tiny_gltf.h`：MIT（syoyo）
- `json.hpp`：MIT（nlohmann/json 3.10.4）
- `stb_image.h` / `stb_image_write.h`：public domain 或 MIT（擇一）

## miniaudio（vendored）

- 路徑：`external/miniaudio/`，詳見其 `README.md`
- License：public domain 或 MIT-0（擇一，見標頭檔尾部授權聲明）

## GLFW（建置期取得，未 vendored）

- 來源：https://github.com/glfw/glfw（CMake FetchContent 3.3.8
  或本機 `-DGLFW_ROOT`）
- License：zlib/libpng

## Java 依賴（Maven 取得，未 vendored）

- `java/` 下 Spring Boot / Spring Cloud 相依套件由 Maven 於
  建置期解析（Apache License 2.0 等），原始碼 repo 不含其程式碼。

## FFmpeg（執行期外部工具，未 vendored）

- `Media/VideoEncoder` 以子行程方式呼叫使用者的 `ffmpeg` CLI
  （stdin 餵 rawvideo）；引擎不含、亦不連結 FFmpeg 程式碼，
  不構成衍生作品。
- 注意：發佈物**不得隨附 ffmpeg 執行檔**——常見 ffmpeg build
  連結 GPL 元件（如 libx264），隨附會對該二進位產生 GPL
  散布義務。請指引使用者自行安裝 ffmpeg。
