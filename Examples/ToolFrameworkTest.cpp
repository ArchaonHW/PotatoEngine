// ToolFrameworkTest - AI 工具框架測試（全 headless）
//
// 覆蓋：ToolRegistry 註冊/查找、必填參數驗證、真超時
//   （worker thread + wait_for）、指數退避重試、
//   ToolChain 步驟間 {{prev.*}} 模板替換與真條件分流。

#include "ToolFramework.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>

using namespace Potato::AI;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

namespace {

using Params = std::unordered_map<std::string, std::string>;

ToolResult Ok(const std::string& out) {
    ToolResult r;
    r.success = true;
    r.output = out;
    return r;
}

ToolResult Fail(const std::string& err) {
    ToolResult r;
    r.success = false;
    r.error = err;
    return r;
}

} // namespace

int main() {
    printf("=== ToolFramework 測試 ===\n\n");

    ToolRegistry reg;
    ToolExecutor exec(&reg);

    // [1] Registry：註冊/執行/查找
    {
        Tool echo;
        echo.name = "echo";
        echo.description = "echoes the 'text' parameter";
        echo.parameters = {{"text", "text to echo"}};
        echo.requiredParameters = {"text"};
        echo.function = [](const Params& p) {
            return Ok("echo:" + p.at("text"));
        };
        reg.RegisterTool(echo);

        Check(reg.GetToolCount() == 1, "註冊工具數");
        Check(reg.GetTool("echo") != nullptr, "GetTool 命中");
        Check(reg.GetTool("nope") == nullptr,
              "GetTool 未命中回傳 null");

        ToolResult r = exec.Execute("echo", {{"text", "hi"}});
        Check(r.success && r.output == "echo:hi",
              "基本執行+參數傳遞");

        // 必填參數缺漏 → 驗證拒絕
        r = exec.Execute("echo", {});
        Check(!r.success &&
                  r.error.find("required") != std::string::npos,
              "必填參數缺漏被拒絕");

        // 關閉安全檢查 → 直接進函式（函式內 at() 會拋,
        // async 包裝捕獲成 error）
        exec.EnableSafetyChecks(false);
        r = exec.Execute("echo", {});
        Check(!r.success, "例外被捕獲為失敗而非當機");
        exec.EnableSafetyChecks(true);

        // 關鍵字查找
        Check(!reg.FindToolsByKeyword("echo").empty(),
              "名稱關鍵字查找");
    }

    // [2] 真超時：長跑工具被 wait_for 截斷
    {
        Tool slow;
        slow.name = "slow";
        slow.description = "sleeps 500ms";
        slow.function = [](const Params&) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));
            return Ok("done");
        };
        reg.RegisterTool(slow);

        const auto t0 = std::chrono::steady_clock::now();
        ToolResult r =
            exec.ExecuteWithTimeout("slow", {}, 50);
        const auto dt = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        Check(!r.success &&
                  r.error.find("timeout") != std::string::npos,
              "逾時回傳失敗");
        Check(dt < 400, "逾時實際截斷（<400ms 而非 500ms）");
        Check(exec.GetStats().timeoutExecutions == 1,
              "timeoutExecutions 計數");

        // 足夠時限 → 正常完成
        r = exec.ExecuteWithTimeout("slow", {}, 2000);
        Check(r.success && r.output == "done",
              "足夠時限正常完成");
    }

    // [3] 重試：前 N 次失敗後成功
    {
        std::atomic<int> calls{0};
        Tool flaky;
        flaky.name = "flaky";
        flaky.description = "fails first 2 calls";
        flaky.function = [&calls](const Params&) {
            const int n = ++calls;
            return n < 3 ? Fail("flaky fail")
                         : Ok("flaky ok");
        };
        reg.RegisterTool(flaky);

        ToolResult r =
            exec.ExecuteWithRetry("flaky", {}, 5, 1);
        Check(r.success && r.output == "flaky ok",
              "重試後成功");
        Check(calls.load() == 3, "實際呼叫 3 次");
        Check(exec.GetStats().retriedExecutions == 2,
              "retriedExecutions 計數");

        // 上限用盡仍失敗
        calls = 0;
        flaky.function = [&calls](const Params&) {
            ++calls;
            return Fail("always");
        };
        Tool* t = reg.GetTool("flaky");
        t->function = flaky.function;
        r = exec.ExecuteWithRetry("flaky", {}, 2, 1);
        Check(!r.success && calls.load() == 3,
              "重試上限用盡（1+2 次呼叫）");

        // 永久失敗型錯誤不重試：不存在的工具
        const auto callsBefore =
            exec.GetStats().totalExecutions;
        r = exec.ExecuteWithRetry("no_such_tool", {}, 5, 1);
        Check(!r.success &&
                  exec.GetStats().totalExecutions ==
                      callsBefore + 1,
              "not-found 錯誤不重試");
    }

    // [4] ToolChain：{{prev.*}} 模板 + 真條件分流
    {
        // upper：把 text 轉大寫
        Tool upper;
        upper.name = "upper";
        upper.description = "uppercase";
        upper.function = [](const Params& p) {
            std::string s = p.count("text") ? p.at("text") : "";
            for (auto& c : s) c = ::toupper(c);
            return Ok(s);
        };
        reg.RegisterTool(upper);

        // emit：輸出固定字串
        Tool emit;
        emit.name = "emit";
        emit.description = "emits 'hello'";
        emit.function = [](const Params&) {
            return Ok("hello");
        };
        reg.RegisterTool(emit);

        ToolChain chain("t");
        chain.AddTool("emit", {});
        chain.AddTool("upper", {{"text", "{{prev.output}}"}});
        ToolResult r = chain.Execute(&exec);
        Check(r.success && r.output == "HELLO",
              "鏈步驟間 {{prev.output}} 傳遞");

        // 條件分流：prev.success → success 分支工具
        Tool onOk;
        onOk.name = "on_ok";
        onOk.description = "success branch";
        onOk.function = [](const Params&) {
            return Ok("took-ok");
        };
        Tool onFail;
        onFail.name = "on_fail";
        onFail.description = "fail branch";
        onFail.function = [](const Params&) {
            return Ok("took-fail");
        };
        reg.RegisterTool(onOk);
        reg.RegisterTool(onFail);

        ToolChain c2("c2");
        c2.AddTool("emit", {});
        c2.AddConditional("prev.success", "on_ok", "on_fail");
        r = c2.Execute(&exec);
        Check(r.output == "took-ok",
              "條件 prev.success 走 true 分支");

        Tool boom;
        boom.name = "boom";
        boom.description = "always fails";
        boom.function = [](const Params&) {
            return Fail("kaput");
        };
        reg.RegisterTool(boom);

        ToolChain c3("c3");
        c3.SetStopOnError(false);
        c3.AddTool("boom", {});
        c3.AddConditional("prev.fail", "on_fail", "on_ok");
        r = c3.Execute(&exec);
        Check(r.output == "took-fail",
              "條件 prev.fail 走分支");

        // output.contains 條件
        ToolChain c4("c4");
        c4.AddTool("emit", {});
        c4.AddConditional("prev.output.contains:ell",
                          "on_ok", "on_fail");
        r = c4.Execute(&exec);
        Check(r.output == "took-ok",
              "output.contains 條件命中");

        // 統計：total > successful（有故意失敗）
        const auto st = exec.GetStats();
        Check(st.totalExecutions > st.successfulExecutions &&
                  st.failedExecutions > 0,
              "執行統計累積");
    }

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n",
           g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
