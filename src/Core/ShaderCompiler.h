#pragma once

#include <d3d11.h>
#include <winrt/base.h>

#include <filesystem>
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

		// Compile a HLSL file. Returns compiled bytecode or empty vector on failure.
		// Profile is determined by the file's parent directory:
		//   VS/ → vs_5_0, PS/ → ps_5_0, CS/ → cs_5_0
		[[nodiscard]] std::vector<std::byte> CompileFromFile(std::string_view a_shaderFileName);

		// Low-level: compile a single HLSL source string
		[[nodiscard]] static winrt::com_ptr<ID3DBlob> CompileHLSL(
			std::string_view a_source,
			std::string_view a_entryPoint,
			std::string_view a_target,
			const std::vector<std::filesystem::path>& a_includeDirs,
			std::string* a_error = nullptr);

	private:
		ShaderCompiler() = default;

		static void ReportError(ID3DBlob* a_errorBlob);

		std::filesystem::path sourceRoot;
	};
}
