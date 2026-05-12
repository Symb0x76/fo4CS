#include "Core/Menu.h"

#include "Core/Globals.h"

namespace CommunityShaders::Menu
{
	void Setup() {}
	void Draw()
	{
		// D3D11 CS Menu removed — Feature settings rendered via D3D12 Overlay.
	}
	void Render(ID3D11Device*, ID3D11DeviceContext*)
	{
		// D3D11 CS Menu removed — Feature settings rendered via D3D12 Overlay.
	}
	void Reset() {}
	bool IsOpen() noexcept { return false; }
	void SetOpen(bool) noexcept {}
	void SetHwnd(HWND) noexcept {}
}
