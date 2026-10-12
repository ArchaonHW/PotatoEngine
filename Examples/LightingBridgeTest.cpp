// LightingBridgeTest - LightingManager → deferred 光源接軌測試（全 headless）
// 覆蓋：castShadow 旗標、AttenuationToRadius 三段式衰減換算、
//   GatherDeferredLights 類型映射/disabled 略過/Spot 錐參數、
//   GetDeferredAmbient 亮度折合
// LightingManager 是 singleton——測試結束還原狀態避免污染其他測試

#include "Rendering/Lighting.h"
#include "Rendering/DeferredShading.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Potato;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name) {
    if (ok) { g_pass++; printf("  [PASS] %s\n", name); }
    else    { g_fail++; printf("  [FAIL] %s\n", name); }
}

int main() {
    printf("=== Lighting Bridge Tests ===\n\n");
    auto& lm = LightingManager::GetInstance();
    lm.DestroyAllLights();

    // [1] AttenuationToRadius 換算
    {
        // 純二次：1/(1+d²) = 1/128 → d = sqrt(127) ≈ 11.27
        const float r = LightingManager::AttenuationToRadius(1.f, 0.f, 1.f);
        Check(std::fabs(r - std::sqrt(127.f)) < 0.01f,
              "純二次衰減半徑");

        // 純線性：1/(1+d) = 1/128 → d = 127
        const float rl = LightingManager::AttenuationToRadius(1.f, 1.f, 0.f);
        Check(std::fabs(rl - 127.f) < 0.01f, "純線性衰減半徑");

        // 初始亮度已低於 cutoff：c=200 > K=128 → 半徑 0
        Check(LightingManager::AttenuationToRadius(200.f, 0.f, 1.f) == 0.f,
              "初始亮度低於 cutoff → 半徑 0");

        // 無衰減 → 大半徑（平滑窗口內近乎等強度）
        Check(LightingManager::AttenuationToRadius(0.f, 0.f, 0.f) > 1000.f,
              "無衰減 → 大半徑");

        // 典型三段（c=1, l=0.09, q=0.032）半徑應在合理範圍
        const float rt = LightingManager::AttenuationToRadius(
            1.f, 0.09f, 0.032f);
        Check(rt > 40.f && rt < 80.f, "典型三段衰減半徑範圍");
    }

    // [2] castShadow 旗標
    {
        DirectionalLight dl;
        Check(!dl.IsCastShadow(), "castShadow 預設關");
        dl.SetCastShadow(true);
        Check(dl.IsCastShadow(), "SetCastShadow 生效");
    }

    // [3] GatherDeferredLights 映射
    {
        auto* sun = lm.CreateDirectionalLight("sun");
        sun->SetDirection(Vector3(0.f, -2.f, 0.f)); // 內部正規化
        sun->SetColor(Vector3(1.f, 0.9f, 0.8f));
        sun->SetIntensity(2.0f);
        sun->SetCastShadow(true);

        auto* lamp = lm.CreatePointLight("lamp");
        lamp->SetPosition(Vector3(3.f, 4.f, 5.f));
        lamp->SetAttenuation(1.f, 0.f, 1.f); // → r = sqrt(127)
        lamp->SetIntensity(0.5f);
        lamp->SetCastShadow(true);

        auto* spot = lm.CreateSpotLight("spot");
        spot->SetPosition(Vector3(1.f, 1.f, 1.f));
        spot->SetDirection(Vector3(0.f, -1.f, 0.f));
        spot->SetCutoff(30.f);
        spot->SetOuterCutoff(20.f); // inner<outer 時自動對調
        spot->SetAttenuation(1.f, 1.f, 0.f); // → r = 127
        spot->SetCastShadow(true);

        auto* off = lm.CreatePointLight("off");
        off->SetEnabled(false);

        std::vector<DeferredDirLight> dirs;
        std::vector<DeferredPointLight> pts;
        std::vector<DeferredSpotLight> sps;
        lm.GatherDeferredLights(dirs, pts, sps);

        Check(dirs.size() == 1, "方向光 1 支");
        Check(pts.size() == 1 && sps.size() == 1,
              "點光 1 支 + Spot 1 支（disabled 略過）");

        const auto& d = dirs[0];
        Check(std::fabs(d.direction.y + 1.f) < 1e-4f &&
                  d.direction.LengthSquared() > 0.99f,
              "方向已正規化且向下");
        Check(d.castShadow && d.intensity == 2.0f,
              "castShadow 與 intensity 透傳");
        Check(std::fabs(d.color.y - 0.9f) < 1e-6f, "顏色透傳");

        const DeferredPointLight& pl = pts[0];
        Check(std::fabs(pl.position.x - 3.f) < 1e-6f &&
                  std::fabs(pl.radius - std::sqrt(127.f)) < 0.01f,
              "點光位置與衰減→半徑換算正確");
        Check(pl.intensity == 0.5f, "點光 intensity 透傳");
        Check(pl.castShadow, "點光 castShadow 透傳");

        const DeferredSpotLight& sp = sps[0];
        Check(std::fabs(sp.position.x - 1.f) < 1e-6f &&
                  std::fabs(sp.radius - 127.f) < 0.01f,
              "Spot 位置與衰減→半徑換算正確");
        Check(std::fabs(sp.direction.y + 1.f) < 1e-4f,
              "Spot 方向透傳");
        // cutoff=30/outer=20 → 對調為 inner=20° outer=30°
        Check(std::fabs(sp.innerCos - std::cos(20.f * 3.14159265f / 180.f))
                  < 1e-5f &&
              std::fabs(sp.outerCos - std::cos(30.f * 3.14159265f / 180.f))
                  < 1e-5f,
              "Spot 錐角→cos 且 inner<outer 對調");
        Check(sp.castShadow, "Spot castShadow 透傳");
    }

    // [4] GetDeferredAmbient 亮度折合
    {
        lm.SetAmbientColor(Vector3(1.f, 1.f, 1.f));
        lm.SetAmbientIntensity(0.2f);
        Check(std::fabs(lm.GetDeferredAmbient() - 0.2f) < 1e-6f,
              "白光 ambient 強度折合");
        lm.SetAmbientColor(Vector3(0.f, 1.f, 0.f)); // 純綠 → 0.7152*0.2
        Check(std::fabs(lm.GetDeferredAmbient() - 0.7152f * 0.2f) < 1e-4f,
              "彩光 ambient 走 Rec.709 亮度");
    }

    // 還原 singleton 狀態
    lm.DestroyAllLights();
    lm.SetAmbientColor(Vector3(0.1f, 0.1f, 0.1f));
    lm.SetAmbientIntensity(1.0f);

    printf("\n=== 結果 ===\n  PASS: %d\n  FAIL: %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
