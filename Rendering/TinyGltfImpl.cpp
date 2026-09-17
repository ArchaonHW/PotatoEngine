// tinygltf/stb 實作單元：implementation 巨集全專案只能定義一次，
// 集中在此 TU，避免其他 include tiny_gltf.h 的檔案重複產生符號。

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
