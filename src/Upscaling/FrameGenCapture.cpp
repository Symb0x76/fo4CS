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

void Upscaling::PreAlpha()
{
	if (IsLoadingMenuOpen())
		return;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	auto& colorMain = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& colorPostAlpha = rendererData->renderTargets[(uint)RenderTarget::kMainTemp];

	context->CopyResource(reinterpret_cast<ID3D11Texture2D*>(colorMain.texture), reinterpret_cast<ID3D11Texture2D*>(colorPostAlpha.texture));

	CaptureHUDLessFrame();
}


bool Upscaling::CaptureHUDLessFrame()
{
	if (IsLoadingMenuOpen())
		return false;

	// PostDisplay owns HUDLess capture whenever the D3D12 proxy swap chain exists, which
	// is every configuration that can actually run frame generation -- the shared D3D12
	// handles below are all gated on that same swap chain.
	//
	// This path is not merely a format mismatch against the shared buffer, it is the wrong
	// image. It runs from PreAlpha() at the DrawWorld_Reticle hook, mid-geometry: kMain
	// there is pre-tonemap scene-linear HDR, pre-alpha-blend and un-upscaled. Both
	// consumers want display-referred colour -- FFX extracts UI by differencing HUDLess
	// against the presented backbuffer, and BuildUIColorAndAlphaCS thresholds that
	// difference at 2/255. PostDisplay copies the proxy swap chain buffer at the 3D-to-UI
	// boundary instead: post-tonemap, post-upscale, pre-UI.
	//
	// Runtime-gated rather than gated on FALLOUT_PRE_NG: PreNG sets supportsD3D12Proxy
	// too, so it takes this same proxy path and is not a variant that can be carved out.
	if (DX12SwapChain::GetSingleton()->swapChain)
		return false;

#if defined(FALLOUT_PRE_NG)
	constexpr uint32_t frameIndex = 0;
#else
	if (!d3d12Interop)
		return false;
	const auto frameIndex = DX12SwapChain::GetSingleton()->frameIndex;
#endif
	if (!setupBuffers)
		CreateFrameGenerationResources();
	if (!setupBuffers)
		return false;

	auto rendererData = fo4cs::GetRendererData();
	if (!rendererData)
		return false;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context)
		return false;

	if (!HUDLessBufferShared[frameIndex] || !HUDLessBufferShared[frameIndex]->resource) {
		static bool loggedMissingHUDLessSource = false;
		if (!loggedMissingHUDLessSource) {
			logger::warn("[FrameGen] HUDLess capture waiting for shared target (hudLess={})",
				HUDLessBufferShared[frameIndex] != nullptr);
			loggedMissingHUDLessSource = true;
		}
		return false;
	}

	auto* frameBufferTexture = [&]() -> ID3D11Texture2D* {
		struct Candidate
		{
			RenderTarget target;
			const char* name;
		};
		constexpr Candidate candidates[] = {
			{ RenderTarget::kFrameBuffer, "kFrameBuffer" },
			{ RenderTarget::kMain, "kMain" },
			{ RenderTarget::kMainTemp, "kMainTemp" },
			{ RenderTarget::kMainPreAlpha, "kMainPreAlpha" },
		};

		D3D11_TEXTURE2D_DESC hudLessDesc{};
		HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);

		static bool loggedCandidateState = false;
		for (const auto& candidate : candidates) {
			auto& renderTarget = rendererData->renderTargets[static_cast<uint>(candidate.target)];
			auto* texture = reinterpret_cast<ID3D11Texture2D*>(renderTarget.texture);
			if (!texture) {
				continue;
			}

			D3D11_TEXTURE2D_DESC sourceDesc{};
			texture->GetDesc(&sourceDesc);
			if (!loggedCandidateState) {
				logger::info("[FrameGen] HUDLess candidate {}: {}x{} fmt={} target={}x{} fmt={}",
					candidate.name,
					sourceDesc.Width,
					sourceDesc.Height,
					static_cast<uint32_t>(sourceDesc.Format),
					hudLessDesc.Width,
					hudLessDesc.Height,
					static_cast<uint32_t>(hudLessDesc.Format));
			}

			if (sourceDesc.Width == hudLessDesc.Width &&
				sourceDesc.Height == hudLessDesc.Height &&
				sourceDesc.Format == hudLessDesc.Format) {
				if (!loggedCandidateState) {
					logger::info("[FrameGen] HUDLess capture source selected: {}", candidate.name);
					loggedCandidateState = true;
				}
				return texture;
			}
		}

		if (!loggedCandidateState) {
			logger::warn("[FrameGen] HUDLess capture has no compatible PreNG source target");
			loggedCandidateState = true;
		}
		return nullptr;
	}();
	if (!frameBufferTexture) {
		return false;
	}
#if defined(FALLOUT_PRE_NG)
	D3D11_TEXTURE2D_DESC sourceDesc{};
	D3D11_TEXTURE2D_DESC hudLessDesc{};
	frameBufferTexture->GetDesc(&sourceDesc);
	HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);
	if (sourceDesc.Width != hudLessDesc.Width || sourceDesc.Height != hudLessDesc.Height || sourceDesc.Format != hudLessDesc.Format) {
		static bool loggedHUDLessMismatch = false;
		if (!loggedHUDLessMismatch) {
			logger::warn("[FrameGen] HUDLess capture source mismatch (src={}x{} fmt={}, dst={}x{} fmt={})",
				sourceDesc.Width,
				sourceDesc.Height,
				static_cast<uint32_t>(sourceDesc.Format),
				hudLessDesc.Width,
				hudLessDesc.Height,
				static_cast<uint32_t>(hudLessDesc.Format));
			loggedHUDLessMismatch = true;
		}
		return false;
	}
#endif

	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), frameBufferTexture);
	hudLessFrameIDs[frameIndex] = NextHUDLessFrameID();
	hudLessFrameValid[frameIndex] = true;

	static bool loggedFirstHUDLessCapture = false;
	if (!loggedFirstHUDLessCapture) {
		logger::info("[FrameGen] First HUDLess frame captured (index={}, id={})", frameIndex, hudLessFrameIDs[frameIndex]);
		loggedFirstHUDLessCapture = true;
	}
	return true;
}

void Upscaling::PostDisplay()
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
	if (!rendererData) {
		return;
	}

	auto& swapChain = rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer];
	auto* swapChainRTV = reinterpret_cast<ID3D11RenderTargetView*>(swapChain.rtView);
	if (!swapChainRTV) {
		static bool loggedMissingSwapChainRTV = false;
		if (!loggedMissingSwapChainRTV) {
			logger::warn("[FrameGen] HUDLess post-display capture waiting for frame buffer RTV");
			loggedMissingSwapChainRTV = true;
		}
		return;
	}

	winrt::com_ptr<ID3D11Resource> swapChainResource;
	swapChainRTV->GetResource(swapChainResource.put());
	if (!swapChainResource) {
		static bool loggedMissingSwapChainResource = false;
		if (!loggedMissingSwapChainResource) {
			logger::warn("[FrameGen] HUDLess post-display capture waiting for frame buffer resource");
			loggedMissingSwapChainResource = true;
		}
		return;
	}

#if defined(FALLOUT_PRE_NG)
	constexpr uint32_t frameIndex = 0;
#else
	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	const auto frameIndex = dx12SwapChain->frameIndex;
#endif
	if (!HUDLessBufferShared[frameIndex] || !HUDLessBufferShared[frameIndex]->resource) {
		return;
	}

	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		return;
	}

	D3D11_TEXTURE2D_DESC sourceDesc{};
	D3D11_TEXTURE2D_DESC hudLessDesc{};
	winrt::com_ptr<ID3D11Texture2D> sourceTexture;
	if (FAILED(swapChainResource->QueryInterface(IID_PPV_ARGS(sourceTexture.put()))) || !sourceTexture) {
		static bool loggedPostDisplayNotTexture = false;
		if (!loggedPostDisplayNotTexture) {
			logger::warn("[FrameGen] HUDLess post-display source is not a Texture2D");
			loggedPostDisplayNotTexture = true;
		}
		return;
	}
	sourceTexture->GetDesc(&sourceDesc);
	HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);
	if (sourceDesc.Width != hudLessDesc.Width || sourceDesc.Height != hudLessDesc.Height || sourceDesc.Format != hudLessDesc.Format) {
		static bool loggedPostDisplayMismatch = false;
		if (!loggedPostDisplayMismatch) {
			logger::warn("[FrameGen] HUDLess post-display source mismatch (src={}x{} fmt={}, dst={}x{} fmt={})",
				sourceDesc.Width,
				sourceDesc.Height,
				static_cast<uint32_t>(sourceDesc.Format),
				hudLessDesc.Width,
				hudLessDesc.Height,
				static_cast<uint32_t>(hudLessDesc.Format));
			loggedPostDisplayMismatch = true;
		}
		return;
	}

	context->CopyResource(HUDLessBufferShared[frameIndex]->resource.get(), swapChainResource.get());
	hudLessFrameIDs[frameIndex] = NextHUDLessFrameID();
	hudLessFrameValid[frameIndex] = true;

	static bool loggedFirstPostDisplayHUDLessCapture = false;
	if (!loggedFirstPostDisplayHUDLessCapture) {
		logger::info("[FrameGen] First HUDLess post-display frame captured (index={}, id={})",
			frameIndex,
			hudLessFrameIDs[frameIndex]);
		loggedFirstPostDisplayHUDLessCapture = true;
	}
}
