#include "Core/FeatureConstraints.h"

#include <algorithm>
#include <format>

namespace CommunityShaders::FeatureConstraints
{
	bool ConstraintResult::AnyRecommendDisableAtBoot() const
	{
		return std::ranges::any_of(sources, [](const auto& a_source) {
			return a_source.recommendDisableAtBoot;
		});
	}

	ConstraintResult GetConstraints(const SettingId&)
	{
		return {};
	}

	std::vector<std::pair<SettingId, ConstraintResult>> GetAllActiveConstraints()
	{
		return {};
	}

	std::string BuildConstraintTooltip(const ConstraintResult& a_result)
	{
		if (!a_result.isConstrained) {
			return {};
		}

		std::string tooltip = std::format("Forced to {} by:", FormatConstraintValue(a_result.forcedValue));
		for (const auto& source : a_result.sources) {
			tooltip += std::format("\n- {}: {}", source.featureName, source.reason);
		}

		return tooltip;
	}

	std::string FormatConstraintValue(const std::variant<bool, int, float>& a_value)
	{
		return std::visit([](const auto& a_inner) -> std::string {
			using T = std::decay_t<decltype(a_inner)>;
			if constexpr (std::is_same_v<T, bool>) {
				return a_inner ? "true" : "false";
			} else {
				return std::format("{}", a_inner);
			}
		}, a_value);
	}
}
