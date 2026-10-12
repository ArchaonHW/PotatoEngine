#pragma once

#include "Core/CoreTypes.h"
#include "MathUtils/Vector3.h"
#include "MathUtils/Quaternion.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>

namespace Potato {

/**
 * 序列化接口
 */
class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual std::string Serialize() const = 0;
    virtual bool Deserialize(const std::string& data) = 0;
    virtual std::string GetSerializationType() const = 0;
};

/**
 * 序列化格式枚舉
 */
enum class SerializationFormat {
    JSON,
    Binary,
    XML,
    Custom
};

/**
 * 序列化器基類
 */
class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    virtual bool Serialize(const std::string& filePath, const ISerializable& object) = 0;
    virtual bool Deserialize(const std::string& filePath, ISerializable& object) = 0;
    virtual SerializationFormat GetFormat() const = 0;
};

/**
 * JSON 序列化器
 */
class JsonSerializer : public ISerializer {
public:
    JsonSerializer();
    ~JsonSerializer() override;
    
    bool Serialize(const std::string& filePath, const ISerializable& object) override;
    bool Deserialize(const std::string& filePath, ISerializable& object) override;
    SerializationFormat GetFormat() const override { return SerializationFormat::JSON; }
    
    // JSON 特定方法
    static std::string SerializeToString(const ISerializable& object);
    static bool DeserializeFromString(const std::string& data, ISerializable& object);
};

/**
 * 二進制序列化器
 */
class BinarySerializer : public ISerializer {
public:
    BinarySerializer();
    ~BinarySerializer() override;
    
    bool Serialize(const std::string& filePath, const ISerializable& object) override;
    bool Deserialize(const std::string& filePath, ISerializable& object) override;
    SerializationFormat GetFormat() const override { return SerializationFormat::Binary; }
    
    // 二進制特定方法
    static std::vector<uint8> SerializeToBinary(const ISerializable& object);
    static bool DeserializeFromBinary(const std::vector<uint8>& data, ISerializable& object);
};

/**
 * 序列化管理器
 * 管理序列化器和序列化操作
 */
class SerializationManager {
public:
    SerializationManager();
    ~SerializationManager();
    
    // 初始化和關閉
    void Initialize();
    void Shutdown();
    
    // 序列化器管理
    void RegisterSerializer(SerializationFormat format, SharedPtr<ISerializer> serializer);
    ISerializer* GetSerializer(SerializationFormat format);
    
    // 便捷序列化方法(T 須實作 ISerializable;定義放 header 使各 TU 可實例化)
    template<typename T>
    bool Serialize(const std::string& filePath, const T& object, SerializationFormat format = SerializationFormat::JSON) {
        ISerializer* serializer = GetSerializer(format);
        if (!serializer) {
            return false;
        }
        return serializer->Serialize(filePath, object);
    }

    template<typename T>
    bool Deserialize(const std::string& filePath, T& object, SerializationFormat format = SerializationFormat::JSON) {
        ISerializer* serializer = GetSerializer(format);
        if (!serializer) {
            return false;
        }
        return serializer->Deserialize(filePath, object);
    }

    // 異存系統
    template<typename T>
    bool SaveGame(const std::string& saveSlot, const T& gameState) {
        return Serialize(GetSaveSlotPath(saveSlot), gameState, SerializationFormat::JSON);
    }

    template<typename T>
    bool LoadGame(const std::string& saveSlot, T& gameState) {
        return Deserialize(GetSaveSlotPath(saveSlot), gameState, SerializationFormat::JSON);
    }
    
    // 存檔管理
    std::vector<std::string> GetSaveSlots() const;
    bool DeleteSaveSlot(const std::string& saveSlot);
    bool SaveSlotExists(const std::string& saveSlot) const;
    
    // 自動保存
    void EnableAutoSave(bool enable);
    void SetAutoSaveInterval(float interval);
    void SetMaxAutoSaves(int maxSaves);
    
    // 更新處理
    void Update(float deltaTime);
    
private:
    std::string GetSaveSlotPath(const std::string& saveSlot) const;
    void ProcessAutoSave();
    
private:
    std::unordered_map<SerializationFormat, SharedPtr<ISerializer>> serializers;
    std::string saveDirectory;
    
    bool autoSaveEnabled;
    float autoSaveInterval;
    float autoSaveTimer;
    int maxAutoSaves;
    int currentAutoSaveIndex;
    
    bool initialized;
};

// 全局序列化管理器
extern SerializationManager* gSerializationManager;

/**
 * 初始化全局序列化管理器
 */
bool InitializeSerializationManager();

/**
 * 關閉全局序列化管理器
 */
void ShutdownSerializationManager();

/**
 * 獲取全局序列化管理器
 */
SerializationManager* GetSerializationManager();

// 預定義義可序列化類型
namespace SerializableTypes {

// 變點序列化
struct SceneNodeData : public ISerializable {
    std::string name;
    Vector3 position;
    Quaternion rotation;
    Vector3 scale;
    std::vector<std::string> children;
    
    std::string Serialize() const override;
    bool Deserialize(const std::string& data) override;
    std::string GetSerializationType() const override { return "SceneNodeData"; }
};

// 遊戲對象序列化
struct GameObjectData : public ISerializable {
    std::string name;
    std::string tag;
    int layer;
    bool active;
    std::string sceneNodeData;
    
    std::string Serialize() const override;
    bool Deserialize(const std::string& data) override;
    std::string GetSerializationType() const override { return "GameObjectData"; }
};

// 遊戲狀態序列化
struct GameStateData : public ISerializable {
    std::string levelName;
    float playTime;
    int score;
    std::vector<std::string> activeObjects;
    
    std::string Serialize() const override;
    bool Deserialize(const std::string& data) override;
    std::string GetSerializationType() const override { return "GameStateData"; }
};

} // namespace SerializableTypes
} // namespace Potato

// 便捷宏
#define GET_SERIALIZATION() Potato::GetSerializationManager()