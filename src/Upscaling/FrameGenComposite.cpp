#include "Upscaling/Upscaler.h"

#include "Core/DebugSwitches.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <RE/FO4Runtime.h>

#include "DirectXMath.h"
#include "Render/DX12SwapChain.h"
#include "Render/RuntimeAdapter.h"
#include "Upscaling/UpscalingInternal.h"
#include "Upscaling/UpscalingRenderTargetIDs.h"

using fo4cs::upscaling::IsLoadingMenuOpen;
using fo4cs::upscaling::NextHUDLessFrameID;

void Upscaling::PostAlpha()
{
	if (IsLoadingMenuOpen())
		return;

	if (!d3d12Interop)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;

	auto rendererData = fo4cs::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	{
		auto& colorPreAlpha = rendererData->renderTargets[(uint)RenderTarget::kMain];
		auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

		auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];

		{
			uint32_t dispatchX = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Width) / 8.0f);
			uint32_t dispatchY = (uint32_t)std::ceil(float(dx12SwapChain->swapChainDesc.Height) / 8.0f);

			ID3D11ShaderResourceView* views[4] = { 
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(motionVector.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth)
			};

			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[2] = { motionVectorBufferShared[dx12SwapChain->frameIndex]->uav.get(), depthBufferShared[dx12SwapChain->frameIndex]->uav.get()};
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(generateSharedBuffersCS, nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[4] = { nullptr, nullptr, nullptr, nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[2] = { nullptr, nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);

		if (reticleColorAndAlphaBufferShared[frameIndex] && buildReticleUIColorAndAlphaCS) {
			const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Width) / 8.0f));
			const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(dx12SwapChain->swapChainDesc.Height) / 8.0f));

			ID3D11ShaderResourceView* reticleViews[2] = {
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPreAlpha.srView),
				reinterpret_cast<ID3D11ShaderResourceView*>(colorPostAlpha.srView)
			};
			context->CSSetShaderResources(0, ARRAYSIZE(reticleViews), reticleViews);

			ID3D11UnorderedAccessView* reticleUAVs[1] = { reticleColorAndAlphaBufferShared[frameIndex]->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(reticleUAVs), reticleUAVs, nullptr);

			context->CSSetShader(buildReticleUIColorAndAlphaCS, nullptr, 0);
			context->Dispatch(dispatchX, dispatchY, 1);

			ID3D11ShaderResourceView* nullReticleViews[2] = { nullptr, nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(nullReticleViews), nullReticleViews);

			ID3D11UnorderedAccessView* nullReticleUAVs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullReticleUAVs), nullReticleUAVs, nullptr);

			context->CSSetShader(shader, nullptr, 0);
		}
	}
}

void Upscaling::CopyBuffersToSharedResources()
{
	if (IsLoadingMenuOpen())
		return;

#if !defined(FALLOUT_PRE_NG)
	if (!d3d12Interop)
		return;
#endif

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;
	auto rendererData = fo4cs::GetRendererData();

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	// Depth and motion vectors are double-buffered like the HUDLess target, so they
	// follow the live index on every runtime. PreNG previously pinned this to slot 0,
	// which meant DLSS-G was handed cleared depth and motion-vector buffers on every
	// frame where the swap chain presented from slot 1.
	const auto frameIndex = DX12SwapChain::GetSingleton()->frameIndex;
	if (!motionVectorBufferShared[frameIndex] || !depthBufferShared[frameIndex] || !copyDepthToSharedBufferCS)
		return;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	context->CopyResource(motionVectorBufferShared[frameIndex]->resource.get(), reinterpret_cast<ID3D11Texture2D*>(motionVector.texture));

	{
		auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
		const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(depthBufferShared[frameIndex]->desc.Width) / 8.0f));
		const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(depthBufferShared[frameIndex]->desc.Height) / 8.0f));

		ID3D11ShaderResourceView* views[1] = { reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { depthBufferShared[frameIndex]->uav.get() };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		context->CSSetShader(copyDepthToSharedBufferCS, nullptr, 0);
		context->Dispatch(dispatchX, dispatchY, 1);
	}

	ID3D11ShaderResourceView* views[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
}

bool Upscaling::BuildUIColorAndAlphaResource(ID3D11Texture2D* a_finalFrame)
{
	if (IsLoadingMenuOpen())
		return false;

	if (!d3d12Interop || !a_finalFrame)
		return false;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!hudLessFrameValid[frameIndex] || hudLessFrameIDs[frameIndex] == 0)
		return false;
	if (!HUDLessBufferShared[frameIndex] || !uiColorAndAlphaBufferShared[frameIndex] || !reticleColorAndAlphaBufferShared[frameIndex] || !buildUIColorAndAlphaCS)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	D3D11_TEXTURE2D_DESC finalDesc{};
	a_finalFrame->GetDesc(&finalDesc);
	if (finalDesc.Width == 0 || finalDesc.Height == 0)
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC finalSrvDesc{};
	finalSrvDesc.Format = finalDesc.Format;
	finalSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	finalSrvDesc.Texture2D.MostDetailedMip = 0;
	finalSrvDesc.Texture2D.MipLevels = 1;

	winrt::com_ptr<ID3D11ShaderResourceView> finalFrameSRV;
	if (FAILED(device->CreateShaderResourceView(a_finalFrame, &finalSrvDesc, finalFrameSRV.put()))) {
		static bool loggedSRVFailure = false;
		if (!loggedSRVFailure) {
			logger::warn("[FrameGen] Could not create final-frame SRV; DLSS-G UI alpha tag is unavailable");
			loggedSRVFailure = true;
		}
		return false;
	}

	context->OMSetRenderTargets(0, nullptr, nullptr);

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(finalDesc.Height) / 8.0f));

	ID3D11ShaderResourceView* views[3] = {
		finalFrameSRV.get(),
		HUDLessBufferShared[frameIndex]->srv.get(),
		reticleColorAndAlphaBufferShared[frameIndex]->srv.get()
	};
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(buildUIColorAndAlphaCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullViews[3] = { nullptr, nullptr, nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
	return true;
}

void Upscaling::DenoiseUIAlphaResource()
{
	if (IsLoadingMenuOpen())
		return;

	if (!d3d12Interop || !denoiseUIAlphaCS)
		return;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!uiColorAndAlphaBufferShared[frameIndex])
		return;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	D3D11_TEXTURE2D_DESC desc{};
	uiColorAndAlphaBufferShared[frameIndex]->resource->GetDesc(&desc);
	if (desc.Width == 0 || desc.Height == 0)
		return;

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(desc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(desc.Height) / 8.0f));

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShader(denoiseUIAlphaCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* nullShader = nullptr;
	context->CSSetShader(nullShader, nullptr, 0);
}
