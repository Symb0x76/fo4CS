#include "Core/ShaderCompiler.h"

#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

namespace CommunityShaders
{
	namespace
	{
		// Determine the D3D compile target from the file path.
		// Convention: Shaders/{FeatureName}/{VS,PS,CS,GS,HS,DS}/*.hlsl
		std::string InferTarget(std::string_view a_filePath)
		{
			auto path = std::string(a_filePath);
			auto normalized = std::filesystem::path(path).string();

			// Standard D3D11 profiles for FO4 (D3D11 feature level 11.0)
			if (normalized.find("/VS/") != std::string::npos || normalized.find("\\VS\\") != std::string::npos)
				return "vs_5_0";
			if (normalized.find("/PS/") != std::string::npos || normalized.find("\\PS\\") != std::string::npos)
				return "ps_5_0";
			if (normalized.find("/CS/") != std::string::npos || normalized.find("\\CS\\") != std::string::npos)
				return "cs_5_0";
			if (normalized.find("/GS/") != std::string::npos || normalized.find("\\GS\\") != std::string::npos)
				return "gs_5_0";
			if (normalized.find("/HS/") != std::string::npos || normalized.find("\\HS\\") != std::string::npos)
				return "hs_5_0";
			if (normalized.find("/DS/") != std::string::npos || normalized.find("\\DS\\") != std::string::npos)
				return "ds_5_0";

			return "ps_5_0";
		}
	}

	ShaderCompiler* ShaderCompiler::GetSingleton()
	{
		static ShaderCompiler instance;
		return &instance;
	}

	void ShaderCompiler::SetSourceRoot(std::filesystem::path a_root)
	{
		sourceRoot = std::move(a_root);
	}

	std::vector<std::byte> ShaderCompiler::CompileFromFile(std::string_view a_shaderFileName)
	{
		auto fullPath = sourceRoot / std::string(a_shaderFileName);
		std::error_code ec;
		if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec)) {
			logger::warn("[ShaderCompiler] Source not found: {}", fullPath.string());
			return {};
		}

		std::ifstream file(fullPath, std::ios::binary);
		if (!file.is_open()) {
			logger::warn("[ShaderCompiler] Cannot open: {}", fullPath.string());
			return {};
		}

		std::stringstream ss;
		ss << file.rdbuf();
		auto source = ss.str();
		if (source.empty()) {
			logger::warn("[ShaderCompiler] Empty source: {}", fullPath.string());
			return {};
		}

		auto target = InferTarget(a_shaderFileName);
		auto entryPoint = "main";

		std::vector<std::filesystem::path> includeDirs = { sourceRoot, sourceRoot / "Common" };
		std::string errorMsg;
		auto blob = CompileHLSL(source, entryPoint, target, includeDirs, &errorMsg);

		if (!blob) {
			logger::error("[ShaderCompiler] Compilation failed for {} ({}):\n{}", a_shaderFileName, target, errorMsg);
			return {};
		}

		logger::info("[ShaderCompiler] Compiled {} ({}): {} bytes", a_shaderFileName, target, blob->GetBufferSize());

		auto* ptr = static_cast<const std::byte*>(blob->GetBufferPointer());
		return { ptr, ptr + blob->GetBufferSize() };
	}

	winrt::com_ptr<ID3DBlob> ShaderCompiler::CompileHLSL(
		std::string_view a_source,
		std::string_view a_entryPoint,
		std::string_view a_target,
		const std::vector<std::filesystem::path>&,
		std::string* a_error)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;

	#if defined(_DEBUG) || !defined(NDEBUG)
		flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
	#else
		flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
	#endif

		winrt::com_ptr<ID3DBlob> code;
		winrt::com_ptr<ID3DBlob> errors;

		HRESULT hr = D3DCompile(
			a_source.data(), a_source.size(),
			nullptr,  // source name
			nullptr,  // defines
			D3D_COMPILE_STANDARD_FILE_INCLUDE,  // use standard #include
			std::string(a_entryPoint).c_str(),
			std::string(a_target).c_str(),
			flags, 0,
			code.put(), errors.put());

		if (FAILED(hr)) {
			if (errors && a_error) {
				*a_error = static_cast<const char*>(errors->GetBufferPointer());
			}
			return nullptr;
		}

		return code;
	}

	void ShaderCompiler::ReportError(ID3DBlob* a_errorBlob)
	{
		if (a_errorBlob) {
			auto* msg = static_cast<const char*>(a_errorBlob->GetBufferPointer());
			logger::error("[ShaderCompiler] {}", msg);
		}
	}
}
