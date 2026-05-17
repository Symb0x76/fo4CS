#pragma once

#include <backends/imgui_impl_win32.h>
#include <imgui.h>
#include <windows.h>

class Overlay;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

inline Overlay* g_overlay = nullptr;

inline LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
{
	if (g_overlay && (g_overlay->IsInitialized() || g_overlay->IsVisible())) {
		const auto imguiHandled = ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wParam, a_lParam);

		if (a_msg == WM_KEYDOWN && !(a_lParam & (1 << 30))) {
			const auto key = static_cast<int>(a_wParam);

			// Log first few key events to confirm WndProc is receiving input
			static int loggedKeyCount = 0;
			if (loggedKeyCount < 5) {
				++loggedKeyCount;
				logger::debug("[Overlay] WndProc received key 0x{:X} (msg #{})", static_cast<unsigned>(key), loggedKeyCount);
			}

			if (g_overlay->HandleKeyDown(key)) {
				return true;
			}
		}

		if (g_overlay->IsVisible()) {
			auto& io = ImGui::GetIO();
			const bool mouseMessage =
				a_msg >= WM_MOUSEFIRST && a_msg <= WM_MOUSELAST;
			const bool keyboardMessage =
				a_msg >= WM_KEYFIRST && a_msg <= WM_KEYLAST;

			if ((a_msg == WM_INPUT && io.WantCaptureMouse) ||
				(mouseMessage && io.WantCaptureMouse) ||
				(keyboardMessage && io.WantCaptureKeyboard) ||
				(imguiHandled && (io.WantCaptureMouse || io.WantCaptureKeyboard))) {
				return true;
			}
		}
	}

	if (g_overlay && g_overlay->GetPreviousWndProc()) {
		return CallWindowProcW(g_overlay->GetPreviousWndProc(), a_hwnd, a_msg, a_wParam, a_lParam);
	}
	return DefWindowProcW(a_hwnd, a_msg, a_wParam, a_lParam);
}
