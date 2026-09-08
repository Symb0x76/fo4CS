#include "Upscaling/Upscaler.h"

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"
#include <RE/FO4Runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <d3dcompiler.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"
#include "Upscaling/FidelityFX.h"
#include "Render/PresentationMenuPolicy.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/UpscalingInternal.h"
#include "Upscaling/UpscalingRenderTargetIDs.h"

extern bool enbLoaded;

using fo4cs::upscaling::TraceRenderBackendStage;

namespace
{

	float GetUpscaleRatio(uint qualityMode)
	{
		switch (qualityMode) {
		case 1:
			return 1.5f;
		case 2:
			return 1.7f;
		case 3:
			return 2.0f;
		case 4:
			return 3.0f;
		default:
			return 1.0f;
		}
	}

	uint32_t ScaleRenderExtent(uint32_t displayExtent, float ratio)
	{
		return std::max(1u, static_cast<uint32_t>(static_cast<float>(displayExtent) * ratio));
	}

	void GetJitterOffset(float* outX, float* outY, uint frameIndex, int phaseCount)
	{
		const auto halton = [](uint index, uint base) {
			float result = 0.0f;
			float fraction = 1.0f / static_cast<float>(base);
			while (index > 0) {
				result += static_cast<float>(index % base) * fraction;
				index /= base;
				fraction /= static_cast<float>(base);
			}
			return result;
		};

		const uint index = (frameIndex % static_cast<uint>(std::max(1, phaseCount))) + 1;
		*outX = halton(index, 2) - 0.5f;
		*outY = halton(index, 3) - 0.5f;
	}

	void CopyNativeAABorder(ID3D11DeviceContext* context, ID3D11Resource* source, ID3D11Resource* destination, UINT width, UINT height)
	{
		if (!context || !source || !destination)
			return;

		if (width == 0 || height == 0)
			return;

		constexpr UINT kBorderPixels = 12;
		const auto border = std::min({ kBorderPixels, width / 3, height / 3 });
		if (border == 0)
			return;

		auto copyBox = [&](UINT left, UINT top, UINT right, UINT bottom) {
			D3D11_BOX box{ left, top, 0, right, bottom, 1 };
			context->CopySubresourceRegion(destination, 0, left, top, 0, source, 0, &box);
		};

		// Native AA is 1:1. Restore the final frame edges from the pre-upscale
		// color after the upscaler output copy so clamped output texels cannot
		// stretch into a visible edge strip.
		copyBox(0, 0, border, height);
		copyBox(width - border, 0, width, height);
		copyBox(0, 0, width, border);
		copyBox(0, height - border, width, height);
	}

	bool IsLoadingMenuOpen()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			return ui->GetMenuOpen("LoadingMenu");
		}
		return false;
	}

	void RestoreNativeRenderState(RE::BSGraphics::RenderTargetManager* a_renderTargetManager, RE::BSGraphics::State* a_gameViewport)
	{
		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:begin");
		auto* upscaling = Upscaling::GetSingleton();
		upscaling->jitter = { 0.0f, 0.0f };

		if (a_gameViewport) {
			fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:viewport-reset:begin");
			a_gameViewport->offsetX = 0.0f;
			a_gameViewport->offsetY = 0.0f;
			fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:viewport-reset:end");
		}

		if (a_renderTargetManager) {
			fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:rtm-reset:begin");
			a_renderTargetManager->dynamicWidthRatio = 1.0f;
			a_renderTargetManager->dynamicHeightRatio = 1.0f;
			a_renderTargetManager->isDynamicResolutionCurrentlyActivated = false;
			fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:rtm-reset:end");
		}

		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateSamplerStates:begin");
		upscaling->UpdateSamplerStates(0.0f);
		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateSamplerStates:end");
		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateRenderTargets:begin");
		upscaling->UpdateRenderTargets(1.0f, 1.0f);
		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:UpdateRenderTargets:end");
		fo4cs::diagnostics::WriteHangTraceLine("RestoreNativeRenderState:end");
	}

	const char* GetUpscaleMethodName(Upscaling::UpscaleMethod a_method)
	{
		switch (a_method) {
		case Upscaling::UpscaleMethod::kFSR:
			return "FSR";
		case Upscaling::UpscaleMethod::kDLSS:
			return "DLSS";
		default:
			return "disabled";
		}
	}

}


void Upscaling::OnD3D11DeviceCreated(ID3D11Device* a_device, IDXGIAdapter* a_adapter)
{
	(void)a_device;
	(void)a_adapter;
	renderBackendEnabled =
		(UsesDLSSUpscaling() && d3d12Interop && Streamline::GetSingleton()->featureDLSS) ||
		(UsesFSRUpscaling() && d3d12Interop && FidelityFX::GetSingleton()->featureFSR);
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod(bool a_checkMenu) const
{
	if (pluginMode != PluginMode::kUpscaler || !renderBackendEnabled)
		return UpscaleMethod::kDisabled;

	if (IsLoadingMenuOpen())
		return UpscaleMethod::kDisabled;

	if (a_checkMenu) {
		if (fo4cs::PresentationMenuPolicy::GetOpenNativePresentationMenu())
			return UpscaleMethod::kDisabled;
	}

	auto method = GetPreferredUpscaleMethod();
	if (method == UpscaleMethod::kFSR) {
		static bool loggedUnavailableFSR = false;
		const bool fsrInteropReady = d3d12Interop;
		if (!fsrInteropReady || !FidelityFX::GetSingleton()->featureFSR) {
			if (!loggedUnavailableFSR) {
				logger::warn("[Upscaler] FSR is unavailable; disabling FSR");
				loggedUnavailableFSR = true;
			}
			return UpscaleMethod::kDisabled;
		}
	}
	if (method == UpscaleMethod::kDLSS && !Streamline::GetSingleton()->featureDLSS)
		return UpscaleMethod::kDisabled;

	return method;
}


void Upscaling::CheckResources()
{
	static auto previousUpscaleMethodNoMenu = UpscaleMethod::kDisabled;

	const auto hasUpscalingResources = [&]() {
		if (upscaleMethodNoMenu == UpscaleMethod::kDisabled)
			return true;
		if (!d3d12Interop || !upscalingTexture)
			return false;
		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;
		return upscalerInputShared[frameIndex] &&
		       upscalerOutputShared[frameIndex] &&
		       upscalerInputShared12[frameIndex] &&
		       upscalerOutputShared12[frameIndex];
	};

	const bool methodChanged = previousUpscaleMethodNoMenu != upscaleMethodNoMenu;
	if (!methodChanged && hasUpscalingResources())
		return;

	TraceRenderBackendStage("CheckResources");
	if (methodChanged && previousUpscaleMethodNoMenu == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->DestroyDLSSResources();

	if (upscaleMethodNoMenu == UpscaleMethod::kDisabled) {
		DestroyUpscalingResources();
	} else {
		if (methodChanged && previousUpscaleMethodNoMenu != UpscaleMethod::kDisabled)
			DestroyUpscalingResources();
		if (!setupBuffers)
			CreateFrameGenerationResources();
		CreateUpscalingResources();
	}

	previousUpscaleMethodNoMenu = upscaleMethodNoMenu;
}

void Upscaling::UpdateGameSettings()
{
	*fo4cs::RE::GetTAAEnableFlag() = true;
}

void Upscaling::UpdateUpscaling()
{
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:enter");
	if (pluginMode != PluginMode::kUpscaler)
	{
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:not-upscaler-mode");
		return;
	}

	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:begin");
	if (IsLoadingMenuOpen()) {
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:true");
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		postLoadingSkipUpscale = true;
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:loading-menu");
		return;
	}
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:IsLoadingMenuOpen:false");

	TraceRenderBackendStage("UpdateUpscaling");
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetGraphicsState:begin");
	auto gameViewport = fo4cs::RE::GetGraphicsState();
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetGraphicsState:end");
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetRenderTargetManager:begin");
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetRenderTargetManager:end");
	if (!gameViewport || !renderTargetManager) {
		logger::warn("[Upscaler] Render backend globals are unavailable; disabling upscaling for this update");
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:missing-globals");
		return;
	}

	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:read-screen-size:begin");
	auto screenWidth = gameViewport->screenWidth;
	auto screenHeight = gameViewport->screenHeight;
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:read-screen-size:end");
	if (screenWidth == 0 || screenHeight == 0 || screenWidth > 16384 || screenHeight > 16384) {
		logger::warn("[Upscaler] Invalid viewport size {}x{}; disabling upscaling for this update", screenWidth, screenHeight);
		upscaleMethodNoMenu = UpscaleMethod::kDisabled;
		upscaleMethod = UpscaleMethod::kDisabled;
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:exit:invalid-screen-size");
		return;
	}

	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetUpscaleMethod:begin");
	const auto openNativePresentationMenu = fo4cs::PresentationMenuPolicy::GetOpenNativePresentationMenu();
	const bool useNativePresentation = openNativePresentationMenu.has_value();
	const bool enteringNativePresentation = useNativePresentation && !nativePresentationModeActive;
	const bool exitingNativePresentation = !useNativePresentation && nativePresentationModeActive;

	upscaleMethodNoMenu = GetUpscaleMethod(false);
	upscaleMethod = useNativePresentation ? UpscaleMethod::kDisabled : GetUpscaleMethod(true);
	if (enteringNativePresentation) {
		nativePresentationModeActive = true;
		nativePresentationMenu = *openNativePresentationMenu;
		postLoadingSkipUpscale = true;
	} else if (exitingNativePresentation) {
		nativePresentationModeActive = false;
		postLoadingSkipUpscale = true;
	}
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:GetUpscaleMethod:end");

	const float configuredResolutionScale =
		upscaleMethodNoMenu == UpscaleMethod::kDisabled ? 1.0f : 1.0f / GetUpscaleRatio(settings.qualityMode);
	float resolutionScale = useNativePresentation ? 1.0f : configuredResolutionScale;
	float currentMipBias = std::log2f(resolutionScale);
	if (!useNativePresentation && upscaleMethodNoMenu != UpscaleMethod::kDisabled)
		currentMipBias -= 1.0f;

	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateSamplerStates:begin");
	UpdateSamplerStates(currentMipBias);
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateSamplerStates:end");
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateRenderTargets:begin");
	UpdateRenderTargets(resolutionScale, resolutionScale);
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateRenderTargets:end");
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateGameSettings:begin");
	UpdateGameSettings();
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:UpdateGameSettings:end");

	if (upscaleMethod != UpscaleMethod::kDisabled) {
		const auto width = screenWidth;
		const auto height = screenHeight;
		const auto renderWidth = std::max(1u, static_cast<uint>(static_cast<float>(width) * resolutionScale));
		const auto renderHeight = std::max(1u, static_cast<uint>(static_cast<float>(height) * resolutionScale));
		auto phaseCount = std::max(1, static_cast<int>(8.0f * std::pow(static_cast<float>(width) / static_cast<float>(renderWidth), 2.0f)));
		GetJitterOffset(&jitter.x, &jitter.y, gameViewport->frameCount, phaseCount);
		gameViewport->offsetX = 2.0f * -jitter.x / static_cast<float>(renderWidth);
		gameViewport->offsetY = 2.0f * jitter.y / static_cast<float>(renderHeight);
	} else {
		jitter = { 0.0f, 0.0f };
		gameViewport->offsetX = 0.0f;
		gameViewport->offsetY = 0.0f;
	}

	renderTargetManager->dynamicWidthRatio = resolutionScale;
	renderTargetManager->dynamicHeightRatio = resolutionScale;
	renderTargetManager->isDynamicResolutionCurrentlyActivated = renderTargetManager->dynamicWidthRatio != 1.0f || renderTargetManager->dynamicHeightRatio != 1.0f;

	if (!nativePresentationModeActive) {
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:CheckResources:begin");
		CheckResources();
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:CheckResources:end");
	}
	if (!renderBackendEnabled || upscaleMethodNoMenu == UpscaleMethod::kDisabled) {
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:RestoreNativeRenderState:disabled:begin");
		RestoreNativeRenderState(renderTargetManager, gameViewport);
		fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:RestoreNativeRenderState:disabled:end");
	}

	if (enteringNativePresentation) {
		logger::info(
			"[Presentation] native UI mode entered menu={} configuredUpscale={} effectiveUpscale=disabled backendReady={} interop={} frameGen=held taa=held scale=1.000 jitter=0",
			nativePresentationMenu,
			GetUpscaleMethodName(GetPreferredUpscaleMethod()),
			renderBackendEnabled,
			d3d12Interop);
	} else if (exitingNativePresentation) {
		logger::info(
			"[Presentation] native UI mode exited menu={} configuredUpscale={} effectiveUpscale={} backendReady={} interop={} taa=restored scale={:.3f} frameGenSettle={}",
			nativePresentationMenu,
			GetUpscaleMethodName(GetPreferredUpscaleMethod()),
			GetUpscaleMethodName(upscaleMethodNoMenu),
			renderBackendEnabled,
			d3d12Interop,
			configuredResolutionScale,
			fo4cs::PresentationMenuPolicy::kFrameGenerationPostMenuSettlePresents);
		nativePresentationMenu = {};
	}
	fo4cs::diagnostics::WriteHangTraceLine("UpdateUpscaling:exit");
}

bool Upscaling::Upscale()
{
	if (postLoadingSkipUpscale) {
		postLoadingSkipUpscale = false;
		return false;
	}

	if (upscaleMethod == UpscaleMethod::kDisabled || !upscalingTexture)
		return false;

	TraceRenderBackendStage("Upscale");
	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[(uint)RenderTarget::kFrameBuffer].srView);
	if (!frameBufferSRV) {
		logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscale dispatch");
		return false;
	}
	ID3D11Resource* frameBufferResource = nullptr;
	frameBufferSRV->GetResource(&frameBufferResource);
	if (!frameBufferResource) {
		logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscale dispatch");
		return false;
	}
	// The full-resolution backup is only consumed by the 1:1 Native AA edge
	// restore. Super-resolution modes feed their exact internal extent directly
	// into the shared input texture below.
	if (settings.qualityMode == 0)
		context->CopyResource(upscalingTexture->resource.get(), frameBufferResource);

	auto gameViewport = fo4cs::RE::GetGraphicsState();
	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	auto screenSize = float2(float(gameViewport->screenWidth), float(gameViewport->screenHeight));
	const auto renderWidth = ScaleRenderExtent(static_cast<uint32_t>(gameViewport->screenWidth), renderTargetManager->dynamicWidthRatio);
	const auto renderHeight = ScaleRenderExtent(static_cast<uint32_t>(gameViewport->screenHeight), renderTargetManager->dynamicHeightRatio);
	auto renderSize = float2(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
	bool dispatchedUpscale = false;

	if (upscaleMethod == UpscaleMethod::kDLSS && d3d12Interop) {
		if (!setupBuffers)
			CreateFrameGenerationResources();

		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;

		const bool missingSharedColor =
			!upscalerInputShared[frameIndex] || !upscalerOutputShared[frameIndex] ||
			!upscalerInputShared12[frameIndex] || !upscalerOutputShared12[frameIndex];
		const bool mismatchedSharedColor =
			upscalerInputShared[frameIndex] &&
			(upscalerInputShared[frameIndex]->desc.Width != renderWidth ||
			 upscalerInputShared[frameIndex]->desc.Height != renderHeight ||
			 upscalerInputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format);

		if (missingSharedColor || mismatchedSharedColor)
			CreateUpscalingResources();

		if (upscalerInputShared[frameIndex] && upscalerOutputShared[frameIndex] &&
			upscalerInputShared12[frameIndex] && upscalerOutputShared12[frameIndex] &&
			depthBufferShared12[frameIndex] && motionVectorBufferShared12[frameIndex]) {
			D3D11_BOX srcBox{ 0, 0, 0, renderWidth, renderHeight, 1 };
			context->CopySubresourceRegion(
				upscalerInputShared[frameIndex]->resource.get(),
				0,
				0,
				0,
				0,
				frameBufferResource,
				0,
				&srcBox);
			CopyBuffersToSharedResources();

			auto commandList = dx12SwapChain->BeginInteropCommandList();
			const bool dispatched = Streamline::GetSingleton()->Upscale(
				commandList,
				upscalerInputShared12[frameIndex].get(),
				upscalerOutputShared12[frameIndex].get(),
				depthBufferShared12[frameIndex].get(),
				motionVectorBufferShared12[frameIndex].get(),
				jitter,
				renderSize,
				screenSize,
				settings.qualityMode);
			dx12SwapChain->ExecuteInteropCommandListAndWait();

			if (dispatched) {
				dispatchedUpscale = true;
				context->CopyResource(frameBufferResource, upscalerOutputShared[frameIndex]->resource.get());
				if (settings.qualityMode == 0 && upscalingTexture)
					CopyNativeAABorder(context, upscalingTexture->resource.get(), frameBufferResource, upscalingTexture->desc.Width, upscalingTexture->desc.Height);
			}
		}
	} else if (upscaleMethod == UpscaleMethod::kFSR && d3d12Interop) {
		if (!setupBuffers)
			CreateFrameGenerationResources();

		auto dx12SwapChain = DX12SwapChain::GetSingleton();
		const auto frameIndex = dx12SwapChain->frameIndex;

		const bool missingSharedColor =
			!upscalerInputShared[frameIndex] || !upscalerOutputShared[frameIndex] ||
			!upscalerInputShared12[frameIndex] || !upscalerOutputShared12[frameIndex];
		const bool mismatchedSharedColor =
			(upscalerInputShared[frameIndex] &&
			 (upscalerInputShared[frameIndex]->desc.Width != renderWidth ||
			  upscalerInputShared[frameIndex]->desc.Height != renderHeight ||
			  upscalerInputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format)) ||
			(upscalerOutputShared[frameIndex] &&
			 (upscalerOutputShared[frameIndex]->desc.Width != upscalingTexture->desc.Width ||
			  upscalerOutputShared[frameIndex]->desc.Height != upscalingTexture->desc.Height ||
			  upscalerOutputShared[frameIndex]->desc.Format != upscalingTexture->desc.Format));

		if (missingSharedColor || mismatchedSharedColor)
			CreateUpscalingResources();

		if (upscalerInputShared[frameIndex] && upscalerOutputShared[frameIndex] &&
			upscalerInputShared12[frameIndex] && upscalerOutputShared12[frameIndex] &&
			depthBufferShared12[frameIndex] && motionVectorBufferShared12[frameIndex]) {
			D3D11_BOX srcBox{ 0, 0, 0, renderWidth, renderHeight, 1 };
			context->CopySubresourceRegion(
				upscalerInputShared[frameIndex]->resource.get(),
				0,
				0,
				0,
				0,
				frameBufferResource,
				0,
				&srcBox);
			CopyBuffersToSharedResources();

			auto commandList = dx12SwapChain->BeginInteropCommandList();
			const bool dispatched = FidelityFX::GetSingleton()->Upscale(
				commandList,
				upscalerInputShared12[frameIndex].get(),
				upscalerOutputShared12[frameIndex].get(),
				depthBufferShared12[frameIndex].get(),
				motionVectorBufferShared12[frameIndex].get(),
				jitter,
				renderSize,
				screenSize,
				settings.qualityMode);
			dx12SwapChain->ExecuteInteropCommandListAndWait();

			if (dispatched) {
				dispatchedUpscale = true;
				context->CopyResource(frameBufferResource, upscalerOutputShared[frameIndex]->resource.get());
				if (settings.qualityMode == 0 && upscalingTexture)
					CopyNativeAABorder(context, upscalingTexture->resource.get(), frameBufferResource, upscalingTexture->desc.Width, upscalingTexture->desc.Height);
			}
		}
	}

	frameBufferResource->Release();
	return dispatchedUpscale;
}

bool Upscaling::CreateUpscalingResources()
{
	TraceRenderBackendStage("CreateUpscalingResources");
	auto renderer = fo4cs::GetRendererData();
	auto& main = renderer->renderTargets[(uint)RenderTarget::kMain];
	if (!main.texture) {
		static bool loggedMissingMain = false;
		if (!loggedMissingMain) {
			logger::warn("[Upscaler] Main render target is unavailable; deferring upscaling shared resources");
			loggedMissingMain = true;
		}
		return false;
	}
	const bool needsDLSSSharedResources = UsesDLSSUpscaling() && Streamline::GetSingleton()->featureDLSS;
	const bool needsFSRSharedResources = UsesFSRUpscaling() && FidelityFX::GetSingleton()->featureFSR;
	if (!needsDLSSSharedResources && !needsFSRSharedResources)
		return false;

	auto dx12SwapChain = DX12SwapChain::GetSingleton();
	if (!d3d12Interop || !dx12SwapChain->d3d12Device)
		return false;

	D3D11_TEXTURE2D_DESC presentationColorDesc{};
	if (upscalingTexture) {
		presentationColorDesc = upscalingTexture->desc;
	} else {
		auto& frameBuffer = renderer->renderTargets[(uint)RenderTarget::kFrameBuffer];
		auto frameBufferSRV = reinterpret_cast<ID3D11ShaderResourceView*>(frameBuffer.srView);
		if (!frameBufferSRV) {
			logger::debug("[Upscaler] Frame buffer SRV is unavailable; skipping upscaling shared resources");
			return false;
		}

		winrt::com_ptr<ID3D11Resource> frameBufferResource;
		frameBufferSRV->GetResource(frameBufferResource.put());
		if (!frameBufferResource) {
			logger::debug("[Upscaler] Frame buffer resource is unavailable; skipping upscaling shared resources");
			return false;
		}

		winrt::com_ptr<ID3D11Texture2D> frameBufferTexture;
		if (FAILED(frameBufferResource->QueryInterface(IID_PPV_ARGS(frameBufferTexture.put()))) || !frameBufferTexture) {
			logger::warn("[Upscaler] Frame buffer resource is not a Texture2D; skipping upscaling shared resources");
			return false;
		}

		frameBufferTexture->GetDesc(&presentationColorDesc);
	}
	if (presentationColorDesc.Width == 0 || presentationColorDesc.Height == 0) {
		logger::warn("[Upscaler] Frame buffer target has invalid size {}x{}; skipping upscaling shared resources", presentationColorDesc.Width, presentationColorDesc.Height);
		return false;
	}
	presentationColorDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;
	presentationColorDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
	presentationColorDesc.CPUAccessFlags = 0;
	presentationColorDesc.Usage = D3D11_USAGE_DEFAULT;

	auto renderTargetManager = fo4cs::RE::GetRenderTargetManager();
	const auto renderWidthRatio = renderTargetManager ? renderTargetManager->dynamicWidthRatio : 1.0f;
	const auto renderHeightRatio = renderTargetManager ? renderTargetManager->dynamicHeightRatio : 1.0f;
	const auto renderWidth = ScaleRenderExtent(presentationColorDesc.Width, renderWidthRatio);
	const auto renderHeight = ScaleRenderExtent(presentationColorDesc.Height, renderHeightRatio);

	// Both DLSS and FSR consume the low-resolution active extent. Keeping the
	// DLSS input at presentation size copied stale pixels outside that extent and
	// doubled the color-copy bandwidth for 2560x1440 -> 3840x2160.
	D3D11_TEXTURE2D_DESC inputColorDesc = presentationColorDesc;
	inputColorDesc.Width = renderWidth;
	inputColorDesc.Height = renderHeight;
	D3D11_TEXTURE2D_DESC outputColorDesc = presentationColorDesc;

	const auto openSharedTexture = [&](Texture2D* texture, winrt::com_ptr<ID3D12Resource>& outResource) {
		winrt::com_ptr<IDXGIResource1> dxgiResource;
		DX::ThrowIfFailed(texture->resource->QueryInterface(IID_PPV_ARGS(dxgiResource.put())));
		HANDLE sharedHandle = nullptr;
		DX::ThrowIfFailed(dxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &sharedHandle));
		DX::ThrowIfFailed(dx12SwapChain->d3d12Device->OpenSharedHandle(sharedHandle, IID_PPV_ARGS(outResource.put())));
		CloseHandle(sharedHandle);
	};

	for (int index = 0; index < 2; index++) {
		delete upscalerInputShared[index];
		delete upscalerOutputShared[index];
		upscalerInputShared[index] = new Texture2D(inputColorDesc);
		upscalerOutputShared[index] = new Texture2D(outputColorDesc);
		upscalerInputShared12[index] = nullptr;
		upscalerOutputShared12[index] = nullptr;
		openSharedTexture(upscalerInputShared[index], upscalerInputShared12[index]);
		openSharedTexture(upscalerOutputShared[index], upscalerOutputShared12[index]);
	}

	if (needsFSRSharedResources) {
		const bool fsrSetupSucceeded = FidelityFX::GetSingleton()->SetupUpscaling(
			dx12SwapChain->d3d12Device.get(),
			inputColorDesc.Width,
			inputColorDesc.Height,
			outputColorDesc.Width,
			outputColorDesc.Height);
		if (!fsrSetupSucceeded) {
			logger::error("[Upscaler] FSR upscaling context creation failed; disabling upscaling backend");
			upscaleMethodNoMenu = UpscaleMethod::kDisabled;
			upscaleMethod = UpscaleMethod::kDisabled;
			renderBackendEnabled = false;
			return false;
		}
	}

	logger::info("[Upscaler] Upscaling shared resources created (input={}x{} fmt={}, output={}x{} fmt={}, final={}x{} fmt={}, fsr={}, dlss={})",
		inputColorDesc.Width,
		inputColorDesc.Height,
		static_cast<uint32_t>(inputColorDesc.Format),
		outputColorDesc.Width,
		outputColorDesc.Height,
		static_cast<uint32_t>(outputColorDesc.Format),
		presentationColorDesc.Width,
		presentationColorDesc.Height,
		static_cast<uint32_t>(presentationColorDesc.Format),
		needsFSRSharedResources,
		needsDLSSSharedResources);
	return true;
}

void Upscaling::DestroyUpscalingResources()
{
	dilatedMotionVectorTexture.reset();
	FidelityFX::GetSingleton()->DestroyUpscaling();

	for (int index = 0; index < 2; index++) {
		delete upscalerInputShared[index];
		delete upscalerOutputShared[index];
		upscalerInputShared[index] = nullptr;
		upscalerOutputShared[index] = nullptr;
		upscalerInputShared12[index] = nullptr;
		upscalerOutputShared12[index] = nullptr;
	}
}

