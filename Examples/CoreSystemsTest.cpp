// CoreSystemsTest - 核心系統無頭煙霧測試：ECS / EventBus / ResourceCache
// 純 headless：不需 GL context、不需視窗、不需資產檔案
// 注意：不測 ResourceCache::Load<T>（標頭只宣告未定義，連結會失敗）；
//       不測 ProcessEventQueue 派發（EventBus 無 QueueEvent API，佇列為死碼）

#include "ECS/ECSCoordinator.h"
#include "Events/EventBus.h"
#include "Resources/ResourceManager.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Potato;
using namespace Potato::ECS;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

// ---- 測試用組件（CRTP） ----
struct TestPosition : public Component<TestPosition> {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct TestVelocity : public Component<TestVelocity> {
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
};

// ---- 測試用系統：記錄 Update 呼叫順序 ----
class RecordingSystem : public ISystem {
public:
    RecordingSystem(std::vector<int>* orderLog, int id, int priority)
        : log(orderLog), sysID(id), sysPriority(priority) {}

    void Update(float deltaTime) override {
        log->push_back(sysID);
        lastDeltaTime = deltaTime;
        updateCount++;
    }
    int GetPriority() const override { return sysPriority; }
    const char* GetName() const override { return "RecordingSystem"; }

    int updateCount = 0;
    float lastDeltaTime = 0.0f;

private:
    std::vector<int>* log;
    int sysID;
    int sysPriority;
};

// ---- 測試用事件（CRTP） ----
struct TestEventA : public Event<TestEventA> { int value = 0; };
struct TestEventB : public Event<TestEventB> { int value = 0; };

// ---- 測試用監聽器 ----
class CountingListener : public IEventListener {
public:
    void OnEvent(const IEvent& event) override {
        count++;
        if (event.GetType() == TestEventA::GetTypeStatic()) {
            lastValue = static_cast<const TestEventA&>(event).value;
        }
    }
    int count = 0;
    int lastValue = -1;
};

// ---- 測試用資源載入器：回傳 heap int 哨兵指標，計數 Load/Unload 呼叫 ----
class FakeIntLoader : public IResourceLoader {
public:
    bool Load(const std::string& /*path*/, void** resource) override {
        loadCalls++;
        *resource = new int(kSentinel);
        return true;
    }
    void Unload(void* resource) override {
        unloadCalls++;
        delete static_cast<int*>(resource);
    }
    ResourceType GetType() const override { return ResourceType::Texture; }

    static constexpr int kSentinel = 0x5AFE;
    int loadCalls = 0;
    int unloadCalls = 0;
};

int main() {
    printf("=== Core Systems Tests (ECS / EventBus / ResourceCache) ===\n\n");

    // [1] ECS 實體生命週期：ID 從 1 開始、銷毀後 LIFO 回收
    printf("[1] ECS 實體生命週期\n");
    {
        ECSCoordinator ecs; // 直接用 stack 實例，不碰 gECSCoordinator 全域

        Entity e1 = ecs.CreateEntity();
        Entity e2 = ecs.CreateEntity();
        Check(e1.GetID() == 1, "首個實體 ID 從 1 開始");
        Check(e2.GetID() == 2, "實體 ID 依序遞增");
        Check(ecs.IsEntityValid(e1) && ecs.IsEntityValid(e2), "新建實體為有效");
        Check(ecs.GetEntityCount() == 2, "GetEntityCount 反映存活數");

        ecs.DestroyEntity(e1);
        ecs.DestroyEntity(e2);
        Check(ecs.GetEntityCount() == 0, "銷毀後存活數歸零");
        Check(!ecs.IsEntityValid(e1) && !ecs.IsEntityValid(e2), "銷毀後實體失效");

        // LIFO：最後銷毀的 e2(ID=2) 先被回收
        Entity e3 = ecs.CreateEntity();
        Check(e3.GetID() == 2, "ID 回收採 LIFO（後銷毀先重用）");
        Entity e4 = ecs.CreateEntity();
        Check(e4.GetID() == 1, "LIFO 回收第二順位");
        Entity e5 = ecs.CreateEntity();
        Check(e5.GetID() == 3, "回收用盡後繼續遞增");
        Check(ecs.GetEntityCount() == 3, "回收後存活數正確");

        // [2] ECS 組件：新增/查詢/寫入/移除/缺組件拋例外
        printf("[2] ECS 組件操作\n");
        TestPosition pos;
        pos.x = 7.0f; pos.y = 8.0f; pos.z = 9.0f;
        ecs.AddComponent(e3, pos);
        Check(ecs.HasComponent<TestPosition>(e3), "AddComponent 後 HasComponent 為真");
        Check(!ecs.HasComponent<TestVelocity>(e3), "未加的組件 HasComponent 為假");

        TestPosition& ref = ecs.GetComponent<TestPosition>(e3);
        Check(ref.x == 7.0f && ref.y == 8.0f && ref.z == 9.0f, "GetComponent 讀回寫入值");
        ref.x = 42.0f;
        Check(ecs.GetComponent<TestPosition>(e3).x == 42.0f, "GetComponent 回傳參照可寫入");

        ecs.RemoveComponent<TestPosition>(e3);
        Check(!ecs.HasComponent<TestPosition>(e3), "RemoveComponent 後 HasComponent 為假");

        bool threw = false;
        try {
            ecs.GetComponent<TestVelocity>(e3);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        Check(threw, "GetComponent 缺組件拋 std::out_of_range");

        // DestroyEntity 連帶移除該實體所有組件
        ecs.AddComponent(e3, pos);
        ecs.DestroyEntity(e3);
        Check(!ecs.HasComponent<TestPosition>(e3), "DestroyEntity 連帶移除組件");

        // [3] ECS 系統：Initialize 前 Update 為 no-op；之後依優先級升冪執行
        printf("[3] ECS 系統優先級\n");
        std::vector<int> order;
        auto sysHigh = MakeShared<RecordingSystem>(&order, 100, 100);
        auto sysLow  = MakeShared<RecordingSystem>(&order, 1, 1);
        ecs.AddSystem(sysHigh); // 故意先加高優先級
        ecs.AddSystem(sysLow);
        Check(ecs.GetSystemCount() == 2, "AddSystem 後 GetSystemCount 正確");

        ecs.Update(0.016f);
        Check(order.empty(), "Initialize 前 Update 為 no-op");

        ecs.Initialize();
        ecs.Update(0.5f);
        Check(order.size() == 2, "Update 執行所有註冊系統");
        Check(order.size() == 2 && order[0] == 1 && order[1] == 100,
              "系統依優先級升冪執行");
        Check(sysLow->lastDeltaTime == 0.5f && sysHigh->lastDeltaTime == 0.5f,
              "Update 正確傳遞 deltaTime");

        ecs.Shutdown();
    }

    // [4] EventBus 訂閱與同步派發
    printf("[4] EventBus 訂閱與派發\n");
    {
        EventBus bus;
        int callCount = 0;
        int lastValue = -1;
        bus.Subscribe<TestEventA>([&](const TestEventA& e) {
            callCount++;
            lastValue = e.value;
        });
        Check(bus.GetHandlerCount() == 1, "Subscribe 後 GetHandlerCount 反映訂閱數");

        TestEventA evA;
        evA.value = 77;
        bus.Publish(evA);
        Check(callCount == 1 && lastValue == 77, "Publish 同步派發且攜帶正確負載");

        bus.Publish(TestEventB{});
        Check(callCount == 1, "不同類型事件不觸發 handler");

        bus.Subscribe<TestEventA>([](const TestEventA&) {});
        Check(bus.GetHandlerCount() == 2, "第二個訂閱後 GetHandlerCount 遞增");
    }

    // [5] EventBus 再入：handler 內巢狀 Publish 不死結且送達
    printf("[5] EventBus 巢狀 Publish\n");
    {
        EventBus bus;
        int outerCount = 0;
        bool nestedDelivered = false;
        int nestedValue = -1;
        bus.Subscribe<TestEventA>([&](const TestEventA&) {
            outerCount++;
            TestEventB inner;
            inner.value = 5;
            bus.Publish(inner); // handler 內再 Publish：snapshot 派發不得死結
        });
        bus.Subscribe<TestEventB>([&](const TestEventB& e) {
            nestedDelivered = true;
            nestedValue = e.value;
        });

        bus.Publish(TestEventA{});
        Check(outerCount == 1, "外層 handler 執行一次");
        Check(nestedDelivered && nestedValue == 5, "巢狀事件送達且負載正確");
    }

    // [6] EventBus 監聽器訂閱與退訂
    printf("[6] EventBus 監聽器退訂\n");
    {
        EventBus bus;
        CountingListener listener;
        bus.SubscribeListener(TestEventA::GetTypeStatic(), &listener);
        Check(bus.GetListenerCount() == 1, "SubscribeListener 後 GetListenerCount 反映數量");

        TestEventA evL;
        evL.value = 9;
        bus.Publish(evL);
        Check(listener.count == 1 && listener.lastValue == 9, "監聽器收到事件與負載");

        bus.UnsubscribeListener(TestEventA::GetTypeStatic(), &listener);
        Check(bus.GetListenerCount() == 0, "UnsubscribeListener 後數量歸零");
        bus.Publish(evL);
        Check(listener.count == 1, "退訂後不再收到事件");
    }

    // [7] ResourceCache：假 loader 驗證引用計數 / 快取 / 卸載
    printf("[7] ResourceCache 引用計數與卸載\n");
    {
        ResourceCache cache;
        auto loader = MakeShared<FakeIntLoader>();
        cache.RegisterLoader(ResourceType::Texture, loader);
        const std::string path = "fake/texture.sentinel";

        void* pNull = cache.LoadRaw("no/loader.bin", ResourceType::Mesh);
        Check(pNull == nullptr, "無 loader 的類型回傳 nullptr");

        void* p1 = cache.LoadRaw(path, ResourceType::Texture);
        Check(p1 != nullptr, "LoadRaw 回傳非空指標");
        Check(loader->loadCalls == 1, "首次載入呼叫 loader->Load 一次");
        Check(p1 && *static_cast<int*>(p1) == FakeIntLoader::kSentinel,
              "回傳 loader 產生的哨兵資源");
        Check(cache.IsLoaded(path), "載入後 IsLoaded 為真");
        Check(cache.GetLoadedResourceCount() == 1, "GetLoadedResourceCount 為 1");
        Check(cache.GetMetadata(path).referenceCount == 1, "初始引用計數為 1");

        void* p2 = cache.LoadRaw(path, ResourceType::Texture);
        Check(p2 == p1, "重複 LoadRaw 回傳快取指標");
        Check(loader->loadCalls == 1, "重複載入不再呼叫 loader->Load");
        Check(cache.GetMetadata(path).referenceCount == 2, "重複載入遞增引用計數");

        cache.Unload(path);
        Check(cache.IsLoaded(path), "引用計數未歸零前不卸載");
        Check(loader->unloadCalls == 0, "未歸零前不呼叫 loader->Unload");

        cache.Unload(path);
        Check(!cache.IsLoaded(path), "引用計數歸零後 IsLoaded 為假");
        Check(loader->unloadCalls == 1, "歸零時呼叫 loader->Unload 釋放");
        Check(cache.GetLoadedResourceCount() == 0, "卸載後資源數歸零");

        // UnloadAll：不論引用計數一律釋放
        cache.LoadRaw(path, ResourceType::Texture);
        cache.UnloadAll();
        Check(!cache.IsLoaded(path) && loader->unloadCalls == 2,
              "UnloadAll 釋放所有資源");
    }

    // [8] ResourceManager：僅測初始化與路徑解析（無 loader 時 Load 系列回傳 nullptr）
    printf("[8] ResourceManager 初始化與路徑解析\n");
    {
        ResourceManager rm;
        Check(rm.Initialize(), "ResourceManager Initialize 成功");
        Check(rm.ResolveResourcePath("C:/abs/dir/tex.png") == "C:/abs/dir/tex.png",
              "絕對路徑原樣回傳（不觸碰檔案系統）");
        Check(rm.LoadTexture("nonexistent.png") == nullptr,
              "無 loader 時 LoadTexture 回傳 nullptr");
        rm.Shutdown();
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
