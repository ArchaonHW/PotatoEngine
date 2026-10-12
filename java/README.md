# Potato Engine AI Agent — Java/Spring 呈現層

以 JNI 橋接 `ReinforcementLearning`/`NeuralNetwork` lib（QL/DQN/PG/AC ×
GridWorld），Spring Cloud 微服務拓樸：

- `discovery-server`：Eureka 註冊中心（:8761）
- `api-gateway`：Spring Cloud Gateway（:8080，REST + WebSocket 代理）
- `agent-service`：Spring Boot 服務（:8081，JNI + REST + WebSocket + 前端頁）

## 建置

```powershell
# 1. 原生 DLL（repo 根目錄；含 CJK 路徑請走 junction，如 F:\potato_pe）
cmake -S <repo> -B <repo>\build-ninja -G Ninja
cmake --build <repo>\build-ninja --target PotatoAgentJni
# 產物：<repo>\build-ninja\bin\PotatoAgentJni.dll

# 2. Java（需要 JDK 17+、Maven）
cd java
mvn package
```

## 啟動（依序）

```powershell
java -jar discovery-server/target/discovery-server-1.0.0.jar
java -jar api-gateway/target/api-gateway-1.0.0.jar
java -jar agent-service/target/agent-service-1.0.0.jar
```

`agent-service` 啟動時自動在 `build-ninja/bin` 等目錄搜尋
`PotatoAgentJni.dll`；也可用 `-Dpotato.jni.dll=<路徑>` 或
`POTATO_JNI_DLL` 環變數指定。

瀏覽器開 `http://localhost:8080/`（經 gateway）或
`http://localhost:8081/`（直連）。選演算法 → 建立 Session →
開始訓練即可看到 GridWorld 即時走位與動作估值。

## REST API（/api/agent）

| Method | Path | 說明 |
|---|---|---|
| GET | /status | 原生庫是否載入、session 數 |
| POST | /sessions | 建 session `{algo,gridSize,randMaps,seed,modelPath}` |
| DELETE | /sessions/{id} | 銷毀 |
| GET | /sessions/{id}/grid | 地圖/目標/障礙 |
| POST | /sessions/{id}/reset | 重置（randMaps 重抽地圖） |
| POST | /sessions/{id}/step?explore&learn | 單步推進 |
| POST | /sessions/{id}/end-episode?learn | episode 收尾 |
| POST | /sessions/{id}/model/save|load | 模型存取 `{path}` |

WebSocket：`/ws/agent`，指令 `{"cmd":"run","sessionId":..,"episodes":N,
"maxSteps":M,"learn":bool,"explore":bool,"delayMs":D}` 與 `{"cmd":"stop"}`；
推播 `grid`/`step`/`episodeEnd`/`runEnd`/`error`。
