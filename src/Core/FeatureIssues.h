#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace FeatureIssues
{
	enum class IssueType
	{
		OBSOLETE,
		VERSION_MISMATCH,
		OVERRIDE_FAILED,
		UNKNOWN
	};

	struct FeatureIssueInfo
	{
		std::string shortName;
		std::string version;
		std::string iniPath;
		std::string rejectionReason;
		std::string replacementFeature;
		std::string userMessage;
		IssueType issueType = IssueType::UNKNOWN;
		std::string minimumVersionRequired;
	};

	inline std::vector<FeatureIssueInfo>& GetMutableIssues()
	{
		static std::vector<FeatureIssueInfo> issues;
		return issues;
	}

	inline void AddFeatureIssue(FeatureIssueInfo a_info)
	{
		GetMutableIssues().push_back(std::move(a_info));
	}

	inline const std::vector<FeatureIssueInfo>& GetFeatureIssues()
	{
		return GetMutableIssues();
	}

	inline bool IsObsoleteFeature(std::string_view a_shortName)
	{
		// Initially empty — add entries as Features are deprecated
		static const std::unordered_map<std::string_view, std::string_view> obsoleteMap = {
			// Example: {"OldFeature", "NewFeature"},
		};
		return obsoleteMap.contains(a_shortName);
	}

	inline std::string_view GetObsoleteReplacement(std::string_view a_shortName)
	{
		static const std::unordered_map<std::string_view, std::string_view> map = {};
		auto it = map.find(a_shortName);
		return it != map.end() ? it->second : "";
	}
}
