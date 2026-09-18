// MediaPipeTest - 影音生成管線 CPU 段無頭測試
// 覆蓋: DatasetManifest round-trip/拒絕、PipelineRunner plan 解析與
//       段編排（必要段失敗中止/optional 段續跑/工件驗證）、
//       VideoEncoder::BuildCommand 參數（不需真跑 ffmpeg）

#include "Media/DatasetManifest.h"
#include "Media/PipelineRunner.h"
#include "Media/VideoEncoder.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Potato::Media;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) {
        g_pass++;
        printf("  [PASS] %s\n", name);
    } else {
        g_fail++;
        printf("  [FAIL] %s\n", name);
    }
}

int main() {
    printf("=== Media Pipeline Tests ===\n\n");

    // ---- [1] DatasetManifest ----
    printf("[1] manifest\n");
    {
        DatasetManifest m;
        m.kind = "video";
        m.seed = 42;
        m.episodes = {"ep_0000", "ep_0001"};
        Check(m.SchemaTag() == "potato.video_dataset/1", "schema tag");
        Check(m.IsValid(), "kind 非空即 valid");

        const std::string json = m.ToJson();
        Check(json.find("potato.video_dataset/1") != std::string::npos,
              "JSON 含 schema");
        Check(json.find("\"seed\":42") != std::string::npos, "JSON 含 seed");

        DatasetManifest m2;
        Check(m2.FromJson(json), "round-trip 回讀");
        Check(m2.kind == "video" && m2.seed == 42 &&
                  m2.episodes.size() == 2,
              "回讀內容一致");

        Check(!m2.FromJson("{bad"), "壞 JSON 拒絕");
        Check(!m2.FromJson("{\"schema\":\"potato.other/1\"}"),
              "非 dataset schema 拒絕");
        Check(m2.kind == "video", "拒絕後原值不被覆寫");

        // 檔案 IO
        const std::string dir = "output/media_test";
        std::filesystem::create_directories(dir);
        Check(m.SaveToFile(dir + "/dataset.json"), "manifest 落盤");
        DatasetManifest m3;
        Check(m3.LoadFromFile(dir + "/dataset.json"), "manifest 讀檔");
        Check(m3.kind == "video", "讀檔內容一致");
        Check(!m3.LoadFromFile(dir + "/nonexistent.json"),
              "缺檔拒絕");
    }

    // extras 保留
    {
        DatasetManifest m;
        Check(m.FromJson(
                "{\"schema\":\"potato.x_dataset/1\",\"seed\":1,"
                "\"extras\":{\"a\":[1,2],\"b\":{\"c\":\"d\"}}}"),
              "extras 解析");
        Check(m.extrasJson.find("\"a\"") != std::string::npos,
              "extras 保留物件");
        const std::string re = m.ToJson();
        Check(re.find("\"extras\":") != std::string::npos,
              "extras round-trip");
    }

    // ---- [2] VideoEncoder::BuildCommand ----
    printf("\n[2] encoder command\n");
    {
        const std::string cmd = VideoEncoder::BuildCommand(
            "out.mp4", 640, 360, 12, PixelFormat::RGBA, "log.txt");
        Check(cmd.find("rawvideo") != std::string::npos, "rawvideo 輸入");
        Check(cmd.find("rgba") != std::string::npos, "RGBA pix_fmt");
        Check(cmd.find("640x360") != std::string::npos, "解析度");
        Check(cmd.find("libx264") != std::string::npos, "x264 編碼");
        Check(cmd.find("vflip") != std::string::npos,
              "vflip（bottom-up 修正）");
        Check(cmd.find("log.txt") != std::string::npos, "log 導向");

        const std::string cmd2 = VideoEncoder::BuildCommand(
            "o.mp4", 1, 1, 1, PixelFormat::RGB, "");
        Check(cmd2.find("rgb24") != std::string::npos, "RGB pix_fmt");
        Check(cmd2.find("2>") == std::string::npos, "無 log 不加導向");
    }

    // ---- [3] PipelineRunner plan 解析 ----
    printf("\n[3] plan parse\n");
    {
        PipelineRunner r;
        Check(r.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":["
                  "{\"name\":\"s1\",\"exe\":\"tool_a\",\"args\":[\"x\"],"
                  "\"outputs\":[\"o1\"],\"optional\":true}]}"),
              "合法 plan");
        Check(r.StageCount() == 1, "段數 1");
        Check(r.Stages()[0].optional, "optional 旗標");
        Check(r.Stages()[0].args.size() == 1, "args 解析");

        Check(!r.LoadPlan("{bad"), "壞 JSON 拒絕");
        Check(!r.LoadPlan("{\"schema\":\"potato.other/1\"}"),
              "錯 schema 拒絕");
        Check(!r.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":[]}"),
              "空 stages 拒絕");
        Check(!r.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":["
                  "{\"name\":\"x\",\"exe\":\"\"}]}"),
              "空 exe 拒絕");
    }

    // ---- [4] Runner 段編排 ----
    printf("\n[4] runner\n");
    {
        // 必要段成功：用一個會產出工件的指令（cmd echo）
        const std::string marker = "output/media_test/marker.txt";
        std::filesystem::create_directories("output/media_test");
        std::filesystem::remove(marker);

        PipelineRunner r;
        // Windows: echo 文字 > 檔案（system() 走 cmd）
        Check(r.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":["
                  "{\"name\":\"gen\",\"exe\":\"cmd\",\"args\":[\"/c\","
                  "\"echo hi > " + marker + "\"],"
                  "\"outputs\":[\"" + marker + "\"]}]}"),
              "單段 plan");
        Check(r.Run(), "必要段成功 + 工件存在");
        Check(std::filesystem::exists(marker), "工件真的產出");
        Check(r.FailedOptional().empty(), "無 optional 失敗");

        // 必要段工件缺席 → 中止
        PipelineRunner r2;
        Check(r2.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":["
                  "{\"name\":\"noop\",\"exe\":\"cmd\",\"args\":[\"/c\","
                  "\"echo hi\"],\"outputs\":[\"never_exists_xyz\"]},"
                  "{\"name\":\"after\",\"exe\":\"cmd\",\"args\":[\"/c\","
                  "\"echo x\"]}]}"),
              "缺席工件 plan");
        Check(!r2.Run(), "工件缺席 → 失敗");
        Check(r2.LastError().find("noop") != std::string::npos,
              "錯誤指明失敗段");

        // optional 段失敗 → 繼續跑完
        PipelineRunner r3;
        Check(r3.LoadPlan(
                  "{\"schema\":\"potato.pipeline_plan/1\",\"stages\":["
                  "{\"name\":\"opt\",\"exe\":\"cmd\",\"args\":[\"/c\","
                  "\"exit 1\"],\"optional\":true},"
                  "{\"name\":\"real\",\"exe\":\"cmd\",\"args\":[\"/c\","
                  "\"echo ok\"]}]}"),
              "optional plan");
        Check(r3.Run(), "optional 失敗不中止");
        Check(r3.FailedOptional().size() == 1, "記到 optional 失敗");
    }

    printf("\n=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
