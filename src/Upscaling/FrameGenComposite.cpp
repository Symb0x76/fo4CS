#include "Upscaling/Upscaler.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "DirectXMath.h"
#include "Render/DX12SwapChain.h"
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

bool Upscaling::CaptureUIColorAndAlphaResource()
{
	if (IsLoadingMenuOpen())
		return false;

	if (!d3d12Interop)
		return false;

	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;

	if (!uiColorAndAlphaBufferShared[frameIndex] || !copyUIToSharedBufferCS)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData)
		return false;

	// The UI layer is the engine's own kUI target, not a reconstruction. The former
	// difference path (final frame minus HUDLess, thresholded) was a classifier whose two
	// failure modes are both visible once DLSS-G reads the alpha as "do not interpolate":
	// under-detection flickers the HUD, over-detection freezes scene pixels against the
	// interpolated background. kUI carries the real pixels and the game's own alpha.
	//
	// UpscalerRenderTargets.cpp's renderTargetsPatch deliberately covers 36/37 but not
	// 17/18, so kUI stays at native (backbuffer) size -- which is what DLSS-G's uiExtent
	// already assumes.
	//
	// Reached through the SRV rather than RenderTarget::texture: that field is null for
	// swap-chain-backed slots and this codebase has been bitten by trusting it before
	// (see the CreateFrameGenerationResources comment on #21's format probe).
	auto& uiTarget = rendererData->renderTargets[(uint)RenderTarget::kUI];
	auto* uiSRV = reinterpret_cast<ID3D11ShaderResourceView*>(uiTarget.srView);
	if (!uiSRV) {
		static bool loggedMissingUISRV = false;
		if (!loggedMissingUISRV) {
			logger::warn("[FrameGen] Engine UI target (slot {}) has no SRV; DLSS-G will interpolate the UI",
				(uint)RenderTarget::kUI);
			loggedMissingUISRV = true;
		}
		return false;
	}

	winrt::com_ptr<ID3D11Resource> uiResource;
	uiSRV->GetResource(uiResource.put());
	winrt::com_ptr<ID3D11Texture2D> uiTexture;
	if (!uiResource || FAILED(uiResource->QueryInterface(IID_PPV_ARGS(uiTexture.put()))) || !uiTexture) {
		static bool loggedUINotTexture = false;
		if (!loggedUINotTexture) {
			logger::warn("[FrameGen] Engine UI target (slot {}) is not a Texture2D; DLSS-G will interpolate the UI",
				(uint)RenderTarget::kUI);
			loggedUINotTexture = true;
		}
		return false;
	}

	D3D11_TEXTURE2D_DESC uiDesc{};
	uiTexture->GetDesc(&uiDesc);

	const auto& sharedDesc = uiColorAndAlphaBufferShared[frameIndex]->desc;

	// Reported once whether or not it matches: this is the only place a run log states what
	// slot 17 actually is, and the premise that it holds the UI layer has never been
	// confirmed against a running game.
	static bool loggedUITargetShape = false;
	if (!loggedUITargetShape) {
		logger::info("[FrameGen] UI layer source is engine slot {} ({}x{} fmt={}); shared UI target is {}x{} fmt={}",
			(uint)RenderTarget::kUI,
			uiDesc.Width,
			uiDesc.Height,
			static_cast<uint32_t>(uiDesc.Format),
			sharedDesc.Width,
			sharedDesc.Height,
			static_cast<uint32_t>(sharedDesc.Format));
		loggedUITargetShape = true;
	}

	// A size mismatch means kUI is not the backbuffer-resolution layer this assumes, and a
	// partial copy would hand DLSS-G a UI mask that is wrong over most of the screen.
	// Returning false leaves the tag off entirely, so DLSS-G interpolates the whole frame --
	// degraded, but the same thing upstream Skyrim CS does by choice.
	if (uiDesc.Width != sharedDesc.Width || uiDesc.Height != sharedDesc.Height) {
		static bool loggedUISizeMismatch = false;
		if (!loggedUISizeMismatch) {
			logger::warn("[FrameGen] Engine UI target is {}x{} but the shared UI target is {}x{}; DLSS-G will interpolate the UI",
				uiDesc.Width,
				uiDesc.Height,
				sharedDesc.Width,
				sharedDesc.Height);
			loggedUISizeMismatch = true;
		}
		return false;
	}

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return false;

	context->OMSetRenderTargets(0, nullptr, nullptr);

	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(sharedDesc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(sharedDesc.Height) / 8.0f));

	ID3D11ShaderResourceView* views[1] = { uiSRV };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { uiColorAndAlphaBufferShared[frameIndex]->uav.get() };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(copyUIToSharedBufferCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullViews[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* shader = nullptr;
	context->CSSetShader(shader, nullptr, 0);
	return true;
}
