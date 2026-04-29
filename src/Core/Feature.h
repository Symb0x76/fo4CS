#pragma once

#include "Core/FeatureCategories.h"
#include "Core/FeatureConstraints.h"
#include "Core/FeatureVersions.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommunityShaders
{
	class Feature
	{
	public:
		struct SettingSearchEntry
		{
			std::string label;
			std::string description;
			std::function<void()> focusCallback;
			std::string featureName;
		};

		virtual ~Feature() = default;

		bool loaded = false;
		std::string version;
		std::string failedLoadedMessage;

		[[nodiscard]] virtual std::string GetName() = 0;
		[[nodiscard]] virtual std::string GetShortName() = 0;
		[[nodiscard]] virtual std::string GetFeatureModLink() { return {}; }
		[[nodiscard]] virtual std::string_view GetShaderDefineName() { return {}; }
		[[nodiscard]] virtual std::vector<std::pair<std::string_view, std::string_view>> GetShaderDefineOptions() { return {}; }
		[[nodiscard]] virtual std::vector<SettingSearchEntry> GetSettingsSearchEntries() { return {}; }

		[[nodiscard]] virtual bool HasShaderDefine(std::int32_t) { return false; }
		[[nodiscard]] virtual bool SupportsVR() { return false; }
		[[nodiscard]] virtual bool IsCore() const { return false; }
		[[nodiscard]] virtual std::string_view GetCategory() const { return FeatureCategories::kOther; }
		[[nodiscard]] virtual bool IsInMenu() const { return true; }
		[[nodiscard]] virtual bool DrawFailLoadMessage() const { return true; }
		[[nodiscard]] virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() { return {}; }

		virtual void SetupResources() {}
		virtual void Reset() {}
		virtual void DrawSettings() {}
		virtual void DrawUnloadedUI();

		virtual void ReflectionsPrepass() {}
		virtual void Prepass() {}
		virtual void EarlyPrepass() {}

		virtual void Load() {}
		virtual void DataLoaded() {}
		virtual void PostPostLoad() {}

		virtual void SaveSettings() {}
		virtual void LoadSettings() {}
		virtual void RestoreDefaultSettings() {}
		virtual bool ToggleAtBootSetting();
		virtual bool ReapplyOverrideSettings();
	};
}
