#include "Render/DX12SwapChain.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <dx12/ffx_api_dx12.hpp>
#include <dx12/ffx_api_framegeneration_dx12.hpp>
#include <dxgi1_6.h>
#include <string_view>

#include <directx/d3dx12.h>

#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/Upscaler.h"

#include "Render/DX12SwapChainInternal.h"
#include "Diagnostics/LogEvents.h"

extern bool enbLoaded;

using fo4cs::render::ResolveOverlayCallbacks;
using fo4cs::render::s_overlayInitCb;
using fo4cs::render::s_overlayPollCb;
using fo4cs::render::s_overlayPresentCb;
using fo4cs::diagnostics::Event;
using fo4cs::diagnostics::LogEvent;

namespace
{
std::string FormatHRESULT(HRESULT hr)
	{
		return std::format("0x{:08X}", static_cast<std::uint32_t>(hr));
	}

	enum class PresentTracePhase
	{
		kNone,
		kLoading,
		kGameplay
	};

	const char* GetPresentTracePhaseName(PresentTracePhase phase)
	{
		switch (phase) {
		case PresentTracePhase::kLoading:
			return "loading";
		case PresentTracePhase::kGameplay:
			return "gameplay";
		default:
			return "none";
		}
	}

	bool ShouldTracePresentFrame(uint64_t presentID, PresentTracePhase tracePhase)
	{
		const auto settings = Upscaling::GetSingleton()->settings;
		if (!settings.debugLogging || settings.debugFrameLogCount <= 0) {
			return false;
		}

		const auto bootstrapFrames = static_cast<uint64_t>(std::min(settings.debugFrameLogCount, 12));
		if (presentID < bootstrapFrames) {
			return true;
		}

		static PresentTracePhase previousTracePhase = PresentTracePhase::kNone;
		static uint64_t traceWindowStart = UINT64_MAX;

		if (tracePhase != PresentTracePhase::kNone && tracePhase != previousTracePhase) {
			traceWindowStart = presentID;
			logger::info(
				"[DX12SwapChain] Present trace window started at present={} phase={} for {} frames",
				presentID,
				GetPresentTracePhaseName(tracePhase),
				settings.debugFrameLogCount);
		}
		previousTracePhase = tracePhase;

		return traceWindowStart != UINT64_MAX &&
			presentID >= traceWindowStart &&
			presentID - traceWindowStart < static_cast<uint64_t>(settings.debugFrameLogCount);
	}

	struct ScopedPresentTraceFlag
	{
		explicit ScopedPresentTraceFlag(Upscaling* a_upscaling, bool a_enabled) :
			upscaling(a_upscaling)
		{
			if (upscaling) {
				upscaling->debugTraceCurrentPresent = a_enabled;
			}
		}

		~ScopedPresentTraceFlag()
		{
			if (upscaling) {
				upscaling->debugTraceCurrentPresent = false;
			}
		}

		Upscaling* upscaling;
	};

	const char* GetFrameGenerationBackendName(bool dlss, bool fsr)
	{
		if (dlss) {
			return "DLSS-G";
		}
		if (fsr) {
			return "FSR-FG";
		}
		return "none";
	}

	enum class FrameGenerationBlockReason
	{
		kUIUnavailable,
		kBlockingMenuOpen,
		kPostLoadingSettle
	};

	struct FrameGenerationBlock
	{
		FrameGenerationBlockReason reason;
		std::string_view detail;

		constexpr bool operator==(const FrameGenerationBlock&) const = default;
	};

	// VATS, HUD, dialogue, and workshop remain FrameGen-enabled as gameplay surfaces.
	constexpr std::array kFrameGenerationBlockingMenus{
		std::string_view{ "MainMenu" },
		std::string_view{ "LoadingMenu" },
		std::string_view{ "FaderMenu" },
		std::string_view{ "PauseMenu" },
		std::string_view{ "PipboyMenu" },
		std::string_view{ "TerminalMenu" },
		std::string_view{ "ExamineMenu" },
		std::string_view{ "ExamineConfirmMenu" },
		std::string_view{ "ContainerMenu" },
		std::string_view{ "BarterMenu" },
		std::string_view{ "LockpickingMenu" },
		std::string_view{ "MessageBoxMenu" },
		std::string_view{ "SitWaitMenu" },
		std::string_view{ "HolotapeMenu" },
		std::string_view{ "PipboyHolotapeMenu" },
		std::string_view{ "TerminalHolotapeMenu" },
		std::string_view{ "PowerArmorModMenu" }
	};

	const char* GetFrameGenerationBlockReasonName(FrameGenerationBlockReason reason)
	{
		switch (reason) {
		case FrameGenerationBlockReason::kUIUnavailable:
			return "ui-unavailable";
		case FrameGenerationBlockReason::kBlockingMenuOpen:
			return "menu-open";
		case FrameGenerationBlockReason::kPostLoadingSettle:
			return "post-loading-settle";
		default:
			return "unknown";
		}
	}

	std::optional<FrameGenerationBlock> GetFrameGenerationUIBlock()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return FrameGenerationBlock{ FrameGenerationBlockReason::kUIUnavailable, "UI" };
		}

		const auto openMenu = std::ranges::find_if(kFrameGenerationBlockingMenus, [ui](std::string_view menu) {
			return ui->GetMenuOpen(menu.data());
		});
		if (openMenu != kFrameGenerationBlockingMenus.end()) {
			return FrameGenerationBlock{ FrameGenerationBlockReason::kBlockingMenuOpen, *openMenu };
		}

		return std::nullopt;
	}

	std::string_view GetFrameGenerationBlockReasonName(const std::optional<FrameGenerationBlock>& block)
	{
		return block ? GetFrameGenerationBlockReasonName(block->reason) : "none";
	}

	std::string_view GetFrameGenerationBlockDetail(const std::optional<FrameGenerationBlock>& block)
	{
		return block ? block->detail : "none";
	}

}

HRESULT DX12SwapChain::Present(UINT SyncInterval, UINT Flags)
{
	static std::atomic_uint64_t presentCounter{ 0 };
	static bool lastFrameGenerationActive = false;
	static const char* lastFrameGenerationBackend = "none";
	static std::optional<FrameGenerationBlock> lastFrameGenerationBlock;

	const auto presentID = presentCounter.fetch_add(1, std::memory_order_relaxed);
	auto upscaling = Upscaling::GetSingleton();
	auto streamline = Streamline::GetSingleton();
	const auto frameGenerationUIBlock = GetFrameGenerationUIBlock();
	const bool loadingMenuOpen =
		frameGenerationUIBlock &&
		frameGenerationUIBlock->reason == FrameGenerationBlockReason::kBlockingMenuOpen &&
		frameGenerationUIBlock->detail == "LoadingMenu";

	const auto tracePhase = loadingMenuOpen ? PresentTracePhase::kLoading :
		(!frameGenerationUIBlock ? PresentTracePhase::kGameplay : PresentTracePhase::kNone);
	const bool traceFrame = ShouldTracePresentFrame(presentID, tracePhase);
	ScopedPresentTraceFlag scopedTraceFlag(upscaling, traceFrame);
	const char* stage = "begin";
	const auto trace = [&](const char* nextStage) {
		stage = nextStage;
	};

	try {
		if (traceFrame) {
			logger::debug("[DX12SwapChain] Present#{} begin frameIndex={}", presentID, frameIndex);
		}
		trace("reflex-sleep");
		streamline->SleepReflexFrame("present");



		ID3D11Texture2D* finalFrame = enbLoaded ? swapChainBufferProxyENB->resource11 : swapChainBufferProxy->resource.get();

		trace("copy-d3d11-proxy-to-shared");
		if (enbLoaded)
			d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);
		else
			d3d11Context->CopyResource(swapChainBufferWrapped[frameIndex]->resource11, finalFrame);

		const bool uiColorAndAlphaReady =
			upscaling->UsesDLSSFrameGeneration() &&
			upscaling->BuildUIColorAndAlphaResource(swapChainBufferWrapped[frameIndex]->resource11);

		trace("wait-d3d11-to-d3d12");
		DX::ThrowIfFailed(d3d11Context->Signal(d3d11Fence.get(), fenceValue));
		DX::ThrowIfFailed(commandQueue->Wait(d3d12Fence.get(), fenceValue));
		fenceValue++;

		WaitForCommandAllocator(frameIndex);
		trace("reset-command-list");
		DX::ThrowIfFailed(commandAllocators[frameIndex]->Reset());
		DX::ThrowIfFailed(commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr));

		auto fakeSwapChain = swapChainBufferWrapped[frameIndex]->resource.get();
		auto realSwapChain = swapChainBuffers[frameIndex].get();

		trace("copy-shared-to-backbuffer");
		{
			CD3DX12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
			};
			commandLists[frameIndex]->ResourceBarrier(2, barriers);
		}

		commandLists[frameIndex]->CopyResource(realSwapChain, fakeSwapChain);

		{
			CD3DX12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(fakeSwapChain, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(realSwapChain, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT)
			};
			commandLists[frameIndex]->ResourceBarrier(2, barriers);
		}

		bool useFrameGenerationThisFrame = false;
		auto fidelityFX = FidelityFX::GetSingleton();
		const bool useDLSSFrameGeneration = upscaling->UsesDLSSFrameGeneration() && streamline->featureDLSSG;
		const bool useFSRFrameGeneration = upscaling->UsesFSRFrameGeneration() && fidelityFX->featureFrameGen;
		const bool frameGenerationBackendAvailable = useDLSSFrameGeneration || useFSRFrameGeneration;
		const char* frameGenerationBackend = GetFrameGenerationBackendName(useDLSSFrameGeneration, useFSRFrameGeneration);

		bool skipFgAfterLoading = false;
		{
			static bool wasLoading = false;
			if (!loadingMenuOpen && wasLoading) {
				skipFgAfterLoading = true;
				upscaling->postLoadingSkipUpscale = true;
			}
			wasLoading = loadingMenuOpen;
		}

		auto frameGenerationBlock = frameGenerationUIBlock;
		if (!frameGenerationBlock && skipFgAfterLoading) {
			frameGenerationBlock = FrameGenerationBlock{ FrameGenerationBlockReason::kPostLoadingSettle, "post-loading" };
		}

		useFrameGenerationThisFrame = frameGenerationBackendAvailable && !frameGenerationBlock;

		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex &&
			(presentID == 0 ||
				lastFrameGenerationActive != useFrameGenerationThisFrame ||
				std::string_view(lastFrameGenerationBackend) != frameGenerationBackend ||
				lastFrameGenerationBlock != frameGenerationBlock)) {
			logger::info(
				"[FrameGen] present={} backend={} active={} available={} phase={} block={} detail={}",
				presentID,
				frameGenerationBackend,
				useFrameGenerationThisFrame,
				frameGenerationBackendAvailable,
				GetPresentTracePhaseName(tracePhase),
				GetFrameGenerationBlockReasonName(frameGenerationBlock),
				GetFrameGenerationBlockDetail(frameGenerationBlock));
			lastFrameGenerationActive = useFrameGenerationThisFrame;
			lastFrameGenerationBackend = frameGenerationBackend;
			lastFrameGenerationBlock = frameGenerationBlock;
		}

		trace("frame-generation");
		if (useDLSSFrameGeneration) {
			const bool dlssgTagged = streamline->TagResourcesAndConfigure(
				upscaling->HUDLessBufferShared12[frameIndex].get(),
				uiColorAndAlphaReady ? upscaling->uiColorAndAlphaBufferShared12[frameIndex].get() : nullptr,
				upscaling->depthBufferShared12[frameIndex].get(),
				upscaling->motionVectorBufferShared12[frameIndex].get(),
				useFrameGenerationThisFrame);
			if (!dlssgTagged && useFrameGenerationThisFrame) {
				logger::warn("[FrameGen] DLSS-G skipped this frame after Streamline tagging/configuration failure");
				useFrameGenerationThisFrame = false;
			}
		}

		if (useFSRFrameGeneration) {
			fidelityFX->Present(useFrameGenerationThisFrame);
		}


		// Fallback hotkey polling. Works even if WndProc hook is displaced
		ResolveOverlayCallbacks();
		if (auto pollCb = s_overlayPollCb ? s_overlayPollCb : overlayPollCallback) {
			pollCb();
		}

		trace("overlay");
		if (auto presentCb = s_overlayPresentCb ? s_overlayPresentCb : overlayPresentCallback) {
			auto* backBuffer = swapChainBuffers[frameIndex].get();
			CD3DX12_RESOURCE_BARRIER toRT = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
			commandLists[frameIndex]->ResourceBarrier(1, &toRT);
			presentCb(commandLists[frameIndex].get(), backBuffer, swapChainDesc.Format);
			CD3DX12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
			commandLists[frameIndex]->ResourceBarrier(1, &toPresent);
		}

		trace("close-command-list");
		DX::ThrowIfFailed(commandLists[frameIndex]->Close());

		trace("execute-command-list");
		ID3D12CommandList* commandListsToExecute[] = { commandLists[frameIndex].get() };
		commandQueue->ExecuteCommandLists(1, commandListsToExecute);

		// Fix FPS cap being e.g. 55 instead of 60
		if (!upscaling->highFPSPhysicsFixLoaded && SyncInterval > 0)
			SyncInterval = 1;

		streamline->SetPCLMarker(sl::PCLMarker::ePresentStart, "present-start");
		trace("present");
		const auto presentResult = swapChain->Present(SyncInterval, Flags);
		if (FAILED(presentResult)) {
			logger::error("[DX12SwapChain] IDXGISwapChain::Present failed: {}", FormatHRESULT(presentResult));
			streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame(); return presentResult;
		}

		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end");

		if (useDLSSFrameGeneration) {
			trace("dlssg-present-state");
			streamline->LogDLSSGPresentState(useFrameGenerationThisFrame, presentID);
		}

		trace("wait-d3d12-to-d3d11");
		DX::ThrowIfFailed(commandQueue->Signal(d3d12Fence.get(), fenceValue));
		commandAllocatorFenceValues[frameIndex] = fenceValue;
		DX::ThrowIfFailed(d3d11Context->Wait(d3d11Fence.get(), fenceValue));
		fenceValue++;

		streamline->AdvanceFrame();

		trace("skip-frame-latency-wait");

		trace("update-frame-index");
		frameIndex = swapChain->GetCurrentBackBufferIndex();

		trace("reset-shared-resources");
		if (frameGenerationBackendAvailable) {
			upscaling->Reset();
		}

		trace("game-frame-limiter");
		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && !upscaling->highFPSPhysicsFixLoaded)
			upscaling->GameFrameLimiter();

		trace("frame-limiter");
		if (upscaling->pluginMode != Upscaling::PluginMode::kReflex && SyncInterval == 0)
			upscaling->FrameLimiter(useFrameGenerationThisFrame);

		if (traceFrame) {
			logger::debug("[DX12SwapChain] Present#{} completed (nextFrameIndex={})", presentID, frameIndex);
		}

		return S_OK;
	} catch (const winrt::hresult_error& e) {
		const auto hr = static_cast<HRESULT>(e.code());
		LogEvent(Event::Error, "[DX12SwapChain] Present failed at stage '{}' with HRESULT {}", stage, FormatHRESULT(hr));
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return hr;
	} catch (const std::exception& e) {
		LogEvent(Event::Error, "[DX12SwapChain] Present failed at stage '{}': {}", stage, e.what());
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return DXGI_ERROR_DEVICE_REMOVED;
	} catch (...) {
		LogEvent(Event::Error, "[DX12SwapChain] Present failed at stage '{}' with unknown exception", stage);
		commandLists[frameIndex]->Close();
		commandAllocators[frameIndex]->Reset();
		commandLists[frameIndex]->Reset(commandAllocators[frameIndex].get(), nullptr);
		streamline->SetPCLMarker(sl::PCLMarker::ePresentEnd, "present-end"); streamline->AdvanceFrame();
		return DXGI_ERROR_DEVICE_REMOVED;
	}
}
