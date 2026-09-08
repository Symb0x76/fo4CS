#pragma once

#include <OverlayAPI.h>

// ImGui settings panels for the three features, shared by the standalone DLLs
// (FrameGen, Reflex, Upscaler) and the all-in-one NuclearGFX host. Every callback
// receives Upscaling::Settings* as userData and re-applies runtime fallbacks after
// a change so the UI never shows a combination the runtime cannot honour.
namespace fo4cs::panels
{
	struct PanelSpec
	{
		const char* name;
		int category;
		OverlayPanelCallbacks* callbacks;  // static storage, bound to Upscaling::settings
	};

	[[nodiscard]] PanelSpec FrameGenerationPanel();
	[[nodiscard]] PanelSpec ReflexPanel();
	[[nodiscard]] PanelSpec UpscalerPanel();

	// Standalone DLLs register through the overlay host's exported
	// Overlay_RegisterPanel (Overlay.dll, or NuclearGFX.dll when it hosts the
	// overlay). Returns false when no host is loaded yet or the export is missing.
	bool RegisterWithOverlayHost(const PanelSpec& panel);
}
