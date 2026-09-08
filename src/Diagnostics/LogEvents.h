#pragma once

#include <string_view>

namespace fo4cs::diagnostics
{
	enum class Event : std::string_view::value_type
	{
		Boot,
		DeviceReady,
		HookInstall,
		FeatureState,
		ResourceCreate,
		ResourceReset,
		Error,
		Shutdown
	};

	constexpr std::string_view Code(Event event) noexcept
	{
		switch (event) {
		case Event::Boot: return "BOOT";
		case Event::DeviceReady: return "DEVICE_READY";
		case Event::HookInstall: return "HOOK_INSTALL";
		case Event::FeatureState: return "FEATURE_STATE";
		case Event::ResourceCreate: return "RESOURCE_CREATE";
		case Event::ResourceReset: return "RESOURCE_RESET";
		case Event::Error: return "ERROR";
		case Event::Shutdown: return "SHUTDOWN";
		}
		return "UNKNOWN";
	}
}
