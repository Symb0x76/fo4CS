#include "Upscaling/UpscalingShaderCompile.h"

#include <array>
#include <filesystem>
#include <string_view>

#include <d3dcompiler.h>
#include <winrt/base.h>

#include "Render/RuntimeAdapter.h"

ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program)
{
	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

	// Compiler setup
	uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

	winrt::com_ptr<ID3DBlob> shaderBlob;
	winrt::com_ptr<ID3DBlob> shaderErrors;

	std::string str;
	std::wstring path{ FilePath };
	std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
		return (char)c;
	});
	if (!std::filesystem::exists(FilePath)) {
		logger::error("Failed to compile shader; {} does not exist", str);
		return nullptr;
	}
	if (FAILED(D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, shaderBlob.put(), shaderErrors.put()))) {
		logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
		return nullptr;
	}
	if (shaderErrors)
		logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

	ID3D11ComputeShader* regShader;
	DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
	return regShader;
}

ID3D11DeviceChild* CompileFrameGenerationShader(const wchar_t* fileName, const char* programType, const char* program)
{
	static constexpr std::array<std::wstring_view, 1> shaderDirectories{
		L"Data\\F4SE\\Plugins\\FrameGen"
	};

	for (const auto directory : shaderDirectories) {
		const auto path = std::filesystem::path(directory) / fileName;
		std::error_code ec;
		if (std::filesystem::exists(path, ec)) {
			return CompileShader(path.c_str(), programType, program);
		}
	}

	logger::error("[FrameGen] Failed to compile shader; {} was not found in FrameGen", std::filesystem::path(fileName).string());
	return nullptr;
}
