#pragma once

#include "Upscaling/Streamline.h"

#include <cstdint>
#include <string>

#include <magic_enum/magic_enum.hpp>

// Helpers shared by the Streamline translation units. Non-template functions have
// exactly one definition in StreamlineInternal.cpp.
namespace fo4cs::streamline
{
	[[nodiscard]] std::string ResultToString(sl::Result result);

	template <class T>
	std::string EnumToString(T value)
	{
		if (const auto name = magic_enum::enum_name(value); !name.empty()) {
			return std::string(name);
		}

		return std::to_string(static_cast<int>(value));
	}

	// True when the current Present is a traced frame (see DX12SwapChain::Present)
	// and debug logging is enabled.
	[[nodiscard]] bool ShouldTraceStreamlineFrame(uint64_t frameID);

	// Reflex mode requested by Upscaling::settings.
	[[nodiscard]] sl::ReflexMode GetConfiguredReflexMode();
}
