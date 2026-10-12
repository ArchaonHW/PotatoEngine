#include "Serialization.h"
#include "JsonParser.h"
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace Potato {

// 全局序列化管理器
SerializationManager* gSerializationManager = nullptr;

// ============================================================================
// JsonSerializer 實現
// ============================================================================

JsonSerializer::JsonSerializer() {
}

JsonSerializer::~JsonSerializer() {
}

bool JsonSerializer::Serialize(const std::string& filePath, const ISerializable& object) {
    std::string jsonData = SerializeToString(object);
    
    std::ofstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for JSON serialization: " << filePath << std::endl;
        return false;
    }
    
    file << jsonData;
    file.close();
    
    std::cout << "Serialized object to JSON: " << filePath << std::endl;
    return true;
}

bool JsonSerializer::Deserialize(const std::string& filePath, ISerializable& object) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for JSON deserialization: " << filePath << std::endl;
        return false;
    }
    
    std::string jsonData((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    return DeserializeFromString(jsonData, object);
}

std::string JsonSerializer::SerializeToString(const ISerializable& object) {
    return object.Serialize();
}

bool JsonSerializer::DeserializeFromString(const std::string& data, ISerializable& object) {
    return object.Deserialize(data);
}

// ============================================================================
// BinarySerializer 實現
// ============================================================================

BinarySerializer::BinarySerializer() {
}

BinarySerializer::~BinarySerializer() {
}

bool BinarySerializer::Serialize(const std::string& filePath, const ISerializable& object) {
    std::vector<uint8> binaryData = SerializeToBinary(object);
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for binary serialization: " << filePath << std::endl;
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(binaryData.data()), binaryData.size());
    file.close();
    
    std::cout << "Serialized object to binary: " << filePath << std::endl;
    return true;
}

bool BinarySerializer::Deserialize(const std::string& filePath, ISerializable& object) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file for binary deserialization: " << filePath << std::endl;
        return false;
    }
    
    // 獲取文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // 讀取二進制數據
    std::vector<uint8> binaryData(fileSize);
    file.read(reinterpret_cast<char*>(binaryData.data()), fileSize);
    file.close();
    
    return DeserializeFromBinary(binaryData, object);
}

std::vector<uint8> BinarySerializer::SerializeToBinary(const ISerializable& object) {
    std::string jsonData = object.Serialize();
    
    std::vector<uint8> binaryData(jsonData.begin(), jsonData.end());
    return binaryData;
}

bool BinarySerializer::DeserializeFromBinary(const std::vector<uint8>& data, ISerializable& object) {
    std::string jsonData(data.begin(), data.end());
    return object.Deserialize(jsonData);
}

// ============================================================================
// SerializationManager 實現
// ============================================================================

SerializationManager::SerializationManager()
    : saveDirectory("./Saves")
    , autoSaveEnabled(false)
    , autoSaveInterval(300.0f) // 5 minutes
    , autoSaveTimer(0.0f)
    , maxAutoSaves(5)
    , currentAutoSaveIndex(0)
    , initialized(false)
{
}

SerializationManager::~SerializationManager() {
    Shutdown();
}

void SerializationManager::Initialize() {
    if (initialized) return;
    
    // 註冊默認序列化器
    RegisterSerializer(SerializationFormat::JSON, MakeShared<JsonSerializer>());
    RegisterSerializer(SerializationFormat::Binary, MakeShared<BinarySerializer>());
    
    // 創建存檔目錄
    std::error_code ec;
    std::filesystem::create_directories(saveDirectory, ec);
    
    initialized = true;
    std::cout << "Serialization Manager initialized" << std::endl;
}

void SerializationManager::Shutdown() {
    if (!initialized) return;
    
    serializers.clear();
    
    initialized = false;
    std::cout << "Serialization Manager shutdown complete" << std::endl;
}

void SerializationManager::RegisterSerializer(SerializationFormat format, SharedPtr<ISerializer> serializer) {
    serializers[format] = serializer;
    std::cout << "Registered serializer for format: " << static_cast<int>(format) << std::endl;
}

ISerializer* SerializationManager::GetSerializer(SerializationFormat format) {
    auto it = serializers.find(format);
    if (it != serializers.end()) {
        return it->second.get();
    }
    return nullptr;
}

namespace {
// 存檔名只允許英數字、底線、連字號——GetSaveSlotPath 與
// GetSaveSlots 必須共用同一個規則,否則列出的 slot 會對不上路徑
std::string SanitizeSlotName(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            sanitized += c;
        }
    }
    return sanitized.empty() ? "invalid_slot" : sanitized;
}
} // namespace

std::vector<std::string> SerializationManager::GetSaveSlots() const {
    std::vector<std::string> saveSlots;

    // 掃描存檔目錄中的 .json 檔。只列出 stem 本身已合規的檔案
    // （SanitizeSlotName(stem) == stem）：這保證每個列出的 slot
    // 都能被 SaveSlotExists/DeleteSaveSlot 經 GetSaveSlotPath 操作。
    // 例如 "a.b.json" 的 stem sanitize 後是 "ab" ≠ "a.b",
    // 列出它也無法對回實際檔案,直接排除
    std::error_code ec;
    if (!std::filesystem::is_directory(saveDirectory, ec)) {
        return saveSlots;
    }
    for (const auto& entry : std::filesystem::directory_iterator(saveDirectory, ec)) {
        std::error_code entryEc;
        if (entry.is_regular_file(entryEc) && !entryEc &&
            entry.path().extension() == ".json") {
            std::string stem = entry.path().stem().string();
            if (SanitizeSlotName(stem) == stem) {
                saveSlots.push_back(stem);
            }
        }
    }
    std::sort(saveSlots.begin(), saveSlots.end());
    return saveSlots;
}

bool SerializationManager::DeleteSaveSlot(const std::string& saveSlot) {
    std::string filePath = GetSaveSlotPath(saveSlot);

    std::error_code ec;
    bool removed = std::filesystem::remove(filePath, ec);
    if (removed) {
        std::cout << "Deleted save slot: " << saveSlot << std::endl;
    }
    return removed;
}

bool SerializationManager::SaveSlotExists(const std::string& saveSlot) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(GetSaveSlotPath(saveSlot), ec);
}

void SerializationManager::EnableAutoSave(bool enable) {
    autoSaveEnabled = enable;
    std::cout << "Auto save " << (enable ? "enabled" : "disabled") << std::endl;
}

void SerializationManager::SetAutoSaveInterval(float interval) {
    autoSaveInterval = std::max(60.0f, interval); // 最小1分鐘
    std::cout << "Auto save interval set to: " << autoSaveInterval << " seconds" << std::endl;
}

void SerializationManager::SetMaxAutoSaves(int maxSaves) {
    maxAutoSaves = std::max(1, maxSaves);
    std::cout << "Max auto saves set to: " << maxAutoSaves << std::endl;
}

void SerializationManager::Update(float deltaTime) {
    if (!autoSaveEnabled) return;
    
    autoSaveTimer += deltaTime;
    
    if (autoSaveTimer >= autoSaveInterval) {
        ProcessAutoSave();
        autoSaveTimer = 0.0f;
    }
}

std::string SerializationManager::GetSaveSlotPath(const std::string& saveSlot) const {
    // 防止路徑遍歷：存檔名稱只允許英數字、底線、連字號
    return saveDirectory + "/" + SanitizeSlotName(saveSlot) + ".json";
}

void SerializationManager::ProcessAutoSave() {
    std::string saveSlot = "auto_save_" + std::to_string(currentAutoSaveIndex + 1);
    currentAutoSaveIndex = (currentAutoSaveIndex + 1) % maxAutoSaves;
    
    std::cout << "Auto saving to slot: " << saveSlot << std::endl;
    
    // 實際應該保存當前遊戲狀態
    // 這需要遊戲狀態的引用
}

// ============================================================================
// 全局函數實現
// ============================================================================

bool InitializeSerializationManager() {
    if (gSerializationManager) {
        return false;
    }
    
    gSerializationManager = new SerializationManager();
    gSerializationManager->Initialize();
    
    return true;
}

void ShutdownSerializationManager() {
    if (gSerializationManager) {
        delete gSerializationManager;
        gSerializationManager = nullptr;
    }
}

SerializationManager* GetSerializationManager() {
    return gSerializationManager;
}

// ============================================================================
// 可序列化類型實現
// ============================================================================

namespace SerializableTypes {

// 跳脫 JSON 字串中的特殊字元，防止產生不合法 JSON 與注入
static std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string SceneNodeData::Serialize() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"name\":\"" << EscapeJsonString(name) << "\",";
    ss << "\"position\":[" << position.x << "," << position.y << "," << position.z << "],";
    ss << "\"rotation\":[" << rotation.x << "," << rotation.y << "," << rotation.z << "," << rotation.w << "],";
    ss << "\"scale\":[" << scale.x << "," << scale.y << "," << scale.z << "],";
    ss << "\"children\":[";
    for (size_t i = 0; i < children.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << EscapeJsonString(children[i]) << "\"";
    }
    ss << "]}";
    return ss.str();
}

// 從 JsonValue array 讀 Vector3([x,y,z])
static bool ReadVector3(const JsonValue& v, Vector3& out) {
    if (!v.IsArray() || v.Size() < 3) return false;
    out.x = v[0].AsFloat();
    out.y = v[1].AsFloat();
    out.z = v[2].AsFloat();
    return true;
}

// 從 JsonValue array 讀 Quaternion([x,y,z,w])
static bool ReadQuaternion(const JsonValue& v, Quaternion& out) {
    if (!v.IsArray() || v.Size() < 4) return false;
    out.x = v[0].AsFloat();
    out.y = v[1].AsFloat();
    out.z = v[2].AsFloat();
    out.w = v[3].AsFloat();
    return true;
}

bool SceneNodeData::Deserialize(const std::string& data) {
    JsonValue root;
    if (!JsonValue::ParseOk(data, root) || !root.IsObject()) {
        return false;
    }
    name = root["name"].AsString();
    ReadVector3(root["position"], position);
    ReadQuaternion(root["rotation"], rotation);
    ReadVector3(root["scale"], scale);
    children.clear();
    for (const auto& c : root["children"].AsArray()) {
        children.push_back(c.AsString());
    }
    return true;
}

std::string GameObjectData::Serialize() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"name\":\"" << EscapeJsonString(name) << "\",";
    ss << "\"tag\":\"" << EscapeJsonString(tag) << "\",";
    ss << "\"layer\":" << layer << ",";
    ss << "\"active\":" << (active ? "true" : "false") << ",";
    ss << "\"sceneNodeData\":\"" << EscapeJsonString(sceneNodeData) << "\"";
    ss << "}";
    return ss.str();
}

bool GameObjectData::Deserialize(const std::string& data) {
    JsonValue root;
    if (!JsonValue::ParseOk(data, root) || !root.IsObject()) {
        return false;
    }
    name = root["name"].AsString();
    tag = root["tag"].AsString();
    layer = root["layer"].AsInt();
    active = root["active"].AsBool(true);
    sceneNodeData = root["sceneNodeData"].AsString();
    return true;
}

std::string GameStateData::Serialize() const {
    std::stringstream ss;
    ss << "{";
    ss << "\"levelName\":\"" << EscapeJsonString(levelName) << "\",";
    ss << "\"playTime\":" << playTime << ",";
    ss << "\"score\":" << score << ",";
    ss << "\"activeObjects\":[";
    for (size_t i = 0; i < activeObjects.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << EscapeJsonString(activeObjects[i]) << "\"";
    }
    ss << "]}";
    return ss.str();
}

bool GameStateData::Deserialize(const std::string& data) {
    JsonValue root;
    if (!JsonValue::ParseOk(data, root) || !root.IsObject()) {
        return false;
    }
    levelName = root["levelName"].AsString();
    playTime = root["playTime"].AsFloat();
    score = root["score"].AsInt();
    activeObjects.clear();
    for (const auto& o : root["activeObjects"].AsArray()) {
        activeObjects.push_back(o.AsString());
    }
    return true;
}

} // namespace SerializableTypes

} // namespace Potato