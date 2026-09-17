# tinygltf (vendored)

Source: https://github.com/syoyo/tinygltf — release **v2.9.7** (2025-11-02).

Files:

- `tiny_gltf.h` — header-only glTF 2.0 loader (MIT)
- `json.hpp` — nlohmann/json 3.10.4, bundled by tinygltf (MIT)
- `stb_image.h` — stb_image v2.28, PNG/JPEG decoder (public domain / MIT)
- `stb_image_write.h` — stb_image_write (public domain / MIT)

v2.9.7 is used instead of v3.x because v3 introduced a new `tg3` API and
deprecated the classic `tinygltf::` interface used by `Rendering/ModelLoader.cpp`.

Implementation macros (`TINYGLTF_IMPLEMENTATION`, `STB_IMAGE_IMPLEMENTATION`,
`STB_IMAGE_WRITE_IMPLEMENTATION`) live in `Rendering/TinyGltfImpl.cpp` — do not
define them anywhere else.
