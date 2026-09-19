// LightLimitFix -- settings, ImGui panel and the shared per-frame buffer.
//
// Split out of src/Features/LightLimitFix.cpp. This cluster owns no PreNG state
// and no function-local statics, which is why it goes first: if the split
// mechanism itself is wrong, it shows up here with nothing else confounding it.

#include "Features/LightLimitFix.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "SimpleIni.h"

#include <imgui.h>

#if defined(FALLOUT_POST_AE)
// DataLoaded() reaches GameSettingCollection. LightLimitFix.cpp got this
// transitively through RE/T/TESObjectLIGH.h; name it directly here.
#include "RE/S/Setting.h"
#endif

void LightLimitFix::LoadSettings()
{
    constexpr auto kSection = "Settings";
    constexpr auto kVizEnabled = "bEnableLightsVisualisation";
    constexpr auto kVizMode = "uLightsVisualisationMode";

    CSimpleIniA ini;
    ini.SetUnicode();

    const auto path = GetSettingsPath();
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
    {
        ini.LoadFile(path.string().c_str());
    }

    settings.EnableLightsVisualisation = ini.GetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
    settings.LightsVisualisationMode = static_cast<std::uint32_t>(
        ini.GetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode)));
}

void LightLimitFix::SaveSettings()
{
    constexpr auto kSection = "Settings";
    constexpr auto kVizEnabled = "bEnableLightsVisualisation";
    constexpr auto kVizMode = "uLightsVisualisationMode";

    CSimpleIniA ini;
    ini.SetUnicode();

    ini.SetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
    ini.SetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode));

    const auto path = GetSettingsPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    ini.SaveFile(path.string().c_str());
}

void LightLimitFix::RestoreDefaultSettings()
{
    settings = {};
}

void LightLimitFix::DrawSettings()
{
    if (ImGui::CollapsingHeader("Light Limit Fix"))
    {
        int changed = 0;
        changed |= ImGui::Checkbox("Lights Visualisation", &settings.EnableLightsVisualisation) ? 1 : 0;

        const char *modes[] = {"Clusters", "Lights", "Both"};
        int mode = static_cast<int>(settings.LightsVisualisationMode);
        if (ImGui::Combo("Visualisation Mode", &mode, modes, IM_ARRAYSIZE(modes)))
        {
            settings.LightsVisualisationMode = static_cast<std::uint32_t>(std::clamp(mode, 0, IM_ARRAYSIZE(modes) - 1));
            changed = 1;
        }

        ImGui::Text("Lights: %u", currentLightCount);
        ImGui::Text("Clusters: %ux%ux%u", clusterSize[0], clusterSize[1], clusterSize[2]);

        if (changed)
        {
            SaveSettings();
        }
    }
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
    PerFrame perFrame{};
    perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
    perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
    perFrame.CameraNear = CameraNear;
    perFrame.CameraFar = CameraFar;
    perFrame.ClusterSize[0] = clusterSize[0];
    perFrame.ClusterSize[1] = clusterSize[1];
    perFrame.ClusterSize[2] = clusterSize[2];
    return perFrame;
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_AE)
    auto *setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
    if (setting)
    {
        setting->SetInt(0x7FFFFFFF);
        logger::info("[LightLimitFix] Unlocked magic light limit");
    }
#endif
}
