#include "Serialization.h"
#include <iostream>
#include <algorithm>
#include <cstdio>

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
    // 實際應該使用文件系統創建目錄
    
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

template<typename T>
bool SerializationManager::Serialize(const std::string& filePath, const T& object, SerializationFormat format) {
    ISerializer* serializer = GetSerializer(format);
    if (!serializer) {
        std::cerr << "No serializer registered for format: " << static_cast<int>(format) << std::endl;
        return false;
    }
    
    return serializer->Serialize(filePath, object);
}

template<typename T>
bool SerializationManager::Deserialize(const std::string& filePath, T& object, SerializationFormat format) {
    ISerializer* serializer = GetSerializer(format);
    if (!serializer) {
        std::cerr << "No serializer registered for format: " << static_cast<int>(format) << std::endl;
        return false;
    }
    
    return serializer->Deserialize(filePath, object);
}

template<typename T>
bool SerializationManager::SaveGame(const std::string& saveSlot, const T& gameState) {
    std::string filePath = GetSaveSlotPath(saveSlot);
    return Serialize(filePath, gameState, SerializationFormat::JSON);
}

template<typename T>
bool SerializationManager::LoadGame(const std::string& saveSlot, T& gameState) {
    std::string filePath = GetSaveSlotPath(saveSlot);
    return Deserialize(filePath, gameState, SerializationFormat::JSON);
}

std::vector<std::string> SerializationManager::GetSaveSlots() const {
    std::vector<std::string> saveSlots;
    
    // 簡化實現：應該掃描存檔目錄
    saveSlots.push_back("auto_save_1");
    saveSlots.push_back("auto_save_2");
    saveSlots.push_back("manual_save_1");
    saveSlots.push_back("manual_save_2");
    
    return saveSlots;
}

bool SerializationManager::DeleteSaveSlot(const std::string& saveSlot) {
    std::string filePath = GetSaveSlotPath(saveSlot);
    
    // 刪除文件
    // 簡化實現：使用文件系統刪除
    std::cout << "Deleted save slot: " << saveSlot << std::endl;
    
    return true;
}

bool SerializationManager::SaveSlotExists(const std::string& saveSlot) const {
    std::string filePath = GetSaveSlotPath(saveSlot);
    
    // 檢查文件是否存在
    // 簡化實現：使用文件系統檢查
    return false;
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
    std::string sanitized;
    sanitized.reserve(saveSlot.size());
    for (char c : saveSlot) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            sanitized += c;
        }
    }
    if (sanitized.empty()) {
        sanitized = "invalid_slot";
    }
    return saveDirectory + "/" + sanitized + ".json";
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

bool SceneNodeData::Deserialize(const std::string& data) {
    // 簡化實現：解析JSON字符串
    // 實際應該使用JSON解析庫
    std::cout << "Deserializing SceneNodeData: " << data << std::endl;
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
    // 簡化實現：解析JSON字符串
    std::cout << "Deserializing GameObjectData: " << data << std::endl;
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
    // 簡化實現：解析JSON字符串
    std::cout << "Deserializing GameStateData: " << data << std::endl;
    return true;
}

} // namespace SerializableTypes

} // namespace Potato