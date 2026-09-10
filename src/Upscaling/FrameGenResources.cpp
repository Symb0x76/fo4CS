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

#include "Upscaling/UpscalingShaderCompile.h"

void Upscaling::CreateFrameGenerationResources()
{
	logger::debug("[FrameGen] Creating resources");

	if (IsLoadingMenuOpen()) {
		setupBuffers = false;
		return;
	}
	
	setupBuffers = true;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto& main = rendererData->renderTargets[(uint)RenderTarget::kMain];
	auto& motionVector = rendererData->renderTargets[(uint)RenderTarget::kMotionVectors];
	auto& depth = rendererData->depthStencilTargets[(uint)DepthStencilTarget::kMain];
	if (!main.texture || !main.srView || !main.rtView || !motionVector.texture || !depth.srViewDepth) {
		static bool loggedPendingTargets = false;
		if (!loggedPendingTargets) {
			logger::warn(
				"[FrameGen] Render targets unavailable in CreateFrameGenerationResources; deferring "
				"(mainTex={}, mainSRV={}, mainRTV={}, motionTex={}, depthSRV={})",
				main.texture != nullptr,
				main.srView != nullptr,
				main.rtView != nullptr,
				motionVector.texture != nullptr,
				depth.srViewDepth != nullptr);
			loggedPendingTargets = true;
		}
		setupBuffers = false;
		return;
	}

	for (int index = 0; index < 2; index++) {
		D3D11_TEXTURE2D_DESC texDesc{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		reinterpret_cast<ID3D11Texture2D*>(main.texture)->GetDesc(&texDesc);
		reinterpret_cast<ID3D11ShaderResourceView*>(main.srView)->GetDesc(&srvDesc);
		reinterpret_cast<ID3D11RenderTargetView*>(main.rtView)->GetDesc(&rtvDesc);

		texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

		uavDesc.Format = texDesc.Format;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;

		texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		// Save the render-target dimensions (kMain / internal resolution)
		// for depth + motion-vector buffers, then override for HUDLess and
		// UI buffers which DLSS-G requires at backbuffer (swap chain) size.
		const auto renderWidth = texDesc.Width;
		const auto renderHeight = texDesc.Height;
		auto dx12SwapChain = DX12SwapChain::GetSingleton();

		// ---- HUDLess, UI, reticle: backbuffer resolution ----
		if (dx12SwapChain->swapChain) {
			texDesc.Width = dx12SwapChain->swapChainDesc.Width;
			texDesc.Height = dx12SwapChain->swapChainDesc.Height;
		} else {
			texDesc.Width = renderWidth;
			texDesc.Height = renderHeight;
		}

		// The shared HUDLess/UI/reticle family follows the swap chain format.
		//
		// FFX v1.1.x frameinterpolationCreate() begins with an unconditional check that
		// GetFormatPrecisionGroup(backBufferFormat) equals the group of the hudless source
		// format, and FidelityFX::SetupFrameGeneration derives backBufferFormat from this
		// very field. Reading the same variable in both places makes the two groups equal
		// by construction rather than by luck, for any swap chain format.
		//
		// This replaces #21's probe, which asked kFrameBuffer for its format through
		// RenderTarget::texture. That field is null for the swap-chain-backed slot -- its
		// views are valid but the texture pointer is not, which is why every other consumer
		// (Upscale, PostDisplay, UpdateRenderTargets) reaches kFrameBuffer through a view.
		// The probe therefore never fired, the buffer silently inherited kMain's
		// R11G11B10_FLOAT (group 5) against an R8G8B8A8_UNORM backbuffer (group 2), and the
		// first frame-generation dispatch removed the device. The comment #21 left behind,
		// claiming kFrameBuffer is R11G11B10_FLOAT, described a measurement that never
		// happened; the run log's post-display source is fmt 28.
		//
		// The format now comes from outside, so it can no longer be assumed to back the
		// views created below. texDesc already carries D3D11_BIND_UNORDERED_ACCESS and all
		// three textures get an SRV, RTV and UAV, and Texture2D::CreateUAV throws on
		// failure -- out of a render hook, with nothing to catch it. Ask the device first.
		DXGI_FORMAT hudLessFormat = texDesc.Format;
		if (dx12SwapChain->swapChain) {
			const auto candidateFormat = dx12SwapChain->swapChainDesc.Format;
			auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);

			UINT formatSupport = 0;
			const bool usable = device &&
			                    SUCCEEDED(device->CheckFormatSupport(candidateFormat, &formatSupport)) &&
			                    (formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) &&
			                    (formatSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET) &&
			                    (formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) &&
			                    (formatSupport & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);

			if (!usable) {
				// Disable rather than substitute. Any other format re-breaks the exact-match
				// CopyResource gate in PostDisplay and would leave the buffer permanently
				// black, and a black HUDLess is handed to FFX unconditionally.
				logger::error("[FrameGen] Swap chain format {} cannot back the shared HUDLess/UI/reticle views (support=0x{:X}); frame generation disabled",
					static_cast<uint32_t>(candidateFormat),
					formatSupport);
				setupBuffers = false;
				return;
			}

			hudLessFormat = candidateFormat;
		}

		texDesc.Format = hudLessFormat;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		HUDLessBufferShared[index] = new Texture2D(texDesc);
		HUDLessBufferShared[index]->CreateSRV(srvDesc);
		HUDLessBufferShared[index]->CreateRTV(rtvDesc);
		HUDLessBufferShared[index]->CreateUAV(uavDesc);

		uiColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		uiColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		uiColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		uiColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		reticleColorAndAlphaBufferShared[index] = new Texture2D(texDesc);
		reticleColorAndAlphaBufferShared[index]->CreateSRV(srvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateRTV(rtvDesc);
		reticleColorAndAlphaBufferShared[index]->CreateUAV(uavDesc);

		// ---- Depth: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		depthBufferShared[index] = new Texture2D(texDesc);
		depthBufferShared[index]->CreateSRV(srvDesc);
		depthBufferShared[index]->CreateRTV(rtvDesc);
		depthBufferShared[index]->CreateUAV(uavDesc);

		D3D11_TEXTURE2D_DESC texDescMotionVector{};
		reinterpret_cast<ID3D11Texture2D*>(motionVector.texture)->GetDesc(&texDescMotionVector);

		// ---- Motion vectors: internal render resolution ----
		texDesc.Width = renderWidth;
		texDesc.Height = renderHeight;
		texDesc.Format = texDescMotionVector.Format;
		srvDesc.Format = texDesc.Format;
		rtvDesc.Format = texDesc.Format;
		uavDesc.Format = texDesc.Format;

		motionVectorBufferShared[index] = new Texture2D(texDesc);
		motionVectorBufferShared[index]->CreateSRV(srvDesc);
		motionVectorBufferShared[index]->CreateRTV(rtvDesc);
		motionVectorBufferShared[index]->CreateUAV(uavDesc);

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(HUDLessBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&HUDLessBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(depthBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&depthBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(uiColorAndAlphaBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&uiColorAndAlphaBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		{
			IDXGIResource1* dxgiResource = nullptr;
			DX::ThrowIfFailed(motionVectorBufferShared[index]->resource->QueryInterface(IID_PPV_ARGS(&dxgiResource)));

			if (dx12SwapChain->swapChain) {
				HANDLE sharedHandle = nullptr;
				DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(
					nullptr,
					DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
					nullptr,
					&sharedHandle));

				DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(
					sharedHandle,
					IID_PPV_ARGS(&motionVectorBufferShared12[index])));

				CloseHandle(sharedHandle);
			}
		}

		FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		context->ClearRenderTargetView(HUDLessBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(depthBufferShared[index]->rtv.get(), clearColor);
		context->ClearRenderTargetView(motionVectorBufferShared[index]->rtv.get(), clearColor);
		hudLessFrameValid[index] = false;
		hudLessFrameIDs[index] = 0;
	}

	copyDepthToSharedBufferCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"CopyDepthToSharedBufferCS.hlsl", "cs_5_0");
	generateSharedBuffersCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"GenerateSharedBuffersCS.hlsl", "cs_5_0");
	buildUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildUIColorAndAlphaCS.hlsl", "cs_5_0");
	buildReticleUIColorAndAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"BuildReticleUIColorAndAlphaCS.hlsl", "cs_5_0");
	patchHUDLessReticleCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"PatchHUDLessReticleCS.hlsl", "cs_5_0");
	denoiseUIAlphaCS = (ID3D11ComputeShader*)CompileFrameGenerationShader(L"DenoiseUIAlphaCS.hlsl", "cs_5_0");
	logger::info("[FrameGen] Shared resources created (render={}x{}, hud={}x{} fmt={}, copyDepthCS={})",
		depthBufferShared[0]->desc.Width,
		depthBufferShared[0]->desc.Height,
		HUDLessBufferShared[0]->desc.Width,
		HUDLessBufferShared[0]->desc.Height,
		static_cast<uint32_t>(HUDLessBufferShared[0]->desc.Format),
		copyDepthToSharedBufferCS != nullptr);
}

void Upscaling::Reset()
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

	FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	context->ClearRenderTargetView(HUDLessBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	hudLessFrameValid[dx12SwapChain->frameIndex] = false;
	hudLessFrameIDs[dx12SwapChain->frameIndex] = 0;
	if (uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(uiColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	if (reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex])
		context->ClearRenderTargetView(reticleColorAndAlphaBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(depthBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
	context->ClearRenderTargetView(motionVectorBufferShared[dx12SwapChain->frameIndex]->rtv.get(), clearColor);
}
