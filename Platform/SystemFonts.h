#pragma once
// SystemFonts — cross-platform font discovery (header-only).
//
// Replaces hard-coded "C:/Windows/Fonts/..." paths. Each lookup walks:
//   1. an explicit env override (e.g. POTATO_CJK_FONT=/path/to/font.ttc)
//   2. a platform candidate list (Windows → Linux Noto/fontconfig dirs
//      → macOS), returning the first file that exists.
// Missing fonts are never fatal: callers get an empty string and degrade.

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <system_error>

namespace Potato::Platform {

inline std::string EnvironmentValue(const char* name) {
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0;
    if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

inline bool FontFileExists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

inline std::string FirstExistingFont(const char* envOverride,
                                     std::initializer_list<const char*> candidates) {
    if (envOverride != nullptr) {
        const std::string env = EnvironmentValue(envOverride);
        if (FontFileExists(env)) return env;
    }
    for (const char* p : candidates) {
        if (FontFileExists(p)) return p;
    }
    return {};
}

// Traditional-Chinese-capable fonts, thematic order first on Windows
// (標楷體 → 正黑 → 細明 → 雅黑 → 宋體), then Linux Noto CJK / AR PL /
// WenQuanYi locations (Debian/Ubuntu, Fedora, Arch), then macOS.
// Env override: POTATO_CJK_FONT.
inline std::initializer_list<const char*> CjkFontCandidates() {
    static const std::initializer_list<const char*> k = {
        "C:/Windows/Fonts/kaiu.ttf",
        "C:/Windows/Fonts/msjh.ttc",
        "C:/Windows/Fonts/mingliu.ttc",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/arphic/ukai.ttc",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
    };
    return k;
}

inline std::string FindCjkFont() {
    return FirstExistingFont("POTATO_CJK_FONT", CjkFontCandidates());
}

// Bold CJK face (falls back to the regular CJK font). Env: POTATO_CJK_FONT_BOLD.
inline std::string FindCjkBoldFont() {
    std::string p = FirstExistingFont("POTATO_CJK_FONT_BOLD", {
        "C:/Windows/Fonts/msjhbd.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/System/Library/Fonts/PingFang.ttc",
    });
    return p.empty() ? FindCjkFont() : p;
}

} // namespace Potato::Platform
