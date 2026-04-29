#include "Core/Menu.h"

#include "Core/Globals.h"

namespace CommunityShaders::Menu
{
	namespace
	{
		bool open = false;
		bool loggedClosedPlaceholder = false;
	}

	void Setup() {}

	void Draw()
	{
		if (!open) {
			if (!loggedClosedPlaceholder) {
				logger::debug("[CommunityShaders] Menu placeholder is closed");
				loggedClosedPlaceholder = true;
			}
			return;
		}

		loggedClosedPlaceholder = false;
		DrawFeatureSettings();
	}

	void Reset() {}

	bool IsOpen() noexcept
	{
		return open;
	}

	void SetOpen(bool a_open) noexcept
	{
		open = a_open;
	}
}
