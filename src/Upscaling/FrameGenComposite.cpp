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

// Redirect the engine's own UI render target into our texture, the SCS
// HDRDisplay::SetUIBuffer pattern.
//
// Reading RenderTarget::kUI (e4435da) was correct but nowhere near sufficient. Measured on
// a 4K gameplay frame, the layer it produced covered 1,879 of 8,294,400 pixels -- 0.0227%,
// the crosshair and subtitles and nothing else. The compass, HP/RADS/AP/AMMO gauges and the
// rest of the HUD are in the presented frame and in NEITHER kUI NOR HUDLess, because the
// engine draws them straight into kFrameBuffer after PostDisplay. DLSS-G was therefore
// interpolating almost the entire HUD -- which is the ghosting, and it also explains why the
// power-armor dials never flickered: they live in the interpolated backbuffer, not in a
// layer that could go missing.
//
// So stop asking which slot the UI happens to land in and take the frame buffer itself for
// the duration of interface drawing. Whatever the engine composites -- Scaleform, Interface3D,
// the kUI blit -- lands in our texture because it is what kFrameBuffer points at.
//
// The hook this needs already exists. PostDisplay runs from
// SetUseDynamicResolutionViewportAsDefaultViewport(This, false), which IS the 3D-to-UI
// boundary: Upscale() has run, the scene is final, and everything after it until Present is
// interface. No new REL::ID is required, which is the part that made this look expensive.
void Upscaling::RedirectUIRenderTarget()
{
	if (IsLoadingMenuOpen())
		return;

	// Only DLSS-G consumes kBufferTypeUIColorAndAlpha. FFX gets HUDLess and does its own
	// extraction, so redirecting for it would cost a composite pass and buy nothing.
	if (!UsesDLSSFrameGeneration())
		return;

	if (!d3d12Interop || !setupBuffers)
		return;

	if (uiRedirectActive)
		return;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!uiColorAndAlphaBufferShared[frameIndex] || !uiColorAndAlphaBufferShared[frameIndex]->rtv)
		return;

	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData)
		return;

	auto& frameBuffer = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	if (!frameBuffer.rtView) {
		static bool loggedMissingFrameBufferRTV = false;
		if (!loggedMissingFrameBufferRTV) {
			logger::warn("[FrameGen] Frame buffer has no RTV; UI redirect unavailable, DLSS-G will interpolate the UI");
			loggedMissingFrameBufferRTV = true;
		}
		return;
	}

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return;

	auto* uiRTV = uiColorAndAlphaBufferShared[frameIndex]->rtv.get();

	// Cleared every frame: the layer must contain this frame's interface and nothing else,
	// and the buffer is double-buffered so last-but-one frame's UI would otherwise survive.
	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(uiRTV, clearColor);

	savedFrameBufferRTV = reinterpret_cast<void*>(frameBuffer.rtView);
	frameBuffer.rtView = reinterpret_cast<decltype(frameBuffer.rtView)>(uiRTV);
	uiRedirectActive = true;
	uiRedirectFrameIndex = frameIndex;

	// Both halves matter. Writing the field is what redirects the engine's own
	// SetRenderTarget(kFrameBuffer) calls; binding immediately covers interface draws that
	// inherit the currently bound target instead of re-selecting it.
	context->OMSetRenderTargets(1, &uiRTV, nullptr);

	static bool loggedFirstRedirect = false;
	if (!loggedFirstRedirect) {
		logger::info("[FrameGen] UI render target redirected into the shared UI layer (frameIndex={})", frameIndex);
		loggedFirstRedirect = true;
	}
}

// Puts the engine's frame buffer RTV back. Must run before anything reads the proxy swap
// chain buffer for this frame, and must run even on the paths that skip compositing --
// leaving kFrameBuffer pointing at our texture would send the next frame's scene into it.
bool Upscaling::RestoreUIRenderTarget()
{
	if (!uiRedirectActive)
		return false;

	uiRedirectActive = false;

	auto rendererData = fo4cs::GetRendererData();
	if (rendererData && savedFrameBufferRTV) {
		auto& frameBuffer = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
		frameBuffer.rtView = reinterpret_cast<decltype(frameBuffer.rtView)>(savedFrameBufferRTV);
	}
	savedFrameBufferRTV = nullptr;

	return uiRedirectFrameIndex == DX12SwapChain::GetSingleton()->frameIndex;
}

// Puts the UI back on the frame the player sees. The proxy swap chain buffer has no UI at
// this point -- it all went into our texture -- so without this the HUD is simply gone.
bool Upscaling::CompositeUIOntoPresentedFrame(ID3D11UnorderedAccessView* a_presentedFrameUAV)
{
	if (!a_presentedFrameUAV || !compositeUIOverBackbufferCS)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
	if (!uiColorAndAlphaBufferShared[frameIndex])
		return false;

	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData)
		return false;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return false;

	const auto& uiDesc = uiColorAndAlphaBufferShared[frameIndex]->desc;
	const uint32_t dispatchX = static_cast<uint32_t>(std::ceil(static_cast<float>(uiDesc.Width) / 8.0f));
	const uint32_t dispatchY = static_cast<uint32_t>(std::ceil(static_cast<float>(uiDesc.Height) / 8.0f));

	context->OMSetRenderTargets(0, nullptr, nullptr);

	ID3D11ShaderResourceView* views[1] = { uiColorAndAlphaBufferShared[frameIndex]->srv.get() };
	context->CSSetShaderResources(0, ARRAYSIZE(views), views);

	ID3D11UnorderedAccessView* uavs[1] = { a_presentedFrameUAV };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

	context->CSSetShader(compositeUIOverBackbufferCS, nullptr, 0);
	context->Dispatch(dispatchX, dispatchY, 1);

	ID3D11ShaderResourceView* nullViews[1] = { nullptr };
	context->CSSetShaderResources(0, ARRAYSIZE(nullViews), nullViews);

	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(nullUAVs), nullUAVs, nullptr);

	ID3D11ComputeShader* nullShader = nullptr;
	context->CSSetShader(nullShader, nullptr, 0);

	ReportUILayerCoverageOnce(frameIndex);
	return true;
}

// One-shot, once per session, on a frame that is known to carry a HUD.
//
// The alpha channel of this layer is the single fact that decides whether any of this works,
// and it has cost three game runs to learn each time by dumping PNGs and looking. The risk
// it measures is specific: if the engine masks alpha writes while drawing the interface, the
// colour lands but alpha stays 0, the composite above draws nothing, and DLSS-G gets an
// empty mask -- a silent failure that looks exactly like the redirect not being installed.
// A percentage in the log separates those in one line.
//
// It costs one GPU sync, once. That is cheaper than another round trip.
void Upscaling::ReportUILayerCoverageOnce(uint32_t a_frameIndex)
{
	static uint64_t compositedFrames = 0;
	static bool reported = false;
	constexpr uint64_t kReportAfterFrames = 600;

	if (reported || ++compositedFrames < kReportAfterFrames)
		return;
	reported = true;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto* source = uiColorAndAlphaBufferShared[a_frameIndex]->resource.get();
	if (!device || !context || !source)
		return;

	D3D11_TEXTURE2D_DESC desc = uiColorAndAlphaBufferShared[a_frameIndex]->desc;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;

	winrt::com_ptr<ID3D11Texture2D> staging;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.put())) || !staging) {
		logger::warn("[FrameGen] UI layer coverage: staging texture unavailable");
		return;
	}

	context->CopyResource(staging.get(), source);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
		logger::warn("[FrameGen] UI layer coverage: map failed");
		return;
	}

	// R8G8B8A8_UNORM by construction -- the shared family follows the swap chain format and
	// CreateFrameGenerationResources disables frame generation outright if that format
	// cannot back these views.
	uint64_t opaqueish = 0;
	uint64_t anyAlpha = 0;
	const auto* base = static_cast<const uint8_t*>(mapped.pData);
	for (uint32_t y = 0; y < desc.Height; ++y) {
		const auto* row = base + static_cast<size_t>(y) * mapped.RowPitch;
		for (uint32_t x = 0; x < desc.Width; ++x) {
			const uint8_t alpha = row[static_cast<size_t>(x) * 4 + 3];
			if (alpha != 0) {
				++anyAlpha;
				if (alpha > 127)
					++opaqueish;
			}
		}
	}
	context->Unmap(staging.get(), 0);

	const auto total = static_cast<uint64_t>(desc.Width) * desc.Height;
	logger::info("[FrameGen] UI layer coverage: alpha>0 on {} of {} pixels ({:.4f}%), alpha>127 on {} ({:.4f}%)",
		anyAlpha,
		total,
		total ? 100.0 * static_cast<double>(anyAlpha) / static_cast<double>(total) : 0.0,
		opaqueish,
		total ? 100.0 * static_cast<double>(opaqueish) / static_cast<double>(total) : 0.0);
}
