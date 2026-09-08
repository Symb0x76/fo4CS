#include "Upscaling/Upscaler.h"

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/UpscalingInternal.h"
#include "Upscaling/UpscalingRenderTargetIDs.h"

using fo4cs::upscaling::TraceRenderBackendStage;

#include <d3dcompiler.h>

namespace
{
	ID3D11DeviceChild* CompileShaderAny(const wchar_t* filePath, const char* programType, const char* program = "main")
	{
		auto rendererData = fo4cs::GetRendererData();
		auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

		uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		winrt::com_ptr<ID3DBlob> shaderBlob;
		winrt::com_ptr<ID3DBlob> shaderErrors;

		if (!std::filesystem::exists(filePath)) {
			logger::error("[Upscaler] Failed to compile shader; file does not exist");
			return nullptr;
		}

		const auto hr = D3DCompileFromFile(filePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, program, programType, flags, 0, shaderBlob.put(), shaderErrors.put());
		if (FAILED(hr)) {
			logger::warn("[Upscaler] Shader compilation failed: {}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}

		if (std::string_view(programType).starts_with("cs")) {
			ID3D11ComputeShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}
		if (std::string_view(programType).starts_with("vs")) {
			ID3D11VertexShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}
		if (std::string_view(programType).starts_with("ps")) {
			ID3D11PixelShader* shader = nullptr;
			DX::ThrowIfFailed(device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &shader));
			return shader;
		}

		return nullptr;
	}
}

ID3D11ComputeShader* Upscaling::GetDilateMotionVectorCS()
{
	if (!dilateMotionVectorCS)
		dilateMotionVectorCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\DilateMotionVectorCS.hlsl", "cs_5_0"));
	return dilateMotionVectorCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideLinearDepthCS()
{
	if (!overrideLinearDepthCS)
		overrideLinearDepthCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\OverrideLinearDepthCS.hlsl", "cs_5_0"));
	return overrideLinearDepthCS.get();
}

ID3D11ComputeShader* Upscaling::GetOverrideDepthCS()
{
	if (!overrideDepthCS)
		overrideDepthCS.attach((ID3D11ComputeShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\OverrideDepthCS.hlsl", "cs_5_0"));
	return overrideDepthCS.get();
}

ID3D11VertexShader* Upscaling::GetCopyDepthVS()
{
	if (!copyDepthVS)
		copyDepthVS.attach((ID3D11VertexShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\CopyDepthVS.hlsl", "vs_5_0"));
	return copyDepthVS.get();
}

ID3D11PixelShader* Upscaling::GetCopyDepthPS()
{
	if (!copyDepthPS)
		copyDepthPS.attach((ID3D11PixelShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\CopyDepthPS.hlsl", "ps_5_0"));
	return copyDepthPS.get();
}

ID3D11DepthStencilState* Upscaling::GetCopyDepthStencilState()
{
	if (!copyDepthStencilState) {
		D3D11_DEPTH_STENCIL_DESC desc{};
		desc.DepthEnable = TRUE;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateDepthStencilState(&desc, copyDepthStencilState.put()));
	}
	return copyDepthStencilState.get();
}

ID3D11BlendState* Upscaling::GetCopyBlendState()
{
	if (!copyBlendState) {
		D3D11_BLEND_DESC desc{};
		desc.RenderTarget[0].RenderTargetWriteMask = 0;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateBlendState(&desc, copyBlendState.put()));
	}
	return copyBlendState.get();
}

ID3D11RasterizerState* Upscaling::GetCopyRasterizerState()
{
	if (!copyRasterizerState) {
		D3D11_RASTERIZER_DESC desc{};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateRasterizerState(&desc, copyRasterizerState.put()));
	}
	return copyRasterizerState.get();
}

ID3D11SamplerState* Upscaling::GetCopySamplerState()
{
	if (!copySamplerState) {
		D3D11_SAMPLER_DESC desc{};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxLOD = D3D11_FLOAT32_MAX;
		auto device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
		DX::ThrowIfFailed(device->CreateSamplerState(&desc, copySamplerState.put()));
	}
	return copySamplerState.get();
}

ID3D11PixelShader* Upscaling::GetBSImagespaceShaderSSLRRaytracing()
{
	if (!BSImagespaceShaderSSLRRaytracing)
		BSImagespaceShaderSSLRRaytracing.attach((ID3D11PixelShader*)CompileShaderAny(L"Data\\F4SE\\Plugins\\Upscaler\\BSImagespaceShaderSSLRRaytracing.hlsl", "ps_5_0"));
	return BSImagespaceShaderSSLRRaytracing.get();
}

ConstantBuffer* Upscaling::GetUpscalingCB()
{
	static std::unique_ptr<ConstantBuffer> upscalingCB;
	if (!upscalingCB)
		upscalingCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<UpscalingCB>());
	return upscalingCB.get();
}

void Upscaling::UpdateAndBindUpscalingCB(ID3D11DeviceContext* a_context, float2 a_screenSize, float2 a_renderSize)
{
	UpscalingCB data{};
	data.ScreenSize[0] = static_cast<uint>(a_screenSize.x);
	data.ScreenSize[1] = static_cast<uint>(a_screenSize.y);
	data.RenderSize[0] = static_cast<uint>(a_renderSize.x);
	data.RenderSize[1] = static_cast<uint>(a_renderSize.y);
	data.CameraData = float4(fo4cs::RE::GetCameraFar(), fo4cs::RE::GetCameraNear(), fo4cs::RE::GetCameraFar() - fo4cs::RE::GetCameraNear(), fo4cs::RE::GetCameraFar() * fo4cs::RE::GetCameraNear());

	auto cb = GetUpscalingCB();
	cb->Update(data);
	auto buffer = cb->CB();
	a_context->CSSetConstantBuffers(0, 1, &buffer);
}

void Upscaling::PatchSSRShader()
{
	auto context = reinterpret_cast<ID3D11DeviceContext*>(fo4cs::GetRendererData()->context);
	context->PSSetShader(GetBSImagespaceShaderSSLRRaytracing(), nullptr, 0);
}
