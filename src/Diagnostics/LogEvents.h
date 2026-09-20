#pragma once

#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

// Structured runtime log events.
//
// Every event is written as one line of the form
//
//     event=<CODE> <free text>
//
// so tools/validate-runtime-log.ps1 can grep the per-plugin F4SE logs after a
// manual in-game session. Codes are a stable contract: add new ones, do not
// rename existing ones. Per-frame or high-frequency paths must not emit events.
namespace fo4cs::diagnostics
{
	enum class Event
	{
		Boot,            // logger initialised (first line of every plugin log)
		DeviceReady,     // D3D11 device seen / D3D12 proxy swap chain ready
		HookInstall,     // a group of detours or IAT hooks was installed
		FeatureState,    // effective settings after INI load + runtime fallbacks
		ResourceCreate,  // shared GPU resources created
		ResourceReset,   // shared GPU resources destroyed
		Error,           // a failure the plugin recovered from or fell back on
		Shutdown         // reserved; F4SE plugins have no unload callback today
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

	// Emits "event=<CODE> <message>" through the default (plugin) logger.
	// Error events use the error level so existing log-level filters still apply.
	template <class... Args>
	void LogEvent(Event event, spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		const auto message = spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...);
		if (event == Event::Error) {
			spdlog::error("event={} {}", Code(event), message);
		} else {
			spdlog::info("event={} {}", Code(event), message);
		}
	}
}
