#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace FeatureConstraints
{
	struct SettingId
	{
		std::string featureShortName;
		std::string settingPath;

		bool operator==(const SettingId& other) const
		{
			return featureShortName == other.featureShortName && settingPath == other.settingPath;
		}
	};

	struct Constraint
	{
		SettingId targetSetting;
		std::variant<bool, int, float> forcedValue;
		std::string reason;
		bool recommendDisableAtBoot = false;
	};

	struct ConstraintResult
	{
		bool isConstrained = false;
		std::variant<bool, int, float> forcedValue;

		struct Source
		{
			std::string featureName;
			std::string featureShortName;
			std::string reason;
			bool recommendDisableAtBoot;
		};
		std::vector<Source> sources;

		bool AnyRecommendDisableAtBoot() const
		{
			for (const auto& src : sources) {
				if (src.recommendDisableAtBoot)
					return true;
			}
			return false;
		}
	};

	ConstraintResult GetConstraints(const SettingId& setting);
	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints();
	std::string BuildConstraintTooltip(const ConstraintResult& result);
	std::string FormatConstraintValue(const std::variant<bool, int, float>& value);
}
