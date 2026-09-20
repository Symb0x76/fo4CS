#include "Upscaling/UpscalingInternal.h"

#include "Upscaling/Upscaler.h"

#include "Diagnostics/HangTrace.h"

namespace fo4cs::upscaling
{
	uint64_t NextHUDLessFrameID()
	{
		static uint64_t nextFrameID = 0;
		return ++nextFrameID;
	}

	bool IsLoadingMenuOpen()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			return ui->GetMenuOpen("LoadingMenu");
		}
		return false;
	}

	void TraceRenderBackendStage(std::string_view stage)
	{
		fo4cs::diagnostics::WriteHangTraceLine(stage);

		auto upscaling = Upscaling::GetSingleton();
		if (!upscaling->debugTraceCurrentPresent) {
			return;
		}

		static std::string previousStage;
		if (previousStage == stage) {
			return;
		}

		previousStage = stage;
		logger::debug("[Upscaler] Render backend stage: {}", stage);
	}
}
