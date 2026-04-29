#pragma once

#include "Overlay/Overlay.h"

#include <imgui.h>
#include <shellapi.h>

#include <filesystem>

namespace OverlayShaderDump
{
	inline void RenderPanel(const ShaderDumpStats& a_stats)
	{
		if (a_stats.runtimeName.empty()) {
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("ShaderDB Hunter", nullptr)) {
			ImGui::End();
			return;
		}

		ImGui::Text("Runtime: %s", a_stats.runtimeName.c_str());

		bool dumping = a_stats.dumpingEnabled;
		if (ImGui::Checkbox("Dumping", &dumping)) {
			// Toggle is handled by the Runtime/Feature callback
		}

		ImGui::Separator();
		if (ImGui::BeginTable("ShaderStats", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
			ImGui::TableSetupColumn("Stage");
			ImGui::TableSetupColumn("Unique");
			ImGui::TableSetupColumn("Dumped");
			ImGui::TableHeadersRow();

			auto addRow = [](const char* a_stage, std::uint32_t a_unique, std::uint32_t) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s", a_stage);
				ImGui::TableNextColumn();
				ImGui::Text("%u", a_unique);
				ImGui::TableNextColumn();
				ImGui::Text("-");
			};

			addRow("VS", a_stats.vsCount, 0u);
			addRow("PS", a_stats.psCount, 0u);
			addRow("CS", a_stats.csCount, 0u);

			ImGui::EndTable();
		}

		ImGui::Text("Total Unique: %u", a_stats.uniqueShaders);

		if (ImGui::Button("Open Dump Directory")) {
			const auto dumpPath = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "fo4CS" / "ShaderDump" / a_stats.runtimeName;
			std::error_code ec;
			if (std::filesystem::exists(dumpPath, ec)) {
				ShellExecuteW(nullptr, L"open", dumpPath.c_str(), nullptr, nullptr, SW_SHOW);
			}
		}

		ImGui::End();
	}
}
