#pragma once

namespace CommunityShaders::Menu
{
	void Setup();
	void Draw();
	void Reset();
	[[nodiscard]] bool IsOpen() noexcept;
	void SetOpen(bool a_open) noexcept;
}
