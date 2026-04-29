#include "Core/Feature.h"
#include "Core/MenuRegistry.h"

Feature::Feature()
{
	MenuRegistry::Register(this);
}

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
