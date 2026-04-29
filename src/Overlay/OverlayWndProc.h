#pragma once

#include <backends/imgui_impl_win32.h>
#include <windows.h>

class Overlay;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

inline Overlay* g_overlay = nullptr;

inline LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
{
	if (g_overlay && g_overlay->IsInitialized()) {
		ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wParam, a_lParam);

		if (a_msg == WM_KEYDOWN && !(a_lParam & (1 << 30))) {
			const auto key = static_cast<int>(a_wParam);
			if (g_overlay->IsCapturingHotkey()) {
				g_overlay->SetHotkey(key);
				return true;
			}
			if (key == g_overlay->GetHotkey()) {
				g_overlay->ToggleVisible();
				return true;
			}
		}
	}

	if (g_overlay && g_overlay->GetPreviousWndProc()) {
		return CallWindowProcW(g_overlay->GetPreviousWndProc(), a_hwnd, a_msg, a_wParam, a_lParam);
	}
	return DefWindowProcW(a_hwnd, a_msg, a_wParam, a_lParam);
}
