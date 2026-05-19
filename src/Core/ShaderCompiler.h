#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommunityShaders
{
	class ShaderCompiler
	{
	public:
		static ShaderCompiler* GetSingleton();

		// Set the root directory for shader source files (e.g. "Data/Shaders")
		void SetSourceRoot(std::filesystem::path a_root);

		// Compile a HLSL file. Returns nullopt on failure.
		// Profile is determined by the file's parent directory:
		//   VS/ → vs_5_0, PS/ → ps_5_0, CS/ → cs_5_0
		[[nodiscard]] std::optional<std::vector<std::byte>> CompileFromFile(std::string_view a_shaderFileName);

		// Compile a HLSL file with explicit profile, defines, and entry point.
		[[nodiscard]] std::optional<std::vector<std::byte>> CompileFromFile(
			std::string_view a_shaderFileName,
			std::string_view a_target,
			const D3D_SHADER_MACRO* a_defines,
			std::string_view a_entryPoint = "main");

		// Low-level: compile a single HLSL source string
		[[nodiscard]] static winrt::com_ptr<ID3DBlob> CompileHLSL(
			std::string_view a_source,
			std::string_view a_entryPoint,
			std::string_view a_target,
			const std::vector<std::filesystem::path>& a_includeDirs,
			std::string* a_error = nullptr);

		// Low-level with D3D_SHADER_MACRO defines
		[[nodiscard]] static winrt::com_ptr<ID3DBlob> CompileHLSL(
			std::string_view a_source,
			std::string_view a_entryPoint,
			std::string_view a_target,
			const D3D_SHADER_MACRO* a_defines,
			const std::vector<std::filesystem::path>& a_includeDirs,
			std::string* a_error = nullptr);

	private:
		ShaderCompiler() = default;

		static void ReportError(ID3DBlob* a_errorBlob);

		std::filesystem::path sourceRoot;
	};
}
