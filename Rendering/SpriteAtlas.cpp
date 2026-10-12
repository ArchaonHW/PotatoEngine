#include "Rendering/SpriteAtlas.h"

#include "Rendering/ImageCodec.h"
#include "Serialization/JsonParser.h"

#include <fstream>
#include <sstream>

namespace Potato {

namespace {

std::string DirOf(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    return (sep == std::string::npos) ? "." : path.substr(0, sep);
}

bool ReadTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

bool SpriteAtlas::LoadIndex(const std::string& jsonPath) {
    std::string text;
    JsonValue root;
    if (!ReadTextFile(jsonPath, text) ||
        !JsonValue::ParseOk(text, root) ||
        root["schema"].AsString() != "potato.sprite_atlas/1") {
        return false;
    }

    const std::string img = root["image"].AsString();
    if (img.empty()) return false;

    std::unordered_map<std::string, SpriteFrame> frames;
    const JsonValue& fj = root["frames"];
    if (!fj.IsObject() || fj.Size() == 0) return false;
    for (const auto& kv : fj.objectValue) {
        const std::vector<JsonValue>& r = kv.second.AsArray();
        if (r.size() != 4) return false;
        SpriteFrame fr;
        fr.x = r[0].AsInt(-1);
        fr.y = r[1].AsInt(-1);
        fr.w = r[2].AsInt(-1);
        fr.h = r[3].AsInt(-1);
        if (fr.x < 0 || fr.y < 0 || fr.w <= 0 || fr.h <= 0) return false;
        frames[kv.first] = fr;
    }

    // default 可選；有寫就必須指到存在的 frame
    const std::string def = root["default"].AsString();
    if (!def.empty() && frames.find(def) == frames.end()) return false;

    int dw = 0, dh = 0;
    const JsonValue& sj = root["size"];
    if (sj.IsArray() && sj.AsArray().size() == 2) {
        dw = sj.AsArray()[0].AsInt();
        dh = sj.AsArray()[1].AsInt();
        if (dw <= 0 || dh <= 0) return false;
    }

    // 全部驗過才提交——失敗不留下半套索引
    imagePath_ = DirOf(jsonPath) + "/" + img;
    defaultName_ = def;
    frames_ = std::move(frames);
    declW_ = dw;
    declH_ = dh;
    pixels_.clear();
    w_ = h_ = 0;
    indexLoaded_ = true;
    FillUvs();
    return true;
}

bool SpriteAtlas::LoadPixels() {
    if (!indexLoaded_ || imagePath_.empty()) return false;

    std::ifstream f(imagePath_, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string blob = ss.str();

    std::vector<uint8> px;
    int w = 0, h = 0;
    std::string err;
    if (!ImageCodec::DecodeImage(
            reinterpret_cast<const uint8*>(blob.data()), blob.size(),
            px, w, h, &err)) {
        return false;
    }
    // 索引有宣告尺寸就必須一致——不一致代表索引/圖檔版本錯配
    if (declW_ > 0 && (w != declW_ || h != declH_)) return false;

    pixels_ = std::move(px);
    w_ = w;
    h_ = h;
    declW_ = w;
    declH_ = h;
    FillUvs();
    return true;
}

bool SpriteAtlas::LoadFromFile(const std::string& jsonPath) {
    return LoadIndex(jsonPath) && LoadPixels();
}

const SpriteFrame* SpriteAtlas::Find(const std::string& name) const {
    auto it = frames_.find(name);
    if (it != frames_.end()) return &it->second;
    if (!defaultName_.empty()) {
        auto d = frames_.find(defaultName_);
        if (d != frames_.end()) return &d->second;
    }
    return nullptr;
}

void SpriteAtlas::FillUvs() {
    if (declW_ <= 0 || declH_ <= 0) return;
    const float iw = 1.0f / declW_, ih = 1.0f / declH_;
    for (auto& kv : frames_) {
        SpriteFrame& f = kv.second;
        f.u0 = f.x * iw;
        f.v0 = f.y * ih;
        f.u1 = (f.x + f.w) * iw;
        f.v1 = (f.y + f.h) * ih;
    }
}

} // namespace Potato
