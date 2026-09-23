#pragma once

// ============================================================================
// 最小 JSON parser — 讓 ISerializable::Deserialize 真正能解析
// 支援: object / array / string(含 escape 與 \uXXXX) / number / true / false / null
// 用法: JsonValue v = JsonValue::Parse(text); v["name"].AsString();
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>

namespace Potato {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::unordered_map<std::string, JsonValue> objectValue;

    // ---- 建構 ----
    static JsonValue Null() { return JsonValue(); }
    static JsonValue Bool(bool b) { JsonValue v; v.type = Type::Bool; v.boolValue = b; return v; }
    static JsonValue Number(double n) { JsonValue v; v.type = Type::Number; v.numberValue = n; return v; }
    static JsonValue String(std::string s) { JsonValue v; v.type = Type::String; v.stringValue = std::move(s); return v; }

    // ---- 查詢 ----
    bool IsNull() const { return type == Type::Null; }
    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == Type::Array; }
    bool IsString() const { return type == Type::String; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsBool() const { return type == Type::Bool; }

    // 缺 key 回傳 Null,呼叫端可安全鏈式存取
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue nullVal;
        if (type != Type::Object) return nullVal;
        auto it = objectValue.find(key);
        return (it != objectValue.end()) ? it->second : nullVal;
    }
    const JsonValue& operator[](size_t index) const {
        static const JsonValue nullVal;
        if (type != Type::Array || index >= arrayValue.size()) return nullVal;
        return arrayValue[index];
    }
    size_t Size() const { return (type == Type::Array) ? arrayValue.size() : objectValue.size(); }

    // ---- 取值(型別不符回傳預設值,不拋例外)----
    std::string AsString(const std::string& def = "") const {
        if (type == Type::String) return stringValue;
        if (type == Type::Number) return std::to_string(numberValue);
        if (type == Type::Bool) return boolValue ? "true" : "false";
        return def;
    }
    double AsNumber(double def = 0.0) const {
        if (type == Type::Number) return numberValue;
        if (type == Type::String) { char* e = nullptr; double v = std::strtod(stringValue.c_str(), &e); return (e != stringValue.c_str()) ? v : def; }
        if (type == Type::Bool) return boolValue ? 1.0 : 0.0;
        return def;
    }
    float AsFloat(float def = 0.0f) const { return static_cast<float>(AsNumber(def)); }
    int AsInt(int def = 0) const {
        // double→int 超範圍/NaN 是 UB——先夾取再轉型;
        // NaN(如 AsNumber 走字串 strtod 路徑)回 def
        const double d = AsNumber(def);
        if (std::isnan(d)) return def;
        constexpr double kMax =
            static_cast<double>((std::numeric_limits<int>::max)());
        constexpr double kMin =
            static_cast<double>((std::numeric_limits<int>::min)());
        if (d >= kMax) return (std::numeric_limits<int>::max)();
        if (d <= kMin) return (std::numeric_limits<int>::min)();
        return static_cast<int>(d);
    }
    bool AsBool(bool def = false) const {
        if (type == Type::Bool) return boolValue;
        if (type == Type::Number) return numberValue != 0.0;
        if (type == Type::String) return stringValue == "true" || stringValue == "1";
        return def;
    }
    const std::vector<JsonValue>& AsArray() const {
        static const std::vector<JsonValue> empty;
        return (type == Type::Array) ? arrayValue : empty;
    }

    // ---- 解析 ----
    // 失敗回傳 Type::Null;可用 ParseOk 區分「合法的 null」與「解析失敗」
    static JsonValue Parse(const std::string& text) {
        Parser p(text);
        JsonValue v = p.ParseValue();
        return p.ok ? v : Null();
    }
    static bool ParseOk(const std::string& text, JsonValue& out) {
        Parser p(text);
        out = p.ParseValue();
        p.SkipWhitespace();
        return p.ok && p.pos >= text.size();
    }

private:
    struct Parser {
        const std::string& s;
        size_t pos = 0;
        bool ok = true;

        explicit Parser(const std::string& str) : s(str) {}

        void SkipWhitespace() {
            while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        char Peek() const { return (pos < s.size()) ? s[pos] : '\0'; }
        bool Consume(char c) {
            SkipWhitespace();
            if (Peek() == c) { ++pos; return true; }
            return false;
        }
        bool Match(const char* word) {
            size_t len = std::strlen(word);
            if (s.compare(pos, len, word) == 0) { pos += len; return true; }
            return false;
        }

        JsonValue ParseValue() {
            SkipWhitespace();
            switch (Peek()) {
                case '{': return ParseObject();
                case '[': return ParseArray();
                case '"': return String(ParseString());
                case 't': if (Match("true")) return Bool(true); break;
                case 'f': if (Match("false")) return Bool(false); break;
                case 'n': if (Match("null")) return Null(); break;
                default:
                    if (Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
                        return ParseNumber();
                    }
            }
            ok = false;
            return Null();
        }

        JsonValue ParseObject() {
            ++pos; // '{'
            JsonValue obj; obj.type = Type::Object;
            SkipWhitespace();
            if (Consume('}')) return obj;
            while (ok) {
                SkipWhitespace();
                if (Peek() != '"') { ok = false; return Null(); }
                std::string key = ParseString();
                if (!ok || !Consume(':')) { ok = false; return Null(); }
                obj.objectValue[std::move(key)] = ParseValue();
                if (!ok) return Null();
                if (Consume('}')) return obj;
                if (!Consume(',')) { ok = false; return Null(); }
            }
            return Null();
        }

        JsonValue ParseArray() {
            ++pos; // '['
            JsonValue arr; arr.type = Type::Array;
            SkipWhitespace();
            if (Consume(']')) return arr;
            while (ok) {
                arr.arrayValue.push_back(ParseValue());
                if (!ok) return Null();
                if (Consume(']')) return arr;
                if (!Consume(',')) { ok = false; return Null(); }
            }
            return Null();
        }

        JsonValue ParseNumber() {
            size_t start = pos;
            if (Peek() == '-') ++pos;
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) ||
                   s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E' ||
                   s[pos] == '+' || s[pos] == '-')) ++pos;
            if (pos == start) { ok = false; return Null(); }
            return Number(std::strtod(s.substr(start, pos - start).c_str(), nullptr));
        }

        std::string ParseString() {
            ++pos; // '"'
            std::string out;
            while (pos < s.size()) {
                char c = s[pos++];
                if (c == '"') return out;
                if (c == '\\' && pos < s.size()) {
                    char esc = s[pos++];
                    switch (esc) {
                        case '"': out += '"'; break;
                        case '\\': out += '\\'; break;
                        case '/': out += '/'; break;
                        case 'b': out += '\b'; break;
                        case 'f': out += '\f'; break;
                        case 'n': out += '\n'; break;
                        case 'r': out += '\r'; break;
                        case 't': out += '\t'; break;
                        case 'u': {
                            if (pos + 4 > s.size()) { ok = false; return out; }
                            unsigned cp = std::strtoul(s.substr(pos, 4).c_str(), nullptr, 16);
                            pos += 4;
                            // UTF-8 encode(不處理 surrogate pair,超出 BMP 拆成 '?' 足夠引擎用)
                            if (cp < 0x80) {
                                out += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                out += static_cast<char>(0xC0 | (cp >> 6));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                out += static_cast<char>(0xE0 | (cp >> 12));
                                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                            break;
                        }
                        default: ok = false; return out;
                    }
                } else {
                    out += c;
                }
            }
            ok = false; // 未閉合字串
            return out;
        }
    };
};

} // namespace Potato
