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

namespace
{
	sl::float4x4 IdentityMatrix()
	{
		return {
			sl::float4(1.0f, 0.0f, 0.0f, 0.0f),
			sl::float4(0.0f, 1.0f, 0.0f, 0.0f),
			sl::float4(0.0f, 0.0f, 1.0f, 0.0f),
			sl::float4(0.0f, 0.0f, 0.0f, 1.0f),
		};
	}
}

bool Streamline::EnsureFrameToken(const char* caller)
{
	if (!initialized || !slGetNewFrameToken) {
		logger::error("[Streamline] {} cannot get frame token; Streamline is not fully initialized", caller);
		frameToken = nullptr;
		return false;
	}

	if (frameToken && frameTokenFrameID == frameID) {
		return true;
	}

	uint32_t fid = static_cast<uint32_t>(frameID);
	const auto result = slGetNewFrameToken(frameToken, &fid);
	if (result != sl::Result::eOk || !frameToken) {
		logger::error("[Streamline] {} failed to get frame token for frame {}: {}", caller, frameID, ResultToString(result));
		frameToken = nullptr;
		return false;
	}

	frameTokenFrameID = frameID;
	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] {} frame token ready (frame={})", caller, frameID);
	}
	return true;
}

void Streamline::AdvanceFrame()
{
	if (initialized) {
		frameID++;
	}
}

bool Streamline::Upscale(
	ID3D12GraphicsCommandList* a_commandList,
	ID3D12Resource* a_color,
	ID3D12Resource* a_output,
	ID3D12Resource* a_depth,
	ID3D12Resource* a_motionVectors,
	float2 a_jitter,
	float2 a_renderSize,
	float2 a_displaySize,
	uint a_qualityMode)
{
	if (!initialized || !featureDLSS || !slDLSSSetOptions || !slEvaluateFeature || !a_commandList || !a_color || !a_output || !a_depth || !a_motionVectors)
		return false;

	UpdateConstants(a_jitter);
	if (!frameToken)
		return false;

	sl::DLSSMode dlssMode = sl::DLSSMode::eDLAA;
	switch (a_qualityMode) {
	case 1:
		dlssMode = sl::DLSSMode::eMaxQuality;
		break;
	case 2:
		dlssMode = sl::DLSSMode::eBalanced;
		break;
	case 3:
		dlssMode = sl::DLSSMode::eMaxPerformance;
		break;
	case 4:
		dlssMode = sl::DLSSMode::eUltraPerformance;
		break;
	default:
		break;
	}

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = dlssMode;
	dlssOptions.outputWidth = std::max(1u, static_cast<uint32_t>(a_displaySize.x));
	dlssOptions.outputHeight = std::max(1u, static_cast<uint32_t>(a_displaySize.y));
	auto upscaling = Upscaling::GetSingleton();
	dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
	dlssOptions.useAutoExposure = sl::Boolean::eTrue;

	// Apply DLSS preset per-quality-mode.
	// 0 = Auto (eDefault — DLSS auto-selects), 10 = J, 11 = K, 12 = L, 13 = M.
	const auto presetSetting = upscaling->settings.dlssPreset;
	const auto resolvePreset = [](int presetValue) -> sl::DLSSPreset {
		if (presetValue >= 10 && presetValue <= 15)
			return static_cast<sl::DLSSPreset>(presetValue);
		return sl::DLSSPreset::eDefault;
	};
	const auto p = resolvePreset(presetSetting);
	dlssOptions.dlaaPreset = p;
	dlssOptions.qualityPreset = p;
	dlssOptions.balancedPreset = p;
	dlssOptions.performancePreset = p;
	dlssOptions.ultraPerformancePreset = p;
	dlssOptions.ultraQualityPreset = p;

	const auto optionsResult = slDLSSSetOptions(viewport, dlssOptions);
	if (optionsResult != sl::Result::eOk) {
		logger::warn("[Streamline] Could not enable DLSS: {}", ResultToString(optionsResult));
		return false;
	}

	if (Upscaling::GetSingleton()->settings.debugLogging && presetSetting != 0) {
		logger::debug("[Streamline] DLSS preset overridden to {} (mode={})", presetSetting, EnumToString(dlssMode));
	}

	sl::Extent lowResExtent{ 0, 0, std::max(1u, static_cast<uint32_t>(a_renderSize.x)), std::max(1u, static_cast<uint32_t>(a_renderSize.y)) };
	sl::Extent fullExtent{ 0, 0, std::max(1u, static_cast<uint32_t>(a_displaySize.x)), std::max(1u, static_cast<uint32_t>(a_displaySize.y)) };

	sl::Resource colorIn{ sl::ResourceType::eTex2d, a_color, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource colorOut{ sl::ResourceType::eTex2d, a_output, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource depth{ sl::ResourceType::eTex2d, a_depth, D3D12_RESOURCE_STATE_COMMON };
	sl::Resource mvec{ sl::ResourceType::eTex2d, a_motionVectors, D3D12_RESOURCE_STATE_COMMON };

	sl::ResourceTag colorInTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
	sl::ResourceTag colorOutTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
	sl::ResourceTag depthTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };
	sl::ResourceTag mvecTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &lowResExtent };

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view, &colorInTag, &colorOutTag, &depthTag, &mvecTag };
	const auto evalResult = slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), reinterpret_cast<sl::CommandBuffer*>(a_commandList));
	if (evalResult != sl::Result::eOk) {
		logger::warn("[Streamline] DLSS evaluation failed: {}", ResultToString(evalResult));
		return false;
	}

	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug(
			"[Streamline] DLSS evaluated (frame={}, render={}x{}, output={}x{}, mode={})",
			frameID,
			static_cast<uint32_t>(a_renderSize.x),
			static_cast<uint32_t>(a_renderSize.y),
			dlssOptions.outputWidth,
			dlssOptions.outputHeight,
			EnumToString(dlssMode));
	}

	return true;
}

void Streamline::UpdateConstants(float2 a_jitter)
{
	if (!slGetNewFrameToken || !slSetConstants)
		return;

	if (constantsFrameID == frameID) {
		if (ShouldTraceStreamlineFrame(frameID)) {
			logger::debug("[Streamline] DLSS constants already set for frame {}", frameID);
		}
		return;
	}

	sl::Constants slConstants{};
	auto dx12 = DX12SwapChain::GetSingleton();
	const float aspectRatio =
		dx12->swapChainDesc.Height != 0 ?
			static_cast<float>(dx12->swapChainDesc.Width) / static_cast<float>(dx12->swapChainDesc.Height) :
			(16.0f / 9.0f);

	slConstants.cameraViewToClip = IdentityMatrix();
	slConstants.clipToCameraView = IdentityMatrix();
	slConstants.clipToPrevClip = IdentityMatrix();
	slConstants.prevClipToClip = IdentityMatrix();
	slConstants.cameraPinholeOffset = { 0.0f, 0.0f };
	slConstants.cameraPos = { 0.0f, 0.0f, 0.0f };
	slConstants.cameraUp = { 0.0f, 1.0f, 0.0f };
	slConstants.cameraRight = { 1.0f, 0.0f, 0.0f };
	slConstants.cameraFwd = { 0.0f, 0.0f, 1.0f };
	slConstants.cameraNear = 0.0f;
	slConstants.cameraFar = 1.0f;
	slConstants.cameraFOV = 1.0471976f;
	slConstants.cameraAspectRatio = aspectRatio;
	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.jitterOffset = { -a_jitter.x, -a_jitter.y };
	slConstants.mvecScale = { 1.0f, 1.0f };
	slConstants.reset = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (!EnsureFrameToken("DLSS constants")) {
		return;
	}

	const auto result = slSetConstants(slConstants, *frameToken, viewport);
	if (result != sl::Result::eOk) {
		logger::warn("[Streamline] Could not set DLSS constants: {}", ResultToString(result));
		return;
	}

	constantsFrameID = frameID;
	if (ShouldTraceStreamlineFrame(frameID)) {
		logger::debug("[Streamline] DLSS constants set (frame={}, jitter={}, {})", frameID, a_jitter.x, a_jitter.y);
	}
}

void Streamline::DestroyDLSSResources()
{
	if (!featureDLSS || !slDLSSSetOptions)
		return;

	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);

	if (slFreeResources)
		slFreeResources(sl::kFeatureDLSS, viewport);
}
