// SerializationTest - JSON 序列化/反序列化 round-trip 測試

#include "Serialization/Serialization.h"
#include "Serialization/JsonParser.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>

using namespace Potato;
using namespace Potato::SerializableTypes;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    printf("=== Serialization Round-Trip Tests ===\n\n");

    // [1] JsonParser 基礎型別
    {
        JsonValue v;
        Check(JsonValue::ParseOk("{\"a\":1,\"b\":[true,\"x\"],\"c\":null}", v), "parser: 混合物件解析");
        Check(v["a"].AsInt() == 1, "parser: number");
        Check(v["b"][0].AsBool() == true, "parser: array bool");
        Check(v["b"][1].AsString() == "x", "parser: array string");
        Check(v["c"].IsNull(), "parser: null");
        Check(v["missing"].IsNull(), "parser: 缺 key 回傳 Null");
        Check(!JsonValue::ParseOk("{bad json", v), "parser: 壞輸入拒絕");
    }

    // [2] JsonParser escape/unicode
    {
        JsonValue v;
        Check(JsonValue::ParseOk("{\"s\":\"a\\nb\\\"c\\\\d\\u0041\"}", v), "parser: escape 解析");
        Check(v["s"].AsString() == "a\nb\"c\\dA", "parser: escape 內容正確(\\u0041=A)");
    }

    // [3] SceneNodeData round-trip
    {
        SceneNodeData src;
        src.name = "test node\"with\"escapes\n";  // 含 escape 字元
        src.position = Vector3(1.5f, -2.25f, 3.75f);
        src.rotation = Quaternion(0.1f, 0.2f, 0.3f, 0.9f);
        src.scale = Vector3(2.0f, 2.0f, 2.0f);
        src.children = {"child_a", "child_b"};

        std::string json = src.Serialize();
        SceneNodeData dst;
        Check(dst.Deserialize(json), "SceneNodeData::Deserialize 回傳 true");
        Check(dst.name == src.name, "name round-trip(含 escape)");
        Check(Near(dst.position.x, 1.5f) && Near(dst.position.z, 3.75f), "position round-trip");
        Check(Near(dst.rotation.w, 0.9f), "rotation round-trip");
        Check(dst.children.size() == 2 && dst.children[1] == "child_b", "children round-trip");
    }

    // [4] GameObjectData round-trip
    {
        GameObjectData src;
        src.name = "unit_01";
        src.tag = "Player";
        src.layer = 3;
        src.active = false;
        src.sceneNodeData = "{}";

        GameObjectData dst;
        Check(dst.Deserialize(src.Serialize()), "GameObjectData::Deserialize");
        Check(dst.name == "unit_01" && dst.tag == "Player", "GameObject name/tag");
        Check(dst.layer == 3 && dst.active == false, "GameObject layer/active");
    }

    // [5] GameStateData round-trip
    {
        GameStateData src;
        src.levelName = "level_3";
        src.playTime = 123.5f;
        src.score = 9999;
        src.activeObjects = {"a", "b", "c"};

        GameStateData dst;
        Check(dst.Deserialize(src.Serialize()), "GameStateData::Deserialize");
        Check(dst.levelName == "level_3" && dst.score == 9999, "GameState level/score");
        Check(Near(dst.playTime, 123.5f) && dst.activeObjects.size() == 3, "GameState time/objects");
    }

    // [6] 檔案層級:SaveGame/LoadGame + SaveSlotExists/Delete
    {
        SerializationManager mgr;
        mgr.Initialize();

        GameStateData state;
        state.levelName = "persisted";
        state.score = 42;

        Check(mgr.SaveGame("slot_roundtrip", state), "SaveGame 成功");
        Check(mgr.SaveSlotExists("slot_roundtrip"), "SaveSlotExists 找到存檔");

        GameStateData loaded;
        Check(mgr.LoadGame("slot_roundtrip", loaded), "LoadGame 成功");
        Check(loaded.levelName == "persisted" && loaded.score == 42, "存檔內容正確");

        auto slots = mgr.GetSaveSlots();
        bool found = false;
        for (auto& s : slots) if (s == "slot_roundtrip") found = true;
        Check(found, "GetSaveSlots 列出存檔");

        Check(mgr.DeleteSaveSlot("slot_roundtrip"), "DeleteSaveSlot 刪除");
        Check(!mgr.SaveSlotExists("slot_roundtrip"), "刪除後不存在");

        mgr.Shutdown();
    }

    // [7] 含標點的外部檔案不得被列為 slot（列出也對不回路徑）
    {
        SerializationManager mgr;
        mgr.Initialize();

        // 模擬外部/遺留檔案：stem 含 '.'，sanitize 後對不上
        {
            std::ofstream f("Saves/weird.name.json");
            f << "{}";
        }
        Check(mgr.SaveGame("clean_slot", GameStateData()), "建立合規存檔");

        auto slots = mgr.GetSaveSlots();
        bool allResolvable = true;
        bool weirdListed = false;
        for (auto& s : slots) {
            if (!mgr.SaveSlotExists(s)) allResolvable = false;
            if (s == "weird.name" || s == "weirdname") weirdListed = true;
        }
        Check(!weirdListed, "標點檔名不被列為 slot");
        Check(allResolvable, "列出的 slot 全部可解析（round-trip）");

        mgr.DeleteSaveSlot("clean_slot");
        std::filesystem::remove("Saves/weird.name.json");
        mgr.Shutdown();
    }

    printf("\n=== 結果: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
