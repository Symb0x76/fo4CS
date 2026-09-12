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

#if !defined(FALLOUT_PRE_NG)
	if (!d3d12Interop)
		return false;
#endif
	// PreNG runs the D3D12 proxy too, so these shared targets are double-buffered
	// exactly as on the other runtimes and the capture must follow the live index.
	// It was pinned to slot 0 from before the proxy existed on PreNG, which left
	// slot 1 never written while every consumer reads frameIndex alternately.
	// Without a proxy frameIndex is never assigned and stays 0, so the pre-proxy
	// behaviour is preserved for that case.
	const auto frameIndex = DX12SwapChain::GetSingleton()->frameIndex;
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

			// Match on dimensions only. A format difference is recoverable -- the
			// shared buffers are rebuilt to this source's format below -- whereas
			// a size difference is not, since CopyResource cannot rescale either.
			// Selecting the highest-priority size-compatible source and adapting
			// to it keeps this decision and the one in
			// CreateFrameGenerationResources from being made against different
			// snapshots of which render targets happen to exist.
			if (sourceDesc.Width == hudLessDesc.Width &&
				sourceDesc.Height == hudLessDesc.Height) {
				if (!loggedCandidateState) {
					logger::info("[FrameGen] HUDLess capture source selected: {}", candidate.name);
					loggedCandidateState = true;
				}
				return texture;
			}
		}

		if (!loggedCandidateState) {
			logger::warn("[FrameGen] HUDLess capture has no size-compatible source target");
			loggedCandidateState = true;
		}
		return nullptr;
	}();
	if (!frameBufferTexture) {
		return false;
	}
	// Runs on every runtime, not just PreNG: CopyResource below requires an exact
	// match, and letting a mismatched copy through is a D3D error on any of them.
	D3D11_TEXTURE2D_DESC sourceDesc{};
	D3D11_TEXTURE2D_DESC hudLessDesc{};
	frameBufferTexture->GetDesc(&sourceDesc);
	HUDLessBufferShared[frameIndex]->resource->GetDesc(&hudLessDesc);

	if (sourceDesc.Width != hudLessDesc.Width || sourceDesc.Height != hudLessDesc.Height) {
		// Not recoverable here: the shared buffers are sized to the back buffer and
		// CopyResource cannot rescale any more than it can convert.
		static bool loggedHUDLessSizeMismatch = false;
		if (!loggedHUDLessSizeMismatch) {
			logger::warn("[FrameGen] HUDLess capture source size mismatch (src={}x{}, dst={}x{})",
				sourceDesc.Width,
				sourceDesc.Height,
				hudLessDesc.Width,
				hudLessDesc.Height);
			loggedHUDLessSizeMismatch = true;
		}
		return false;
	}

	if (sourceDesc.Format != hudLessDesc.Format) {
		// The shared buffers were built in a format this source does not provide.
		// Record what the capture actually needs and rebuild; the next frame
		// matches. This is what makes the format decision self-correcting rather
		// than dependent on which render targets happened to exist at creation.
		if (hudLessCaptureFormat != sourceDesc.Format) {
			logger::info("[FrameGen] HUDLess shared buffers rebuilding to capture source format {} (was {})",
				static_cast<uint32_t>(sourceDesc.Format),
				static_cast<uint32_t>(hudLessDesc.Format));
		}
		hudLessCaptureFormat = sourceDesc.Format;
		setupBuffers = false;
		return false;
	}

	hudLessCaptureFormat = sourceDesc.Format;

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
