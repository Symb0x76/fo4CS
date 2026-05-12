#pragma once
#include <Windows.h>

#include <d3d11.h>

namespace CommunityShaders::Menu
{
	void Setup();
	void Draw();
	void Render(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
	void Reset();
	[[nodiscard]] bool IsOpen() noexcept;
	void SetOpen(bool a_open) noexcept;
	void SetHwnd(HWND a_hwnd) noexcept;
}
