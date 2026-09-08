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

namespace
{
	const uint renderTargetsPatch[] = { 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61, 4, 29, 1, 36, 37, 22, 10, 11, 7, 8, 64, 14, 16 };
}

void Upscaling::UpdateRenderTarget(int index, float a_currentWidthRatio, float a_currentHeightRatio)
{
	auto rendererData = fo4cs::GetRendererData();
	originalRenderTargets[index] = rendererData->renderTargets[index];

	auto& original = originalRenderTargets[index];
	auto& proxy = proxyRenderTargets[index];

	if (proxy.uaView)
		proxy.uaView->Release();
	if (proxy.srView)
		proxy.srView->Release();
	if (proxy.rtView)
		proxy.rtView->Release();
	if (proxy.texture)
		proxy.texture->Release();
	proxy = {};

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	if (!original.texture) {
		if (settings.debugLogging) {
			logger::debug("[Upscaler] Render target {} has no texture; skipping proxy creation", index);
		}
		return;
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	reinterpret_cast<ID3D11Texture2D*>(original.texture)->GetDesc(&textureDesc);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Render target {} has invalid size {}x{}; skipping proxy creation", index, textureDesc.Width, textureDesc.Height);
		return;
	}

	D3D11_RENDER_TARGET_VIEW_DESC rtViewDesc{};
	if (original.rtView)
		reinterpret_cast<ID3D11RenderTargetView*>(original.rtView)->GetDesc(&rtViewDesc);

	D3D11_SHADER_RESOURCE_VIEW_DESC srViewDesc{};
	if (original.srView)
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srView)->GetDesc(&srViewDesc);

	D3D11_UNORDERED_ACCESS_VIEW_DESC uaViewDesc{};
	if (original.uaView)
		reinterpret_cast<ID3D11UnorderedAccessView*>(original.uaView)->GetDesc(&uaViewDesc);

	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Render target {} scaled to invalid size; skipping proxy creation", index);
		return;
	}

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxy.texture)));

	if (auto texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture)) {
		if (original.rtView)
			DX::ThrowIfFailed(device->CreateRenderTargetView(texture, &rtViewDesc, reinterpret_cast<ID3D11RenderTargetView**>(&proxy.rtView)));
		if (original.srView)
			DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &srViewDesc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srView)));
		if (original.uaView)
			DX::ThrowIfFailed(device->CreateUnorderedAccessView(texture, &uaViewDesc, reinterpret_cast<ID3D11UnorderedAccessView**>(&proxy.uaView)));
	}
}

void Upscaling::UpdateRenderTargets(float a_currentWidthRatio, float a_currentHeightRatio)
{
	static float previousWidthRatio = 0.0f;
	static float previousHeightRatio = 0.0f;
	const bool upscalingTextureRequired = upscaleMethodNoMenu != UpscaleMethod::kDisabled;
	if (previousWidthRatio == a_currentWidthRatio && previousHeightRatio == a_currentHeightRatio &&
		(!upscalingTextureRequired || upscalingTexture))
		return;

	TraceRenderBackendStage("UpdateRenderTargets");
	previousWidthRatio = a_currentWidthRatio;
	previousHeightRatio = a_currentHeightRatio;

	for (auto index : renderTargetsPatch)
		UpdateRenderTarget(index, a_currentWidthRatio, a_currentHeightRatio);

	UpdateDepth(a_currentWidthRatio, a_currentHeightRatio);

	upscalingTexture = nullptr;
	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView) {
		logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscaling texture creation");
		return;
	}
	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView);

	ID3D11Resource* frameBufferResource = nullptr;
	frameBufferSRV->GetResource(&frameBufferResource);
	if (!frameBufferResource) {
		logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscaling texture creation");
		return;
	}

	D3D11_TEXTURE2D_DESC texDesc{};
	static_cast<ID3D11Texture2D*>(frameBufferResource)->GetDesc(&texDesc);
	frameBufferResource->Release();
	if (texDesc.Width == 0 || texDesc.Height == 0) {
		logger::warn("[Upscaler] Frame buffer has invalid size {}x{}; skipping upscaling texture creation", texDesc.Width, texDesc.Height);
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	upscalingTexture = std::make_unique<Texture2D>(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);
}
