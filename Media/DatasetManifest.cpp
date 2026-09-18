#include "DatasetManifest.h"

#include "Serialization/JsonParser.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace Potato {
namespace Media {

// JsonValue 沒有內建 dump——manifest 只需保守輸出（保留 extras 用）
static void DumpJson(const JsonValue& v, std::string& out) {
    switch (v.type) {
    case JsonValue::Type::Null:   out += "null"; break;
    case JsonValue::Type::Bool:   out += v.AsBool() ? "true" : "false"; break;
    case JsonValue::Type::Number: {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.10g", v.AsNumber());
        out += buf;
        break;
    }
    case JsonValue::Type::String: {
        out += '"';
        const std::string s = v.AsString();
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        out += '"';
        break;
    }
    case JsonValue::Type::Array: {
        out += '[';
        const auto& arr = v.AsArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) out += ',';
            DumpJson(arr[i], out);
        }
        out += ']';
        break;
    }
    case JsonValue::Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& kv : v.objectValue) {
            if (!first) out += ',';
            first = false;
            out += '"';
            for (char c : kv.first) {
                if (c == '"' || c == '\\') { out += '\\'; out += c; }
                else out += c;
            }
            out += "\":";
            DumpJson(kv.second, out);
        }
        out += '}';
        break;
    }
    }
}

std::string DatasetManifest::SchemaTag() const {
    return "potato." + kind + "_dataset/1";
}

std::string DatasetManifest::ToJson() const {
    std::string out = "{\"schema\":\"" + SchemaTag() + "\"";
    char buf[64];
    std::snprintf(buf, sizeof(buf), ",\"seed\":%d", seed);
    out += buf;
    out += ",\"episodes\":[";
    for (size_t i = 0; i < episodes.size(); ++i) {
        out += "\"" + episodes[i] + "\"";
        if (i + 1 < episodes.size()) out += ",";
    }
    out += "]";
    if (!extrasJson.empty()) {
        out += ",\"extras\":" + extrasJson;
    }
    out += "}";
    return out;
}

bool DatasetManifest::FromJson(const std::string& json) {
    JsonValue root;
    if (!JsonValue::ParseOk(json, root)) return false;
    const std::string tag = root["schema"].AsString();
    // 只吃 potato.<kind>_dataset/1；取出 kind 段
    const std::string pre = "potato.", suf = "_dataset/1";
    if (tag.size() <= pre.size() + suf.size() ||
        tag.compare(0, pre.size(), pre) != 0 ||
        tag.compare(tag.size() - suf.size(), suf.size(), suf) != 0) {
        return false;
    }
    DatasetManifest m;
    m.kind = tag.substr(pre.size(), tag.size() - pre.size() - suf.size());
    m.seed = root["seed"].AsInt(0);
    for (const JsonValue& e : root["episodes"].AsArray()) {
        m.episodes.push_back(e.AsString());
    }
    const JsonValue& extras = root["extras"];
    if (extras.IsObject() || extras.IsArray()) {
        DumpJson(extras, m.extrasJson);
    }
    *this = m; // 全部成功才覆寫
    return true;
}

bool DatasetManifest::LoadFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return FromJson(ss.str());
}

bool DatasetManifest::SaveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << ToJson();
    return out.good();
}

} // namespace Media
} // namespace Potato
