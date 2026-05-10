#include "Core/Menu.h"

#include "Core/Globals.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

namespace CommunityShaders::Menu
{
	namespace
	{
		bool open = false;
		bool loggedClosedPlaceholder = false;
		bool imguiInitialized = false;
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

	void Render(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		if (!a_device || !a_context) {
			return;
		}

		if (!imguiInitialized) {
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			if (!ImGui_ImplDX11_Init(a_device, a_context)) {
				logger::warn("[CommunityShaders] ImGui_ImplDX11_Init failed");
				return;
			}
			if (!ImGui_ImplWin32_Init(nullptr)) {
				logger::warn("[CommunityShaders] ImGui_ImplWin32_Init failed");
				return;
			}

			imguiInitialized = true;
			logger::info("[CommunityShaders] D3D11 ImGui initialized");
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		Draw();

		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
