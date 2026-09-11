#include "Core/ShaderHooks/PreNGDescriptorBind.h"

#include "Core/CommunityShaders.h"
#include "Core/Deferred.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderHooks/PreNGDescriptorDiagnostics.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
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

	void LogPreNGDFLightFullContractDescriptorBindHeld(
		const RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		static std::atomic_uint32_t heldCount = 0;
		const auto heldIndex = ++heldCount;
		if (heldIndex > 8 && heldIndex % 8192 != 0) {
			return;
		}

		const auto shaderType = a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1;
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string{ "<null>" };
		logger::info(
			"[BSShaderHooks] PreNG DFLight full-contract descriptor bind held hits={} shaderType={} fxp={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} env={} reason=owned-full-contract-shader-not-vanilla-equivalent",
			heldIndex,
			shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			static_cast<std::uint32_t>(a_pixelDescriptor),
			a_found,
			kPreNGDFLightFullContractDescriptorBindEnv);
	}

	namespace
	{
		struct PreNGDFLightFullShadowedCandidateState
		{
			bool attempted = false;
			RE::BSGraphics::PixelShader entry{};
			winrt::com_ptr<ID3D11PixelShader> shader;
			std::vector<std::byte> bytecode;
		};

		PreNGDFLightFullShadowedCandidateState s_preNGDFLightFullShadowedCandidate920;
		PreNGDFLightFullShadowedCandidateState s_preNGDFLightFullShadowedCandidate922;
		std::mutex s_preNGDFLightFullShadowedCandidateLock;
		std::atomic_uint32_t s_preNGDFLightFullShadowedBindAttempts = 0;
		std::atomic_uint32_t s_preNGDFLightFullShadowedBoundCount = 0;
		std::atomic_bool s_preNGDFLightFullShadowedBindLimitLogged = false;

		PreNGDFLightFullShadowedCandidateState& GetPreNGDFLightFullShadowedCandidateState(std::uint32_t a_descriptor)
		{
			return a_descriptor == kPreNGDFLightFullShadowedPixelDesc920 ?
				s_preNGDFLightFullShadowedCandidate920 :
				s_preNGDFLightFullShadowedCandidate922;
		}
	}

	bool TryBindPreNGDFCompositeDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
		auto* currentVertexShader = reinterpret_cast<RE::BSGraphics::VertexShader*>(vertexEntry);

		if (vertexEntry == 0 || vertexD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-current-vs-entry-unavailable");
			return false;
		}
		const auto pixelD3D = reinterpret_cast<std::uintptr_t>(a_pixelShader ? a_pixelShader->shader : nullptr);
		if (!a_pixelShader || pixelD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-owned-ps-unavailable");
			return false;
		}
		if (globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				a_found,
				vanillaPixelD3D,
				pixelD3D);
		}
		if (!globals::features::lightLimitFix.loaded ||
			!globals::features::lightLimitFix.HasPreNGDFCompositeDescriptorConsumerData()) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"held",
				"dfcomposite-consumer-data-unavailable");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-bind-helper-or-pixel-global-unavailable");
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "dfcomposite-pixel-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-dfcomposite-pixel-current-vs-bound");

		if (globals::features::lightLimitFix.loaded) {
			const bool visibleDescriptorResources =
				ShouldBindPreNGDFCompositeVisibleDescriptorResources(static_cast<std::uint32_t>(a_pixelDescriptor));
			if (ShouldBindPreNGDFCompositeDescriptorResources() || visibleDescriptorResources) {
				if (ShouldPersistPreNGClusterPrepass() && !visibleDescriptorResources) {
					static std::atomic_bool loggedPersistentResourceHold = false;
					if (!loggedPersistentResourceHold.exchange(true, std::memory_order_relaxed)) {
						logger::warn(
							"[BSShaderHooks] PreNG DFComposite descriptor resources held while {}=1 outside the visible-safe 0x88/0x10088 consumer; persistent snapshot keeps b3/t35-t37 live across DFComposite passes and can tint/desaturate non-visible consumers.",
							kPreNGPersistentClusterPrepassEnv);
					}
				} else {
					globals::features::lightLimitFix.BindPreNGDFCompositeDescriptorResourcesToPixelShader();
				}
			}
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"descriptor-dfcomposite-safe-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				true,
				pixelD3D);
		}
		return true;
	}

	namespace
	{
		RE::BSGraphics::PixelShader* GetPreNGDFLightFullShadowedCandidatePixelShader(std::uint32_t a_descriptor)
		{
			std::scoped_lock lock(s_preNGDFLightFullShadowedCandidateLock);
			auto& state = GetPreNGDFLightFullShadowedCandidateState(a_descriptor);
			if (state.shader && state.entry.shader) {
				return std::addressof(state.entry);
			}
			if (state.attempted) {
				return nullptr;
			}
			state.attempted = true;

			auto* device = Runtime::GetSingleton()->GetDevice();
			if (!device) {
				logger::warn(
					"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} reason=device-unavailable",
					a_descriptor,
					kPreNGDFLightFullShadowedCandidateSource);
				return nullptr;
			}

			std::vector<std::pair<std::string, std::string>> defineStorage;
			for (auto* feature : Feature::GetFeatureList()) {
				if (!feature || !feature->loaded) {
					continue;
				}
				auto name = feature->GetShaderDefineName();
				if (!name.empty()) {
					defineStorage.emplace_back(std::move(name), "1");
				}
			}
			defineStorage.emplace_back("FO4CS_DFLIGHT_FULL_SHADOWED_CANDIDATE", "1");

			std::vector<D3D_SHADER_MACRO> defines;
			defines.reserve(defineStorage.size() + 1);
			for (auto& [name, value] : defineStorage) {
				defines.push_back({ name.c_str(), value.c_str() });
			}
			defines.push_back({});

			auto bytecode = ShaderCompiler::GetSingleton()->CompileFromFile(
				kPreNGDFLightFullShadowedCandidateSource,
				"ps_5_0",
				defines.data(),
				"main");
			if (!bytecode) {
				logger::warn(
					"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} reason=compile-failed",
					a_descriptor,
					kPreNGDFLightFullShadowedCandidateSource);
				return nullptr;
			}

			ID3D11PixelShader* shader = nullptr;
			const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
			if (FAILED(hr) || !shader) {
				logger::warn(
					"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS create failed descriptor=0x{:X} source={} bytecode={} hr=0x{:08X} reason=CreatePixelShader-failed",
					a_descriptor,
					kPreNGDFLightFullShadowedCandidateSource,
					bytecode->size(),
					static_cast<std::uint32_t>(hr));
				return nullptr;
			}

			state.shader.attach(shader);
			state.bytecode = std::move(*bytecode);
			state.entry.id = a_descriptor;
			state.entry.shader = state.shader.get();

			auto* shaderCache = ShaderCache::GetSingleton();
			const auto metadata = shaderCache->GetMetadataForBytecode(
				ShaderStage::Pixel,
				state.bytecode.data(),
				state.bytecode.size());
			if (metadata) {
				shaderCache->ObserveD3DShaderObject(ShaderStage::Pixel, reinterpret_cast<std::uintptr_t>(state.shader.get()), *metadata);
			}

			logger::info(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate PS created descriptor=0x{:X} source={} bytecode={} psD3D=0x{:X} metadata={} asm=0x{:08X} hash=0x{:08X}; replacement=narrow bind=held",
				a_descriptor,
				kPreNGDFLightFullShadowedCandidateSource,
				state.bytecode.size(),
				reinterpret_cast<std::uintptr_t>(state.shader.get()),
				metadata ? metadata->uid : "<none>",
				metadata ? metadata->asmHash : 0,
				metadata ? metadata->hash : 0);

			return std::addressof(state.entry);
		}
	}

	bool TryBindPreNGDFLightFullShadowedCandidate(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFLightFullShadowedBindAttempts;
		const auto bindBudget = GetPreNGDFLightFullShadowedCandidateBindBudget();
		const bool shouldLog = attempt <= kPreNGMaxDFLightFullShadowedCandidateBindLogs || IsPreNGPowerOfTwo(attempt);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logBind = [&](const char* a_state, const char* a_reason, RE::BSGraphics::PixelShader* a_pixelShader, std::uintptr_t a_pixelD3D, std::uint32_t a_bindIndex) {
			const bool forceLog = std::strcmp(a_reason, "proof-bind-limit-reached") == 0;
			if (!shouldLog && !forceLog) {
				return;
			}
			logger::info(
				"[BSShaderHooks] PreNG DFLight full-shadowed candidate bind attempts={} binds={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} psEntry=0x{:X} psD3D=0x{:X} lookupResult={} customBind={} reason={} maxProofBinds={}",
				attempt,
				a_bindIndex,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				reinterpret_cast<std::uintptr_t>(a_pixelShader),
				a_pixelD3D,
				a_lookupResult,
				a_state,
				a_reason,
				bindBudget);
		};

		if (a_lookupResult == 0) {
			logBind("failed", "vanilla-lookup-miss", nullptr, 0, s_preNGDFLightFullShadowedBoundCount.load(std::memory_order_relaxed));
			return false;
		}

		const auto bindIndex = s_preNGDFLightFullShadowedBoundCount.fetch_add(1, std::memory_order_relaxed) + 1;
		if (bindIndex > bindBudget) {
			if (!s_preNGDFLightFullShadowedBindLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logBind("held", "proof-bind-limit-reached", nullptr, 0, bindIndex - 1);
			}
			return false;
		}

		auto* pixelShader = GetPreNGDFLightFullShadowedCandidatePixelShader(pixelDescriptor);
		const auto pixelD3D = pixelShader && pixelShader->shader ? reinterpret_cast<std::uintptr_t>(pixelShader->shader) : 0;
		if (!pixelShader || !pixelD3D) {
			logBind("failed", "candidate-ps-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		if (vertexEntry == 0) {
			logBind("failed", "current-vs-entry-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			logBind("failed", "bind-helper-or-pixel-global-unavailable", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			logBind("failed", "pixel-global-write-failed", pixelShader, pixelD3D, bindIndex);
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		logBind("bound", "full-shadowed-candidate-bound", pixelShader, pixelD3D, bindIndex);

		if (shouldLog && globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"dflight-full-shadowed-candidate-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				pixelDescriptor,
				true,
				pixelD3D);
		}

		return true;
	}

	bool TryBindPreNGDescriptorShaders(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());

		if (!ShouldBindPreNGDescriptorShaders()) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				a_vertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_BIND-off");
			return false;
		}

		if (!a_vertexShader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-vs");
			return false;
		}
		if (!a_pixelShader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-ps");
			return false;
		}
		if (!a_vertexShader->shader || !a_pixelShader->shader) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-d3d-object");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		if (!IsReadableMemory(bindAddr, 16)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "bind-helper-unreadable");
			return false;
		}

		const auto vertexGlobal = F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsWritableMemory(vertexGlobal, sizeof(std::uintptr_t)) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "shader-global-unwritable");
			return false;
		}

		const auto vertexEntry = reinterpret_cast<std::uintptr_t>(a_vertexShader);
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(vertexGlobal, vertexEntry) || !WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "shader-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, a_vertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-entry-bound");
		return true;
	}

	bool TryBindPreNGDFLightDescriptorPixelShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::PixelShader* a_pixelShader)
	{
		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		auto* currentVertexShader = reinterpret_cast<RE::BSGraphics::VertexShader*>(vertexEntry);

		const bool allowNarrowDFLightFullContractBind =
			ShouldBindPreNGDFLightFullContractDescriptorShader() &&
			IsPreNGDFLightFullContractDescriptorShader(
				a_shader,
				static_cast<std::uint32_t>(a_pixelDescriptor));
		if (!ShouldBindPreNGDescriptorShaders() && !allowNarrowDFLightFullContractBind) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"gated",
				"FO4CS_LLF_PRENG_DESCRIPTOR_BIND-off");
			return false;
		}
		if (vertexEntry == 0 || vertexD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "current-vs-entry-unavailable");
			return false;
		}
		const auto pixelD3D = reinterpret_cast<std::uintptr_t>(a_pixelShader ? a_pixelShader->shader : nullptr);
		if (!a_pixelShader || pixelD3D == 0) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "missing-owned-ps");
			return false;
		}
		if (IsPreNGDFLightFullShadowedDescriptorConsumerShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)) &&
			(!globals::features::lightLimitFix.loaded ||
			 !globals::features::lightLimitFix.HasPreNGDFLightDescriptorConsumerData())) {
			LogPreNGDescriptorBind(
				a_shader,
				a_vertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				a_pixelDescriptor,
				currentVertexShader,
				a_pixelShader,
				hullEntry,
				domainEntry,
				"held",
				"full-shadowed-prepared-local-light-data-unavailable");
			return false;
		}

		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!IsReadableMemory(bindAddr, 16) || !IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "bind-helper-or-pixel-global-unavailable");
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "failed", "pixel-global-write-failed");
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		LogPreNGDescriptorBind(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor, currentVertexShader, a_pixelShader, hullEntry, domainEntry, "bound", "owned-dflight-pixel-current-vs-bound");

		if (globals::features::lightLimitFix.loaded) {
			globals::features::lightLimitFix.BindPreNGDFLightDescriptorResourcesToPixelShader();
			globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
				"descriptor-dflight-bind",
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_pixelDescriptor),
				true,
				pixelD3D);
		}
		return true;
	}

	bool TryBindPreNGDeferredLightingPixelShader(RE::BSShader& a_shader, std::uint32_t a_pixelDescriptor)
	{
		auto* pixelShader = ShaderCache::GetSingleton()->GetPixelShader(a_shader, a_pixelDescriptor);
		auto* pixelD3D = pixelShader ? reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader) : nullptr;
		if (!pixelShader || !pixelD3D) {
			return false;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (vertexEntry == 0 || !IsReadableMemory(bindAddr, 16) ||
			!IsWritableMemory(pixelGlobal, sizeof(std::uintptr_t))) {
			return false;
		}

		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(pixelShader);
		if (!WritePreNGValue(pixelGlobal, pixelEntry)) {
			return false;
		}

		using PreNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);
		Deferred::GetSingleton()->RegisterLightingPixelShader(pixelD3D);

		static std::atomic_uint32_t boundCount = 0;
		const auto bindIndex = ++boundCount;
		if (bindIndex <= 8 || IsPreNGPowerOfTwo(bindIndex)) {
			logger::info(
				"[BSShaderHooks] PreNG unified Deferred BSLighting PS bound count={} psDesc=0x{:X} psEntry=0x{:X} psD3D=0x{:X}",
				bindIndex,
				a_pixelDescriptor,
				pixelEntry,
				reinterpret_cast<std::uintptr_t>(pixelD3D));
		}
		return true;
	}

#endif
}
