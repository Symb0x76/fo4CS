#include "Core/Hooks.h"
#include "Core/HooksInternal.h"
#include "Core/DiagnosticsFormatter.h"
#include "Core/Globals.h"
#include "Features/LightLimitFix.h"
#include "Core/ShaderCache.h"
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
#if defined(FALLOUT_PRE_NG)
	namespace
	{
		constexpr std::uint32_t kPreNGDFLightVanillaFullShadowed920AsmHash = 0xFB077F61u;
		constexpr std::uint32_t kPreNGDFLightVanillaFullShadowed922AsmHash = 0xA2D7B576u;
		constexpr std::uint32_t kPreNGDefaultDFLightDrawStateProofSamples = 128;
		constexpr std::uint32_t kPreNGMaxDFLightDrawStateProofSamples = 8192;

		std::atomic_uint32_t& PreNGDFLightDrawStateProofSamples()
		{
			static std::atomic_uint32_t samples = 0;
			return samples;
		}

		std::atomic_bool& PreNGDFLightDrawStateProofComplete()
		{
			static std::atomic_bool complete = false;
			return complete;
		}

		std::atomic_bool& PreNGDFLightDrawStateProofLimitLogged()
		{
			static std::atomic_bool logged = false;
			return logged;
		}



		bool IsPreNGDFLightDrawStateProofOpen()
		{
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			return ShouldRunPreNGDFLightDrawStateProof() &&
			       budget > 0 &&
			       !PreNGDFLightDrawStateProofComplete().load(std::memory_order_relaxed) &&
			       PreNGDFLightDrawStateProofSamples().load(std::memory_order_relaxed) < budget;
		}

		std::optional<std::uint32_t> TryReservePreNGDFLightDrawStateProofSample()
		{
			if (!IsPreNGDFLightDrawStateProofOpen()) {
				return std::nullopt;
			}
			const auto sample = PreNGDFLightDrawStateProofSamples().fetch_add(1, std::memory_order_relaxed) + 1;
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			if (sample > budget) {
				if (!PreNGDFLightDrawStateProofLimitLogged().exchange(true, std::memory_order_relaxed)) {
					logger::info(
						"[LightLimitFix] PreNG DFLight draw-state proof budget exhausted; holding draw-state bind/audit samples={} budget={} env={}",
						sample - 1,
						budget,
						kPreNGDFLightDrawStateProofBudgetEnv);
				}
				return std::nullopt;
			}
			return sample;
		}

		void MarkPreNGDFLightDrawStateProofComplete(std::uint32_t a_sample)
		{
			if (!PreNGDFLightDrawStateProofComplete().exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight draw-state proof complete; holding draw-state bind/audit after sample={} budget={}",
					a_sample,
					GetPreNGDFLightDrawStateProofBudget());
			}
		}

		void MaybeLogPreNGDFLightDrawStateProofBudgetReached(std::uint32_t a_sample)
		{
			const auto budget = GetPreNGDFLightDrawStateProofBudget();
			if (budget > 0 &&
				a_sample >= budget &&
				!PreNGDFLightDrawStateProofComplete().load(std::memory_order_relaxed) &&
				!PreNGDFLightDrawStateProofLimitLogged().exchange(true, std::memory_order_relaxed)) {
				logger::info(
					"[LightLimitFix] PreNG DFLight draw-state proof budget reached without complete audit; holding draw-state bind/audit samples={} budget={} env={}",
					a_sample,
					budget,
					kPreNGDFLightDrawStateProofBudgetEnv);
			}
		}

		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> dflightDrawStatePixelShaders;
		std::unordered_map<ID3D11DeviceContext*, ShaderCache::ShaderMetadata> dflightDrawStateBoundPixelShaderByContext;
		std::unordered_set<std::string> loggedDFLightDrawStatePixelShaders;
		std::unordered_set<std::string> loggedDFLightDrawStateBindings;
		std::unordered_set<std::string> loggedDFLightDrawStateDraws;

		constexpr std::size_t kMaxDFLightDrawStateLogs = 24;

		bool IsPreNGDFLightVanillaFullShadowedShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return a_metadata.constantBufferSizes[2] == 448 &&
			       a_metadata.constantBufferSizes[12] == 496 &&
			       a_metadata.textureSlots.size() == 5 &&
			       a_metadata.textureSlotMask == 0x2Fu &&
			       HasTextureSlot(a_metadata, 0) &&
			       HasTextureSlot(a_metadata, 1) &&
			       HasTextureSlot(a_metadata, 2) &&
			       HasTextureSlot(a_metadata, 3) &&
			       HasTextureSlot(a_metadata, 5) &&
			       HasTextureDimension(a_metadata, 4, 0) &&
			       HasTextureDimension(a_metadata, 4, 1) &&
			       HasTextureDimension(a_metadata, 4, 2) &&
			       HasTextureDimension(a_metadata, 4, 3) &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[0] == 1 &&
			       a_metadata.textureSampleCounts[1] == 1 &&
			       a_metadata.textureSampleCounts[2] == 1 &&
			       a_metadata.textureSampleCounts[3] == 1 &&
			       a_metadata.textureSampleCounts[5] == 6 &&
			       a_metadata.inputTextureCount == 5 &&
			       a_metadata.inputCount == 1 &&
			       a_metadata.inputMask == 0x1 &&
			       a_metadata.outputCount == 2 &&
			       a_metadata.outputMask == 0x3 &&
			       a_metadata.sampleInstructionCount == 10 &&
			       a_metadata.hasImmediateConstantBuffer &&
			       a_metadata.immediateConstantBufferRows == 1000 &&
			       !a_metadata.hasDiscard;
		}

		bool IsPreNGDFLightLLFConsumerCandidateShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return a_metadata.constantBufferSizes[2] == 448 &&
			       a_metadata.constantBufferSizes[3] > 0 &&
			       a_metadata.constantBufferSizes[12] == 496 &&
			       a_metadata.textureSlots.size() >= 8 &&
			       HasTextureSlot(a_metadata, 0) &&
			       HasTextureSlot(a_metadata, 1) &&
			       HasTextureSlot(a_metadata, 2) &&
			       HasTextureSlot(a_metadata, 3) &&
			       HasTextureSlot(a_metadata, 5) &&
			       HasTextureSlot(a_metadata, 35) &&
			       HasTextureSlot(a_metadata, 36) &&
			       HasTextureSlot(a_metadata, 37) &&
			       HasTextureDimension(a_metadata, 4, 0) &&
			       HasTextureDimension(a_metadata, 4, 1) &&
			       HasTextureDimension(a_metadata, 4, 2) &&
			       HasTextureDimension(a_metadata, 4, 3) &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[0] == 1 &&
			       a_metadata.textureSampleCounts[1] == 1 &&
			       a_metadata.textureSampleCounts[2] == 1 &&
			       a_metadata.textureSampleCounts[3] == 1 &&
			       a_metadata.textureSampleCounts[5] == 6 &&
			       a_metadata.textureSampleCounts[35] > 0 &&
			       a_metadata.textureSampleCounts[36] > 0 &&
			       a_metadata.textureSampleCounts[37] > 0 &&
			       a_metadata.inputTextureCount >= 8 &&
			       a_metadata.inputCount == 1 &&
			       a_metadata.inputMask == 0x1 &&
			       a_metadata.outputCount == 2 &&
			       a_metadata.outputMask == 0x3 &&
			       a_metadata.sampleInstructionCount >= 10 &&
			       !a_metadata.hasDiscard;
		}

		std::optional<ShaderCache::ShaderMetadata> GetTrackedPreNGDFLightDrawStatePixelShader(ID3D11PixelShader* a_pixelShader)
		{
			if (!a_pixelShader) {
				return std::nullopt;
			}

			std::scoped_lock lock(llfCandidateLock);
			if (auto it = dflightDrawStatePixelShaders.find(a_pixelShader); it != dflightDrawStatePixelShaders.end()) {
				return it->second;
			}

			return std::nullopt;
		}
	}

	bool ShouldTracePreNGDFLightDrawState()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightDrawStateStrictCB()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateStrictCBBindEnv);
		return enabled;
	}

	bool ShouldBindPreNGDFLightDrawStateClusterSRVs()
	{
		static const bool enabled = ReadPreNGEnvironmentSwitch(kPreNGDFLightDrawStateClusterSRVBindEnv);
		return enabled;
	}

	std::uint32_t GetPreNGDFLightDrawStateProofBudget()
	{
		static const std::uint32_t budget = [] {
			const auto configured = ReadPreNGEnvironmentUInt(kPreNGDFLightDrawStateProofBudgetEnv);
			if (!configured) {
				return kPreNGDefaultDFLightDrawStateProofSamples;
			}
			if (*configured > kPreNGMaxDFLightDrawStateProofSamples) {
				logger::warn(
					"[LightLimitFix] PreNG DFLight draw-state proof budget clamped env={} requested={} max={}",
					kPreNGDFLightDrawStateProofBudgetEnv,
					*configured,
					kPreNGMaxDFLightDrawStateProofSamples);
				return kPreNGMaxDFLightDrawStateProofSamples;
			}
			return *configured;
		}();
		return budget;
	}

	bool ShouldRunPreNGDFLightDrawStateProof()
	{
		return ShouldTracePreNGDFLightDrawState() ||
		       ShouldBindPreNGDFLightDrawStateStrictCB() ||
		       ShouldBindPreNGDFLightDrawStateClusterSRVs();
	}

	bool ShouldTrackPreNGDFLightDrawTargets()
	{
		return IsPreNGDFLightDrawStateProofOpen() ||
		       ShouldRunPreNGDFLightZeroAdditivePass() ||
		       ShouldRunPreNGDFLightResourceNoOpPass() ||
		       ShouldRunPreNGDFLightFullContractNoOpPass() ||
		       ShouldRunPreNGDFLightLLFAdditivePass();
	}

	bool IsPreNGDFLightDrawStateTarget(const ShaderCache::ShaderMetadata& a_metadata)
	{
		const bool vanillaFullShadowedHash =
			a_metadata.asmHash == kPreNGDFLightVanillaFullShadowed920AsmHash ||
			a_metadata.asmHash == kPreNGDFLightVanillaFullShadowed922AsmHash;

		return (vanillaFullShadowedHash && IsPreNGDFLightVanillaFullShadowedShape(a_metadata)) ||
		       IsPreNGDFLightLLFConsumerCandidateShape(a_metadata);
	}

	void TrackPreNGDFLightDrawStatePixelShader(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
	{
		if (!a_pixelShader || !ShouldTrackPreNGDFLightDrawTargets() || !IsPreNGDFLightDrawStateTarget(a_metadata)) {
			return;
		}

		const auto key = std::format("{}:{:08X}:{:X}", a_metadata.uid, a_metadata.asmHash, ToAddress(a_pixelShader));
		bool shouldLog = false;
		std::size_t trackedCount = 0;
		{
			std::scoped_lock lock(llfCandidateLock);
			dflightDrawStatePixelShaders[a_pixelShader] = a_metadata;
			if (loggedDFLightDrawStatePixelShaders.size() < kMaxDFLightDrawStateLogs) {
				shouldLog = loggedDFLightDrawStatePixelShaders.insert(key).second;
			}
			trackedCount = dflightDrawStatePixelShaders.size();
		}

		if (!shouldLog) {
			return;
		}

		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state target PS observed asmHash=0x{:08X} hash=0x{:08X} uid={} device=0x{:X} shader=0x{:X} tracked={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} immediateRows={}",
			a_metadata.asmHash,
			a_metadata.hash,
			a_metadata.uid,
			ToAddress(a_device),
			ToAddress(a_pixelShader),
			trackedCount,
			FormatBufferSlots(a_metadata),
			FormatTextureSlots(a_metadata),
			FormatTextureDimensions(a_metadata),
			a_metadata.instructionCount,
			a_metadata.sampleInstructionCount,
			FormatTextureSampleCounts(a_metadata),
			a_metadata.immediateConstantBufferRows);
	}

	std::optional<ShaderCache::ShaderMetadata> GetBoundPreNGDFLightDrawStatePixelShader(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !ShouldTrackPreNGDFLightDrawTargets()) {
			return std::nullopt;
		}

		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = dflightDrawStateBoundPixelShaderByContext.find(a_context); it != dflightDrawStateBoundPixelShaderByContext.end()) {
				return it->second;
			}
		}

		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
		if (!pixelShader) {
			return std::nullopt;
		}

		auto metadata = GetTrackedPreNGDFLightDrawStatePixelShader(pixelShader.get());
		if (metadata) {
			TrackPreNGDFLightDrawStateBoundPixelShader(a_context, pixelShader.get());
		}
		return metadata;
	}

	void TrackPreNGDFLightDrawStateBoundPixelShader(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
	{
		if (!a_context || !ShouldTrackPreNGDFLightDrawTargets()) {
			return;
		}

		const auto metadata = GetTrackedPreNGDFLightDrawStatePixelShader(a_pixelShader);
		bool shouldLog = false;
		std::size_t boundContextCount = 0;
		if (metadata) {
			const auto key = std::format("{}:{:08X}:{:X}:{:X}", metadata->uid, metadata->asmHash, ToAddress(a_context), ToAddress(a_pixelShader));
			std::scoped_lock lock(llfCandidateLock);
			dflightDrawStateBoundPixelShaderByContext[a_context] = *metadata;
			if (loggedDFLightDrawStateBindings.size() < kMaxDFLightDrawStateLogs) {
				shouldLog = loggedDFLightDrawStateBindings.insert(key).second;
			}
			boundContextCount = dflightDrawStateBoundPixelShaderByContext.size();
		} else {
			std::scoped_lock lock(llfCandidateLock);
			dflightDrawStateBoundPixelShaderByContext.erase(a_context);
		}

		if (!metadata || !shouldLog) {
			return;
		}

		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state target PS bound asmHash=0x{:08X} hash=0x{:08X} uid={} context=0x{:X} vtable=0x{:X} shader=0x{:X} boundContexts={} buffers={} textures={} textureDims={} textureSamples={} immediateRows={}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			ToAddress(a_pixelShader),
			boundContextCount,
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			FormatTextureSampleCounts(*metadata),
			metadata->immediateConstantBufferRows);
	}

	void TracePreNGDFLightDrawStateContext(ID3D11DeviceContext* a_context, const char* a_drawKind, std::string_view a_drawCounts)
	{
		if (!a_context || !ShouldRunPreNGDFLightDrawStateProof() || !IsPreNGDFLightDrawStateProofOpen()) {
			return;
		}

		const auto metadata = GetBoundPreNGDFLightDrawStatePixelShader(a_context);
		if (!metadata) {
			return;
		}

		const auto proofSample = TryReservePreNGDFLightDrawStateProofSample();
		if (!proofSample) {
			return;
		}

		if (globals::features::lightLimitFix.loaded) {
			const auto strictState = globals::features::lightLimitFix.BindPreNGDFLightDrawStateStrictLightCB(a_context);
			globals::features::lightLimitFix.BindPreNGDFLightDrawStateClusterSRVs(a_context, strictState.strictCBBound);
		}

		bool bindingComplete = false;
		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
		const auto pixelShaderAddress = ToAddress(pixelShader.get());
		if (globals::features::lightLimitFix.loaded) {
			bindingComplete = globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"dflight-draw-state",
				4,
				0,
				0,
				pixelShaderAddress != 0,
				pixelShaderAddress,
				a_context);
		}
		if (bindingComplete) {
			MarkPreNGDFLightDrawStateProofComplete(*proofSample);
			return;
		}
		MaybeLogPreNGDFLightDrawStateProofBudgetReached(*proofSample);

		if (!ShouldTracePreNGDFLightDrawState()) {
			return;
		}

		D3D11_PRIMITIVE_TOPOLOGY topology{};
		a_context->IAGetPrimitiveTopology(&topology);

		D3D11_VIEWPORT viewport{};
		UINT viewportCount = 1;
		a_context->RSGetViewports(&viewportCount, &viewport);
		const auto viewportDescription = FormatViewport(viewport, viewportCount);

		ID3D11RenderTargetView* renderTargets[2]{};
		a_context->OMGetRenderTargets(2, renderTargets, nullptr);
		const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
		const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
		for (auto* renderTarget : renderTargets) {
			if (renderTarget) {
				renderTarget->Release();
			}
		}

		const auto rt0Description = FormatRenderTargetInfo(rt0);
		const auto rt1Description = FormatRenderTargetInfo(rt1);
		const auto counts = std::string{ a_drawCounts };
		const auto key = std::format(
			"{:08X}:{}:{}:{}:{}:{}:{}",
			metadata->asmHash,
			metadata->uid,
			a_drawKind,
			counts,
			static_cast<std::uint32_t>(topology),
			viewportDescription,
			rt0Description + ":" + rt1Description);

		bool shouldLog = false;
		{
			std::scoped_lock lock(llfCandidateLock);
			if (loggedDFLightDrawStateDraws.size() < kMaxDFLightDrawStateLogs) {
				shouldLog = loggedDFLightDrawStateDraws.insert(key).second;
			}
		}

		if (!shouldLog) {
			return;
		}

		const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
		const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
		logger::info(
			"[LightLimitFix] PreNG DFLight draw-state draw asmHash=0x{:08X} hash=0x{:08X} uid={} draw={} counts={} context=0x{:X} vtable=0x{:X} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} immediateRows={} boundCBs={} boundSRVs={}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			a_drawKind,
			counts,
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			static_cast<std::uint32_t>(topology),
			viewportDescription,
			rt0Description,
			rt1Description,
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			metadata->instructionCount,
			metadata->sampleInstructionCount,
			FormatTextureSampleCounts(*metadata),
			metadata->immediateConstantBufferRows,
			boundConstantBuffers,
			boundShaderResources);
	}
#endif
}
