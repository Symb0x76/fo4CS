#include "Core/Feature.h"

namespace CommunityShaders
{
	void Feature::DrawUnloadedUI()
	{
		if (!failedLoadedMessage.empty()) {
			logger::warn("[CommunityShaders] {} unloaded: {}", GetName(), failedLoadedMessage);
		}
	}

	bool Feature::ToggleAtBootSetting()
	{
		return false;
	}

	bool Feature::ReapplyOverrideSettings()
	{
		return false;
	}
}
