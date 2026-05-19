#include "Core/ShaderCompiler.h"

#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <filesystem>

// Custom include handler that resolves #include paths relative to sourceRoot directories.
// Mirrors Skyrim CS behavior: "LightLimitFix/Common.hlsli" resolves to
// <sourceRoot>/LightLimitFix/Common.hlsli, not relative to the source file directory.
class ShaderIncludeHandler : public ID3DInclude
{
public:
	ShaderIncludeHandler(const std::vector<std::filesystem::path>& a_dirs)
		: m_dirs(a_dirs) {}

	HRESULT __stdcall Open(D3D_INCLUDE_TYPE /*IncludeType*/, LPCSTR pFileName,
		LPCVOID /*pParentData*/, LPCVOID* ppData, UINT* pBytes) override
	{
		for (auto& dir : m_dirs) {
			auto fullPath = dir / pFileName;
			std::error_code ec;
			if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec))
				continue;

			std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
			if (!file.is_open())
				continue;

			auto size = file.tellg();
			file.seekg(0, std::ios::beg);

			auto* buf = new char[static_cast<size_t>(size)];
			file.read(buf, size);
			*ppData = buf;
			*pBytes = static_cast<UINT>(size);
			return S_OK;
		}
		return E_FAIL;
	}

	HRESULT __stdcall Close(LPCVOID pData) override
	{
		delete[] static_cast<const char*>(pData);
		return S_OK;
	}

private:
	std::vector<std::filesystem::path> m_dirs;
};

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

		// Fallback: detect profile from filename suffix (e.g. ClusterBuildingCS.hlsl -> cs_5_0)
		auto stem = std::filesystem::path(path).stem().string();
		if (stem.size() >= 2) {
			auto suffix = stem.substr(stem.size() - 2);
			if (suffix == "VS") return "vs_5_0";
			if (suffix == "PS") return "ps_5_0";
			if (suffix == "CS") return "cs_5_0";
			if (suffix == "GS") return "gs_5_0";
			if (suffix == "HS") return "hs_5_0";
			if (suffix == "DS") return "ds_5_0";
		}

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

	std::optional<std::vector<std::byte>> ShaderCompiler::CompileFromFile(std::string_view a_shaderFileName)
	{
		auto fullPath = sourceRoot / std::string(a_shaderFileName);
		std::error_code ec;
		if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec)) {
			logger::warn("[ShaderCompiler] Source not found: {}", fullPath.string());
			return std::nullopt;
		}

		std::ifstream file(fullPath, std::ios::binary);
		if (!file.is_open()) {
			logger::warn("[ShaderCompiler] Cannot open: {}", fullPath.string());
			return std::nullopt;
		}

		std::stringstream ss;
		ss << file.rdbuf();
		auto source = ss.str();
		if (source.empty()) {
			logger::warn("[ShaderCompiler] Empty source: {}", fullPath.string());
			return std::nullopt;
		}

		auto target = InferTarget(a_shaderFileName);
		auto entryPoint = "main";

		std::vector<std::filesystem::path> includeDirs = { sourceRoot, sourceRoot / "Common" };
		std::string errorMsg;
		auto blob = CompileHLSL(source, entryPoint, target, includeDirs, &errorMsg);

		if (!blob) {
			logger::error("[ShaderCompiler] Compilation failed for {} ({}):\n{}", a_shaderFileName, target, errorMsg);
			return std::nullopt;
		}

		logger::info("[ShaderCompiler] Compiled {} ({}): {} bytes", a_shaderFileName, target, blob->GetBufferSize());

		auto* ptr = static_cast<const std::byte*>(blob->GetBufferPointer());
		return std::vector<std::byte>{ ptr, ptr + blob->GetBufferSize() };
	}

	std::optional<std::vector<std::byte>> ShaderCompiler::CompileFromFile(
		std::string_view a_shaderFileName,
		std::string_view a_target,
		const D3D_SHADER_MACRO* a_defines,
		std::string_view a_entryPoint)
	{
		auto fullPath = sourceRoot / std::string(a_shaderFileName);
		std::error_code ec;
		if (!std::filesystem::exists(fullPath, ec) || !std::filesystem::is_regular_file(fullPath, ec)) {
			logger::warn("[ShaderCompiler] Source not found: {}", fullPath.string());
			return std::nullopt;
		}

		std::ifstream file(fullPath, std::ios::binary);
		if (!file.is_open()) {
			logger::warn("[ShaderCompiler] Cannot open: {}", fullPath.string());
			return std::nullopt;
		}

		std::stringstream ss;
		ss << file.rdbuf();
		auto source = ss.str();
		if (source.empty()) {
			logger::warn("[ShaderCompiler] Empty source: {}", fullPath.string());
			return std::nullopt;
		}

		std::vector<std::filesystem::path> includeDirs = { sourceRoot, sourceRoot / "Common" };
		std::string errorMsg;
		auto blob = CompileHLSL(source, a_entryPoint, a_target, a_defines, includeDirs, &errorMsg);

		if (!blob) {
			logger::error("[ShaderCompiler] Compilation failed for {} ({}):\n{}", a_shaderFileName, a_target, errorMsg);
			return std::nullopt;
		}

		logger::info("[ShaderCompiler] Compiled {} ({}): {} bytes", a_shaderFileName, a_target, blob->GetBufferSize());
		auto* ptr = static_cast<const std::byte*>(blob->GetBufferPointer());
		return std::vector<std::byte>{ ptr, ptr + blob->GetBufferSize() };
	}

	winrt::com_ptr<ID3DBlob> ShaderCompiler::CompileHLSL(
		std::string_view a_source,
		std::string_view a_entryPoint,
		std::string_view a_target,
		const std::vector<std::filesystem::path>& a_includeDirs,
		std::string* a_error)
	{
		return CompileHLSL(a_source, a_entryPoint, a_target, nullptr, a_includeDirs, a_error);
	}

	winrt::com_ptr<ID3DBlob> ShaderCompiler::CompileHLSL(
		std::string_view a_source,
		std::string_view a_entryPoint,
		std::string_view a_target,
		const D3D_SHADER_MACRO* a_defines,
		const std::vector<std::filesystem::path>& a_includeDirs,
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

		ShaderIncludeHandler includeHandler(a_includeDirs);
		HRESULT hr = D3DCompile(
			a_source.data(), a_source.size(),
			nullptr,  // source name
			a_defines,
			&includeHandler,
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
