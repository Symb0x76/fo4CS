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

void Upscaling::UpdateDepth(float a_currentWidthRatio, float a_currentHeightRatio)
{
	auto rendererData = fo4cs::GetRendererData();
	originalDepthStencilTarget = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
	auto& original = originalDepthStencilTarget;
	auto& proxy = depthOverrideTarget;

	for (int i = 0; i < 4; i++) {
		if (proxy.dsView[i])
			proxy.dsView[i]->Release();
		if (proxy.dsViewReadOnlyDepth[i])
			proxy.dsViewReadOnlyDepth[i]->Release();
		if (proxy.dsViewReadOnlyStencil[i])
			proxy.dsViewReadOnlyStencil[i]->Release();
		if (proxy.dsViewReadOnlyDepthStencil[i])
			proxy.dsViewReadOnlyDepthStencil[i]->Release();
	}
	if (proxy.srViewDepth)
		proxy.srViewDepth->Release();
	if (proxy.srViewStencil)
		proxy.srViewStencil->Release();
	if (proxy.texture)
		proxy.texture->Release();
	proxy = {};

	if (a_currentWidthRatio == 1.0f && a_currentHeightRatio == 1.0f)
		return;

	if (!original.texture) {
		logger::warn("[Upscaler] Main depth target is unavailable; skipping depth override");
		return;
	}

	D3D11_TEXTURE2D_DESC textureDesc{};
	reinterpret_cast<ID3D11Texture2D*>(original.texture)->GetDesc(&textureDesc);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Main depth target has invalid size {}x{}; skipping depth override", textureDesc.Width, textureDesc.Height);
		return;
	}
	textureDesc.Width = static_cast<uint>(static_cast<float>(textureDesc.Width) * a_currentWidthRatio);
	textureDesc.Height = static_cast<uint>(static_cast<float>(textureDesc.Height) * a_currentHeightRatio);
	if (textureDesc.Width == 0 || textureDesc.Height == 0) {
		logger::warn("[Upscaler] Main depth target scaled to invalid size; skipping depth override");
		return;
	}

	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&proxy.texture)));
	auto texture = reinterpret_cast<ID3D11Texture2D*>(proxy.texture);

	for (int i = 0; i < 4; i++) {
		if (original.dsView[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsView[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsView[i])));
		}
		if (original.dsViewReadOnlyDepth[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyDepth[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyDepth[i])));
		}
		if (original.dsViewReadOnlyStencil[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyStencil[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyStencil[i])));
		}
		if (original.dsViewReadOnlyDepthStencil[i]) {
			D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
			reinterpret_cast<ID3D11DepthStencilView*>(original.dsViewReadOnlyDepthStencil[i])->GetDesc(&desc);
			DX::ThrowIfFailed(device->CreateDepthStencilView(texture, &desc, reinterpret_cast<ID3D11DepthStencilView**>(&proxy.dsViewReadOnlyDepthStencil[i])));
		}
	}

	if (original.srViewDepth) {
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srViewDepth)->GetDesc(&desc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srViewDepth)));
	}
	if (original.srViewStencil) {
		D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
		reinterpret_cast<ID3D11ShaderResourceView*>(original.srViewStencil)->GetDesc(&desc);
		DX::ThrowIfFailed(device->CreateShaderResourceView(texture, &desc, reinterpret_cast<ID3D11ShaderResourceView**>(&proxy.srViewStencil)));
	}
}

void Upscaling::OverrideDepth(bool a_doCopy)
{
	if (!depthOverrideTarget.texture)
		return;

	auto rendererData = fo4cs::GetRendererData();
	if (a_doCopy)
		CopyDepth();
	rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain] = depthOverrideTarget;
}

void Upscaling::ResetDepth()
{
	if (!depthOverrideTarget.texture)
		return;
	fo4cs::GetRendererData()->depthStencilTargets[(uint)DepthStencilTarget::kMain] = originalDepthStencilTarget;
}

void Upscaling::CopyDepth()
{
	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto gameViewport = fo4cs::RE::GetGraphicsState();
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();

	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	auto renderSize = float2(
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.x * renderTargetManager->dynamicWidthRatio))),
		static_cast<float>(std::max(1u, static_cast<uint32_t>(screenSize.y * renderTargetManager->dynamicHeightRatio))));
	UpdateAndBindUpscalingCB(context, screenSize, renderSize);

	ID3D11ShaderResourceView* depthSRV = reinterpret_cast<ID3D11ShaderResourceView*>(originalDepthStencilTarget.srViewDepth);
	if (!depthSRV || !depthOverrideTarget.dsView[0])
		return;

	winrt::com_ptr<ID3D11DepthStencilState> oldDepthStencilState;
	UINT oldStencilRef = 0;
	context->OMGetDepthStencilState(oldDepthStencilState.put(), &oldStencilRef);

	winrt::com_ptr<ID3D11BlendState> oldBlendState;
	FLOAT oldBlendFactor[4]{};
	UINT oldSampleMask = 0;
	context->OMGetBlendState(oldBlendState.put(), oldBlendFactor, &oldSampleMask);

	winrt::com_ptr<ID3D11RasterizerState> oldRasterizerState;
	context->RSGetState(oldRasterizerState.put());

	ID3D11RenderTargetView* oldRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
	winrt::com_ptr<ID3D11DepthStencilView> oldDSV;
	context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV.put());

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	winrt::com_ptr<ID3D11VertexShader> oldVS;
	context->VSGetShader(oldVS.put(), nullptr, nullptr);
	winrt::com_ptr<ID3D11PixelShader> oldPS;
	context->PSGetShader(oldPS.put(), nullptr, nullptr);

	ID3D11ShaderResourceView* oldPSSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
	context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, oldPSSRVs);

	winrt::com_ptr<ID3D11InputLayout> oldInputLayout;
	context->IAGetInputLayout(oldInputLayout.put());
	D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
	context->IAGetPrimitiveTopology(&oldTopology);

	auto dsvPointer = reinterpret_cast<ID3D11DepthStencilView*>(depthOverrideTarget.dsView[0]);
	context->OMSetRenderTargets(0, nullptr, dsvPointer);
	context->OMSetDepthStencilState(GetCopyDepthStencilState(), 0xFF);
	FLOAT blendFactor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
	context->OMSetBlendState(GetCopyBlendState(), blendFactor, 0xFFFFFFFF);
	context->RSSetState(GetCopyRasterizerState());

	D3D11_VIEWPORT viewport{ 0.0f, 0.0f, renderSize.x, renderSize.y, 0.0f, 1.0f };
	context->RSSetViewports(1, &viewport);

	auto upscalingBuffer = GetUpscalingCB()->CB();
	context->PSSetConstantBuffers(0, 1, &upscalingBuffer);
	context->VSSetShader(GetCopyDepthVS(), nullptr, 0);
	context->PSSetShader(GetCopyDepthPS(), nullptr, 0);
	context->PSSetShaderResources(0, 1, &depthSRV);
	ID3D11SamplerState* samplers[] = { GetCopySamplerState() };
	context->PSSetSamplers(0, 1, samplers);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->Draw(3, 0);

	context->OMSetDepthStencilState(oldDepthStencilState.get(), oldStencilRef);
	context->OMSetBlendState(oldBlendState.get(), oldBlendFactor, oldSampleMask);
	context->RSSetState(oldRasterizerState.get());
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldRTVs, oldDSV.get());
	context->RSSetViewports(numViewports, oldViewports);
	context->VSSetShader(oldVS.get(), nullptr, 0);
	context->PSSetShader(oldPS.get(), nullptr, 0);
	context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, oldPSSRVs);
	context->IASetInputLayout(oldInputLayout.get());
	context->IASetPrimitiveTopology(oldTopology);

	for (auto* rtv : oldRTVs) {
		if (rtv)
			rtv->Release();
	}
	for (auto* srv : oldPSSRVs) {
		if (srv)
			srv->Release();
	}
}
