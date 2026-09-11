#include "Core/ShaderHooks/PreNGBSLightingBind.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
#include "Core/ShaderHooks/PreNGLookupDiagnostics.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"
#include "Core/ShaderHooks/PreNGSwitches.h"
#include "Features/LightLimitFix.h"
#include "Render/RuntimeAdapter.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <winrt/base.h>

#include <RE/FO4Runtime.h>

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSShader.h"
#include "RE/Bethesda/BSGraphics.h"
#endif

namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)

	namespace
	{
		struct PreNGBSLightingVanillaBindAlias
		{
			std::uint32_t pixelDescriptor = 0;
			std::uintptr_t vanillaPixelD3D = 0;
			RE::BSGraphics::PixelShader entry{};
			winrt::com_ptr<ID3D11PixelShader> shader;
		};
	}

	namespace
	{
		std::mutex s_preNGBSLightingVanillaBindLock;
		std::vector<std::unique_ptr<PreNGBSLightingVanillaBindAlias>> s_preNGBSLightingVanillaBindAliases;
		std::atomic_uint32_t s_preNGBSLightingVanillaBindAttempts = 0;
		std::atomic_uint32_t s_preNGBSLightingVanillaBoundCount = 0;
		std::atomic_bool s_preNGBSLightingVanillaBindProofCompleteLogged = false;
		std::atomic_uint64_t s_preNGBSLightingVanillaPendingProbeNextFrame = 0;
		std::atomic_uint32_t s_preNGBSLightingLLFConsumerBindAttempts = 0;
		std::atomic_uint32_t s_preNGBSLightingLLFConsumerBoundCount = 0;
		std::atomic_bool s_preNGBSLightingLLFConsumerBindProofCompleteLogged = false;
		// Tracks the last frame the consumer rebound b3/t35-t37. The clustered
		// resources are LLF-private high registers that the engine's other draws do
		// not touch, so (Skyrim-CS parity) they only need binding once per frame, not
		// per BSLighting draw. Per-draw rebinding was the 90-light ~10 FPS cost.
		std::atomic_uint64_t s_preNGBSLightingLLFConsumerResourceBoundFrame = UINT64_MAX;
	}

	namespace
	{
		bool TryReservePreNGBSLightingVanillaPendingProbeFrame()
		{
			auto* runtime = Runtime::GetSingleton();
			const auto frame = runtime ? runtime->GetFrameCount() : 0;
			auto nextFrame = s_preNGBSLightingVanillaPendingProbeNextFrame.load(std::memory_order_relaxed);
			while (frame >= nextFrame) {
				if (s_preNGBSLightingVanillaPendingProbeNextFrame.compare_exchange_weak(
						nextFrame,
						frame + 1,
						std::memory_order_relaxed,
						std::memory_order_relaxed)) {
					return true;
				}
			}
			return false;
		}
	}

	namespace
	{
		RE::BSGraphics::PixelShader* GetPreNGBSLightingVanillaBindAlias(
			std::uint32_t a_pixelDescriptor,
			std::uintptr_t a_vanillaPixelD3D)
		{
			if (a_vanillaPixelD3D == 0) {
				return nullptr;
			}

			std::scoped_lock lock(s_preNGBSLightingVanillaBindLock);
			for (auto& alias : s_preNGBSLightingVanillaBindAliases) {
				if (alias &&
					alias->pixelDescriptor == a_pixelDescriptor &&
					alias->vanillaPixelD3D == a_vanillaPixelD3D) {
					return std::addressof(alias->entry);
				}
			}

			auto alias = std::make_unique<PreNGBSLightingVanillaBindAlias>();
			alias->pixelDescriptor = a_pixelDescriptor;
			alias->vanillaPixelD3D = a_vanillaPixelD3D;
			alias->shader.copy_from(reinterpret_cast<ID3D11PixelShader*>(a_vanillaPixelD3D));
			alias->entry.id = a_pixelDescriptor;
			alias->entry.shader = alias->shader.get();

			auto* entry = std::addressof(alias->entry);
			s_preNGBSLightingVanillaBindAliases.push_back(std::move(alias));

			logger::info(
				"[BSShaderHooks] PreNG BSLighting vanilla-equivalent PS alias created aliases={} psDesc=0x{:X} vanillaPSD3D=0x{:X} aliasEntry=0x{:X}; shader bytecode/output are unchanged",
				s_preNGBSLightingVanillaBindAliases.size(),
				a_pixelDescriptor,
				a_vanillaPixelD3D,
				reinterpret_cast<std::uintptr_t>(entry));

			return entry;
		}
	}

	bool TryBindPreNGBSLightingVanillaPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		auto* llfFeature = globals::features::lightLimitFix.loaded ?
			std::addressof(globals::features::lightLimitFix) :
			nullptr;
		if ((!llfFeature || !llfFeature->HasPreNGBSLightingDescriptorConsumerData()) &&
			!TryReservePreNGBSLightingVanillaPendingProbeFrame()) {
			return false;
		}

		const auto attempt = ++s_preNGBSLightingVanillaBindAttempts;
		const bool shouldLog = attempt <= 8 || IsPreNGPowerOfTwo(attempt);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		LightLimitFix::PreNGDFLightResourceBindingState resourceState{};
		auto logBind = [&](const char* a_state, const char* a_reason, std::uintptr_t a_vertexEntry, std::uintptr_t a_pixelEntry, std::uintptr_t a_vertexD3D, std::uintptr_t a_vanillaPixelD3D, std::uint32_t a_bindIndex) {
			const bool forceLog = std::strcmp(a_state, "bound") == 0;
			if (!shouldLog && !forceLog) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG BSLighting vanilla-equivalent descriptor bind attempts={} binds={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} vsEntry=0x{:X} psAliasEntry=0x{:X} vsD3D=0x{:X} vanillaPSD3D=0x{:X} vanillaFound={} customBind={} reason={} resources(strictCB={},clusterSRVs={},lights={},strict={},shadowMask=0x{:08X})",
				attempt,
				a_bindIndex,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_vertexEntry,
				a_pixelEntry,
				a_vertexD3D,
				a_vanillaPixelD3D,
				a_found,
				a_state,
				a_reason,
				resourceState.strictCBBound,
				resourceState.clusterSRVsBound,
				resourceState.lightCount,
				resourceState.strictLightCount,
				resourceState.shadowBitMask);
		};

		if (!a_found) {
			logBind("failed", "vanilla-lookup-miss", 0, 0, 0, 0, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
		if (vertexEntry == 0 || vertexD3D == 0) {
			logBind("failed", "current-vs-entry-unavailable", vertexEntry, 0, vertexD3D, vanillaPixelD3D, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}
		if (vanillaPixelEntry == 0 || vanillaPixelD3D == 0) {
			logBind("failed", "current-vanilla-ps-unavailable", vertexEntry, vanillaPixelEntry, vertexD3D, vanillaPixelD3D, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		if (llfFeature) {
			llfFeature->NotifyPreNGBSLightingLLFConsumerDescriptorObserved(
				static_cast<std::uint32_t>(a_vertexDescriptor),
				pixelDescriptor,
				a_found,
				vanillaPixelD3D);
		}
		if (!llfFeature || !llfFeature->HasPreNGBSLightingDescriptorConsumerData()) {
			logBind(
				"held",
				"clustered-payload-pending",
				vertexEntry,
				0,
				vertexD3D,
				vanillaPixelD3D,
				s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		auto* pixelShader = GetPreNGBSLightingVanillaBindAlias(pixelDescriptor, vanillaPixelD3D);
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(pixelShader);
		if (!pixelShader || !pixelShader->shader || pixelEntry == 0) {
			logBind("failed", "vanilla-ps-alias-unavailable", vertexEntry, pixelEntry, vertexD3D, vanillaPixelD3D, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			logBind("failed", "bind-helper-or-pixel-global-unavailable", vertexEntry, pixelEntry, vertexD3D, vanillaPixelD3D, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			logBind("failed", "pixel-global-write-failed", vertexEntry, pixelEntry, vertexD3D, vanillaPixelD3D, s_preNGBSLightingVanillaBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);

		const auto bindIndex = s_preNGBSLightingVanillaBoundCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (llfFeature) {
			resourceState = llfFeature->BindPreNGBSLightingDescriptorResourcesToPixelShader();
			llfFeature->TracePreNGActiveLightingBindings(
				"descriptor-bslighting-vanilla-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				pixelDescriptor,
				a_found,
				vanillaPixelD3D);
		}

		logBind("bound", "vanilla-pixel-alias-current-vs-bound", vertexEntry, pixelEntry, vertexD3D, vanillaPixelD3D, bindIndex);

		if (resourceState.strictCBBound && resourceState.clusterSRVsBound &&
			!s_preNGBSLightingVanillaBindProofCompleteLogged.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[BSShaderHooks] PreNG BSLighting vanilla-equivalent bind proof reached current-vs/vanilla-ps plus b3/t35-t37 completion; future proof binds are held until the BSLighting shader-side LLF consumer is implemented");
		}

		return resourceState.strictCBBound && resourceState.clusterSRVsBound;
	}

	// Phase 3 visible consumer bind. Mirrors TryBindPreNGBSLightingVanillaPixelShader
	// but binds the ShaderCache-compiled BSLightingLLFConsumerPS instead of a
	// vanilla PS alias. Gated by FO4CS_LLF_PRENG_BSLIGHTING_LLF_BIND. The
	// consumer declares the vanilla resource shape plus b3/t35-t37, so the
	// existing strict-CB + cluster-SRV bind path serves it unchanged.
	bool TryBindPreNGBSLightingLLFConsumerPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		auto* llfFeature = globals::features::lightLimitFix.loaded ?
			std::addressof(globals::features::lightLimitFix) :
			nullptr;

		const auto attempt = ++s_preNGBSLightingLLFConsumerBindAttempts;
		const bool shouldLog = attempt <= 8 || IsPreNGPowerOfTwo(attempt);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		LightLimitFix::PreNGDFLightResourceBindingState resourceState{};
		auto logBind = [&](const char* a_state, const char* a_reason, std::uintptr_t a_vertexEntry, std::uintptr_t a_pixelEntry, std::uintptr_t a_consumerPSD3D, std::uint32_t a_bindIndex, bool a_consumerComplete) {
			const bool forceLog = std::strcmp(a_state, "bound") == 0;
			if (!shouldLog && !forceLog) {
				return;
			}

			logger::info(
				"[BSShaderHooks] BSLighting LLF consumer bound attempts={} binds={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} descriptor=0x{:X} vsEntry=0x{:X} psEntry=0x{:X} consumerPSD3D=0x{:X} vanillaFound={} state={} reason={} resources(strictCB={},clusterSRVs={},lights={},strict={},shadowMask=0x{:08X}) llfConsumerComplete={}",
				attempt,
				a_bindIndex,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_vertexEntry,
				a_pixelEntry,
				a_consumerPSD3D,
				a_found,
				a_state,
				a_reason,
				resourceState.strictCBBound,
				resourceState.clusterSRVsBound,
				resourceState.lightCount,
				resourceState.strictLightCount,
				resourceState.shadowBitMask,
				a_consumerComplete);
		};

		const auto bound = s_preNGBSLightingLLFConsumerBoundCount.load(std::memory_order_relaxed);
		if (!a_found) {
			logBind("failed", "vanilla-lookup-miss", 0, 0, 0, bound, false);
			return false;
		}
		if (!a_shader) {
			logBind("failed", "null-shader", 0, 0, 0, bound, false);
			return false;
		}
		if (!llfFeature || !llfFeature->HasPreNGBSLightingDescriptorConsumerData()) {
			logBind("held", "clustered-payload-pending", 0, 0, 0, bound, false);
			return false;
		}
		if (llfFeature->ShouldSuppressPreNGBSLightingVisibleConsumerForMenu()) {
			logBind("held", "preview-menu-vanilla-preserved", 0, 0, 0, bound, false);
			return false;
		}

		// Compile/fetch the LLF consumer PS for this descriptor. ShaderCache owns
		// the lifetime; we bind its D3D object directly (no vanilla-style alias).
		auto* consumerShader = ShaderCache::GetSingleton()->GetPixelShader(
			*a_shader,
			pixelDescriptor);
		const auto consumerPSD3D = consumerShader ?
			reinterpret_cast<std::uintptr_t>(consumerShader->shader) :
			0;
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(consumerShader);
		if (!consumerShader || consumerPSD3D == 0 || pixelEntry == 0) {
			logBind("failed", "consumer-ps-unavailable", 0, pixelEntry, consumerPSD3D, bound, false);
			return false;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		if (vertexEntry == 0 || vertexD3D == 0) {
			logBind("failed", "current-vs-entry-unavailable", vertexEntry, pixelEntry, consumerPSD3D, bound, false);
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			logBind("failed", "bind-helper-or-pixel-global-unavailable", vertexEntry, pixelEntry, consumerPSD3D, bound, false);
			return false;
		}

		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			logBind("failed", "pixel-global-write-failed", vertexEntry, pixelEntry, consumerPSD3D, bound, false);
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);

		const auto bindIndex = s_preNGBSLightingLLFConsumerBoundCount.fetch_add(1, std::memory_order_relaxed) + 1;

		// Bind b3/t35-t37 once per frame (Skyrim-CS parity), not per draw. These
		// are LLF-private registers; binding them on the first eligible BSLighting
		// draw of the frame leaves them valid for the rest of the frame's draws
		// because the unified bind helper only swaps shader objects (not resource
		// slots) and the engine's other draws do not write these high registers.
		auto* runtime = Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		const auto lastResourceFrame =
			s_preNGBSLightingLLFConsumerResourceBoundFrame.load(std::memory_order_relaxed);
		bool consumerComplete = false;
		if (lastResourceFrame != frame) {
			resourceState = llfFeature->BindPreNGBSLightingDescriptorResourcesToPixelShader();
			// FO4 forward clusters-only: the b3 strict-light buffer was removed, so
			// cluster SRV completion (t35-t37 bound with currentLightCount > 0) is
			// the only completion signal for the visible consumer.
			consumerComplete = resourceState.clusterSRVsBound;
			if (consumerComplete) {
				s_preNGBSLightingLLFConsumerResourceBoundFrame.store(frame, std::memory_order_relaxed);
			}
			llfFeature->TracePreNGActiveLightingBindings(
				"descriptor-bslighting-llf-bind",
				static_cast<std::int32_t>(a_shader->shaderType),
				static_cast<std::uint32_t>(a_vertexDescriptor),
				pixelDescriptor,
				a_found,
				consumerPSD3D);
		} else {
			// Resources already bound this frame; the PS swap above is all this
			// draw needs. Treat as complete so the detour still returns 1.
			consumerComplete = true;
		}

		logBind("bound", "llf-consumer-current-vs-bound", vertexEntry, pixelEntry, consumerPSD3D, bindIndex, consumerComplete);
		if (consumerComplete) {
			llfFeature->NotifyPreNGBSLightingVisibleConsumerResumeComplete();
		}

		if (consumerComplete &&
			!s_preNGBSLightingLLFConsumerBindProofCompleteLogged.exchange(true, std::memory_order_relaxed)) {
			logger::info(
				"[BSShaderHooks] PreNG BSLighting LLF consumer bind proof reached current-vs/consumer-ps plus b3/t35-t37 completion llfConsumerComplete=true");
		}

		return consumerComplete;
	}

#endif
}
