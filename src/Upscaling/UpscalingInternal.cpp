#include "Upscaling/UpscalingInternal.h"

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
}
