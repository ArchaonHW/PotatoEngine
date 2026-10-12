#include "PipelineRunner.h"

#include "Serialization/JsonParser.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Potato {
namespace Media {

bool PipelineRunner::LoadPlan(const std::string& json) {
    JsonValue root;
    if (!JsonValue::ParseOk(json, root) ||
        root["schema"].AsString() != "potato.pipeline_plan/1") {
        lastError_ = "bad plan: schema mismatch or malformed JSON";
        return false;
    }
    std::vector<PipelineStage> parsed;
    for (const JsonValue& s : root["stages"].AsArray()) {
        PipelineStage st;
        st.name = s["name"].AsString();
        st.exe = s["exe"].AsString();
        for (const JsonValue& a : s["args"].AsArray()) {
            st.args.push_back(a.AsString());
        }
        for (const JsonValue& o : s["outputs"].AsArray()) {
            st.outputs.push_back(o.AsString());
        }
        st.optional = s["optional"].AsBool(false);
        if (st.exe.empty()) {
            lastError_ = "stage '" + st.name + "' has empty exe";
            return false;
        }
        parsed.push_back(st);
    }
    if (parsed.empty()) {
        lastError_ = "plan has no stages";
        return false;
    }
    stages_ = std::move(parsed);
    return true;
}

bool PipelineRunner::LoadPlanFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        lastError_ = "cannot open plan file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return LoadPlan(ss.str());
}

// exe 與 args 以雙引號包裹避免路徑空白拆字
static std::string QuoteArg(const std::string& a) {
    return "\"" + a + "\"";
}

bool PipelineRunner::Run() {
    failedOptional_.clear();
    for (const PipelineStage& st : stages_) {
        std::string cmd = QuoteArg(st.exe);
        for (const std::string& a : st.args) cmd += " " + QuoteArg(a);

        const int rc = std::system(cmd.c_str());
        bool ok = (rc == 0);
        std::string missing;
        if (ok) {
            for (const std::string& o : st.outputs) {
                if (!std::filesystem::exists(o)) {
                    ok = false;
                    missing = o;
                    break;
                }
            }
        }

        if (!ok) {
            const std::string why =
                (rc != 0) ? ("exit " + std::to_string(rc))
                          : ("missing output " + missing);
            if (st.optional) {
                failedOptional_.push_back(st.name + " (" + why + ")");
                continue;
            }
            lastError_ = "stage '" + st.name + "' failed: " + why;
            return false;
        }
    }
    return true;
}

} // namespace Media
} // namespace Potato
