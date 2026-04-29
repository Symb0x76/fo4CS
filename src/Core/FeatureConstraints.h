#pragma once

#include <string>
#include <variant>
#include <vector>

namespace CommunityShaders::FeatureConstraints
{
	struct SettingId
	{
		std::string featureShortName;
		std::string settingPath;

		bool operator==(const SettingId& a_rhs) const = default;
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
		std::variant<bool, int, float> forcedValue = false;

		struct Source
		{
			std::string featureName;
			std::string featureShortName;
			std::string reason;
			bool recommendDisableAtBoot = false;
		};

		std::vector<Source> sources;

		[[nodiscard]] bool AnyRecommendDisableAtBoot() const;
	};

	[[nodiscard]] ConstraintResult GetConstraints(const SettingId& a_setting);
	[[nodiscard]] std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints();
	[[nodiscard]] std::string BuildConstraintTooltip(const ConstraintResult& a_result);
	[[nodiscard]] std::string FormatConstraintValue(const std::variant<bool, int, float>& a_value);
}
