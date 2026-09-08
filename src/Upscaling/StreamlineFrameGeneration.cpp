#include "Upscaling/Streamline.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Render/DX12SwapChain.h"
#include "Upscaling/Upscaler.h"
#include "Upscaling/StreamlineInternal.h"

using fo4cs::streamline::EnumToString;
using fo4cs::streamline::GetConfiguredReflexMode;
using fo4cs::streamline::ResultToString;
using fo4cs::streamline::ShouldTraceStreamlineFrame;

void Streamline::DisableDLSSGAfterError(const char* reason)
{
	if (dlssgDisabledAfterError) {
		return;
	}

	logger::error("[Streamline] Disabling DLSS-G after integration error: {}", reason);

	if (slDLSSGSetOptions) {
		sl::DLSSGOptions options{};
		options.mode = sl::DLSSGMode::eOff;
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		slDLSSGSetOptions(viewport, options);
	}

	dlssgDisabledAfterError = true;
	featureDLSSG = false;
	dlssgOptionsValid = false;
	dlssgConfiguredMode = sl::DLSSGMode::eOff;
}

bool Streamline::ConfigureDLSSG(
	ID3D12Resource* hudless,
	ID3D12Resource* uiColorAndAlpha,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	sl::DLSSGMode mode,
	const char* reason)
{
	if (!initialized || !featureDLSSG || dlssgDisabledAfterError) {
		return false;
	}

	if (!slDLSSGSetOptions) {
		DisableDLSSGAfterError("slDLSSGSetOptions is unavailable");
		return false;
	}

	auto dx12 = DX12SwapChain::GetSingleton();
	const auto hudlessDesc = hudless ? hudless->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto uiDesc = uiColorAndAlpha ? uiColorAndAlpha->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto depthDesc = depth ? depth->GetDesc() : D3D12_RESOURCE_DESC{};
	const auto motionDesc = motionVectors ? motionVectors->GetDesc() : D3D12_RESOURCE_DESC{};

	sl::DLSSGOptions options{};
	options.mode = mode;
	options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
	options.numFramesToGenerate = 1;
	options.numBackBuffers = std::max(1u, dx12->swapChainDesc.BufferCount);
	options.colorWidth = dx12->swapChainDesc.Width;
	options.colorHeight = dx12->swapChainDesc.Height;
	options.colorBufferFormat = static_cast<uint32_t>(dx12->swapChainDesc.Format);
	options.mvecDepthWidth = motionDesc.Width ? static_cast<uint32_t>(motionDesc.Width) : dx12->swapChainDesc.Width;
	options.mvecDepthHeight = motionDesc.Height ? motionDesc.Height : dx12->swapChainDesc.Height;
	options.mvecBufferFormat = static_cast<uint32_t>(motionDesc.Format);
	options.depthBufferFormat = static_cast<uint32_t>(depthDesc.Format);
	options.hudLessBufferFormat = static_cast<uint32_t>(hudlessDesc.Format);
	options.uiBufferFormat = static_cast<uint32_t>(uiDesc.Format);
	options.enableUserInterfaceRecomposition =
		uiColorAndAlpha ? sl::Boolean::eTrue : sl::Boolean::eFalse;

	const bool unchanged =
		dlssgOptionsValid &&
		dlssgConfiguredMode == mode &&
		dlssgConfiguredWidth == options.colorWidth &&
		dlssgConfiguredHeight == options.colorHeight &&
		dlssgConfiguredColorFormat == options.colorBufferFormat &&
		dlssgConfiguredMvecFormat == options.mvecBufferFormat &&
		dlssgConfiguredDepthFormat == options.depthBufferFormat &&
		dlssgConfiguredHudlessFormat == options.hudLessBufferFormat &&
		dlssgConfiguredUIFormat == options.uiBufferFormat &&
		dlssgConfiguredBackBuffers == options.numBackBuffers;

	if (unchanged) {
		return true;
	}

	if (dlssgLastSetOptionsFrame == frameID) {
		logger::warn(
			"[Streamline] Suppressing duplicate DLSS-G option change in frame {} (old={}, new={}, reason={})",
			frameID,
			EnumToString(dlssgConfiguredMode),
			EnumToString(mode),
			reason);
		return true;
	}

	ConfigureReflexForDLSSG();

	const auto result = slDLSSGSetOptions(viewport, options);
	if (result != sl::Result::eOk) {
		logger::error("[Streamline] slDLSSGSetOptions failed: {}", ResultToString(result));
		DisableDLSSGAfterError("slDLSSGSetOptions failed");
		return false;
	}

	dlssgLastSetOptionsFrame = frameID;
	dlssgOptionsValid = true;
	dlssgConfiguredMode = mode;
	dlssgConfiguredWidth = options.colorWidth;
	dlssgConfiguredHeight = options.colorHeight;
	dlssgConfiguredColorFormat = options.colorBufferFormat;
	dlssgConfiguredMvecFormat = options.mvecBufferFormat;
	dlssgConfiguredDepthFormat = options.depthBufferFormat;
	dlssgConfiguredHudlessFormat = options.hudLessBufferFormat;
	dlssgConfiguredUIFormat = options.uiBufferFormat;
	dlssgConfiguredBackBuffers = options.numBackBuffers;

	logger::info(
		"[Streamline] DLSS-G mode={} reason={} color={}x{} fmt={} hudless={}x{} fmt={} ui={}x{} fmt={} depth={}x{} fmt={} mvec={}x{} fmt={}",
		EnumToString(mode),
		reason,
		options.colorWidth,
		options.colorHeight,
		options.colorBufferFormat,
		static_cast<uint32_t>(hudlessDesc.Width),
		hudlessDesc.Height,
		options.hudLessBufferFormat,
		static_cast<uint32_t>(uiDesc.Width),
		uiDesc.Height,
		options.uiBufferFormat,
		static_cast<uint32_t>(depthDesc.Width),
		depthDesc.Height,
		options.depthBufferFormat,
		static_cast<uint32_t>(motionDesc.Width),
		motionDesc.Height,
		options.mvecBufferFormat);

	if (mode == sl::DLSSGMode::eOn && slDLSSGGetState) {
		sl::DLSSGState state{};
		const auto stateResult = slDLSSGGetState(viewport, state, nullptr);
		if (stateResult != sl::Result::eOk) {
			logger::warn("[Streamline] slDLSSGGetState failed after mode change: {}", ResultToString(stateResult));
		} else {
			logger::info(
				"[Streamline] DLSS-G state status={} minSize={} maxGenerated={} vsyncSupport={}",
				EnumToString(state.status),
				state.minWidthOrHeight,
				state.numFramesToGenerateMax,
				state.bIsVsyncSupportAvailable == sl::Boolean::eTrue);

			if (state.status != sl::DLSSGStatus::eOk) {
				DisableDLSSGAfterError("slDLSSGGetState returned non-OK status");
				return false;
			}
		}
	}

	return true;
}

bool Streamline::TagResourcesAndConfigure(
	ID3D12Resource* hudless,
	ID3D12Resource* uiColorAndAlpha,
	ID3D12Resource* depth,
	ID3D12Resource* motionVectors,
	bool enable)
{
	if (!initialized || !featureDLSSG || dlssgDisabledAfterError) {
		return false;
	}

	const auto requestedMode = enable && hudless && depth && motionVectors ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	if (!hudless || !depth || !motionVectors) {
		static bool loggedMissingResources = false;
		if (enable && !loggedMissingResources) {
			logger::warn(
				"[Streamline] DLSS-G resources are not ready; hudless={}, depth={}, mvec={}",
				hudless != nullptr,
				depth != nullptr,
				motionVectors != nullptr);
			loggedMissingResources = true;
		}
		return ConfigureDLSSG(hudless, uiColorAndAlpha, depth, motionVectors, requestedMode, "missing-resources");
	}

	if (!EnsureFrameToken("DLSS-G resource tagging")) {
		DisableDLSSGAfterError("slGetNewFrameToken failed during DLSS-G tagging");
		return false;
	}

	UpdateConstants(Upscaling::GetSingleton()->jitter);

	auto dx12 = DX12SwapChain::GetSingleton();

	sl::Resource hudlessRes{ sl::ResourceType::eTex2d, hudless, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource uiRes{ sl::ResourceType::eTex2d, uiColorAndAlpha, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource depthRes{ sl::ResourceType::eTex2d, depth, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource mvecRes{ sl::ResourceType::eTex2d, motionVectors, D3D12_RESOURCE_STATE_COMMON };

	// HUDLess and UI must match the backbuffer (swap chain) resolution.
	sl::Extent uiExtent{ 0, 0, dx12->swapChainDesc.Width, dx12->swapChainDesc.Height };

	// Depth and motion vectors are at the *internal render* resolution,
	// which may be smaller than the swap chain when DLSS/FSR upscaling is
	// active.  Using the swap-chain extent here would cause DLSS-G to read
	// beyond buffer bounds → horizontal pixel stretch at render boundary.
	auto depthDesc = depth->GetDesc();
	auto mvecDesc = motionVectors->GetDesc();
	sl::Extent renderExtent{ 0, 0, static_cast<uint32_t>(depthDesc.Width), depthDesc.Height };
	sl::Extent mvecExtent{ 0, 0, static_cast<uint32_t>(mvecDesc.Width), mvecDesc.Height };

	std::vector<sl::ResourceTag> tags;
	tags.reserve(uiColorAndAlpha ? 4u : 3u);
	tags.emplace_back(&hudlessRes, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &uiExtent);
	if (uiColorAndAlpha) {
		tags.emplace_back(&uiRes, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &uiExtent);
	}
	tags.emplace_back(&depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &renderExtent);
	tags.emplace_back(&mvecRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &mvecExtent);

	const auto tagResult = slSetTagForFrame(
		*frameToken,
		viewport,
		tags.data(),
		static_cast<uint32_t>(tags.size()),
		reinterpret_cast<sl::CommandBuffer*>(dx12->commandLists[dx12->frameIndex].get()));
	if (tagResult != sl::Result::eOk) {
		logger::error("[Streamline] slSetTagForFrame failed: {}", ResultToString(tagResult));
		DisableDLSSGAfterError("slSetTagForFrame failed");
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] DLSS-G resources tagged (frame={}, frameIndex={}, mode={}, ui={})", frameID, dx12->frameIndex, EnumToString(requestedMode), uiColorAndAlpha != nullptr);
	}

	return ConfigureDLSSG(hudless, uiColorAndAlpha, depth, motionVectors, requestedMode, enable ? "active-frame" : "inactive-frame");
}

void Streamline::LogDLSSGPresentState(bool active, uint64_t presentID)
{
	const auto settings = Upscaling::GetSingleton()->settings;
	if (!settings.debugLogging || !active || !initialized || !featureDLSSG || dlssgDisabledAfterError || !slDLSSGGetState) {
		return;
	}

	static bool loggedFirstActive = false;
	constexpr uint64_t kSampleInterval = 120;
	if (loggedFirstActive && presentID % kSampleInterval != 0) {
		return;
	}

	sl::DLSSGState state{};
	const auto stateResult = slDLSSGGetState(viewport, state, nullptr);
	if (stateResult != sl::Result::eOk) {
		logger::warn("[Streamline] slDLSSGGetState failed after Present: {}", ResultToString(stateResult));
		return;
	}

	loggedFirstActive = true;
	logger::info(
		"[Streamline] DLSS-G present state present={} frame={} status={} presentedSinceLast={} maxGenerated={} fenceValue={}",
		presentID,
		frameID,
		EnumToString(state.status),
		state.numFramesActuallyPresented,
		state.numFramesToGenerateMax,
		state.lastPresentInputsProcessingCompletionFenceValue);
}
