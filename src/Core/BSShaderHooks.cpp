#include "Core/BSShaderHooks.h"
#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"
#include "Core/Deferred.h"
#include "Core/Feature.h"
#include "Core/Globals.h"
#include "Core/PreNGEnvironment.h"
#include "Core/ShaderHooks/PreNGDescriptorDiagnostics.h"
#include "Core/ShaderHooks/PreNGBSLightingBind.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
#include "Core/ShaderHooks/PreNGLookupDiagnostics.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGSwitches.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"
#include "Core/ShaderHooks/PreNGVanillaDumps.h"
#include "Core/ShaderCompiler.h"
#include "Core/ShaderCache.h"
#include "Features/LightLimitFix.h"
#include "Render/RuntimeAdapter.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <winrt/base.h>
#include <RE/FO4Runtime.h>

namespace CommunityShaders
{
	// ── BSShader::ReloadShaders hook ──────────────────────────────
	//
	// Port of Skyrim CS BSShader::LoadShaders hook.
	// FO4 equivalent: BSShader::ReloadShaders(bool) at vfunc 0x0B.
	//
	// Deferred execution pattern:
	//   ReloadShaders may fire before the D3D device is stable (loading
	//   screens, device creation).  Instead of silently dropping these
	//   calls, we enqueue the BSShader* and drain the queue once per
	//   frame when the device is ready (≥ kStableFrame frames).
	//
	static constexpr std::uint64_t kStableFrame = 5;

	// ── Forward declarations ────────────────────────────────────

	void ReplacePixelShaders(RE::BSShader* shader);
	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device*, const RE::BSShader&, std::uint32_t);
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

	bool ValidatePreNGShaderPath(std::uintptr_t a_imageBase, std::uintptr_t a_vtableAddr)
	{
		const auto setupTechnique = F4Runtime::PreNG::BS_LIGHTING_SHADER_SETUP_TECHNIQUE.address();
		const auto shaderLookup = F4Runtime::PreNG::BS_SHADER_LOOKUP.address();
		const auto bindShaders = F4Runtime::PreNG::BIND_SHADERS.address();
		const bool vtableReadable = IsReadableMemory(a_vtableAddr + (0x02 * sizeof(std::uintptr_t)), sizeof(std::uintptr_t));
		const bool setupReadable = IsReadableMemory(setupTechnique, 16);
		const bool lookupReadable = IsReadableMemory(shaderLookup, 16);
		const bool bindReadable = IsReadableMemory(bindShaders, 16);
		std::uintptr_t observedSetupTechnique = 0;
		if (vtableReadable) {
			ReadPreNGValue(a_vtableAddr + (0x02 * sizeof(std::uintptr_t)), observedSetupTechnique);
		}

		const bool setupMatches = observedSetupTechnique == setupTechnique;
		logger::info(
			"[BSShaderHooks] PreNG active shader path validation base=0x{:X} vtable=0x{:X} vfunc[0x02]=0x{:X} expectedSetupTechnique=0x{:X} shaderLookup=0x{:X} bindShaders=0x{:X} setupMatches={} readable(vtable={}, setup={}, lookup={}, bind={})",
			a_imageBase,
			a_vtableAddr,
			observedSetupTechnique,
			setupTechnique,
			shaderLookup,
			bindShaders,
			setupMatches,
			vtableReadable,
			setupReadable,
			lookupReadable,
			bindReadable);

		return setupMatches && setupReadable && lookupReadable && bindReadable;
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

	struct PreNGBSShaderLookup
	{
		static std::uint8_t thunk(
			RE::BSShader* a_shader,
			std::int32_t a_vertexDescriptor,
			std::int32_t a_hullDescriptor,
			std::int32_t a_domainDescriptor,
			std::int32_t a_pixelDescriptor)
		{
			if (!func) {
				return 0;
			}
			const bool shaderLookupTraceActive =
				ShouldEnablePreNGShaderLookupDiagnostic() &&
				!ShouldBypassPreNGShaderLookupHeavyDiagnostics();
			const bool descriptorPathActive =
				ShouldMutatePreNGDescriptorShaders() ||
				ShouldCompilePreNGDescriptorShadersForDiagnostic() ||
				ShouldBindPreNGDescriptorShaders();
			const bool dflightFullShadowedCandidate =
				ShouldBindPreNGDFLightFullShadowedCandidate() &&
				IsPreNGDFLightFullShadowedCandidateLookup(a_shader, a_pixelDescriptor);
			const bool bsLightingContractCompileActive =
				ShouldCompilePreNGBSLightingContractShader() &&
				IsPreNGBSLightingContractDescriptorShader(a_shader, a_pixelDescriptor);
			const bool bsLightingConsumerCompileActive =
				ShouldCompilePreNGBSLightingConsumerShader() &&
				IsPreNGBSLightingContractDescriptorShader(a_shader, a_pixelDescriptor);
			const bool bsLightingDescriptorObserveActive =
				ShouldObservePreNGBSLightingDescriptors() &&
				IsPreNGBSLightingVanillaDumpLookup(a_shader);
			const bool bsLightingResourceBindActive =
				ShouldBindPreNGBSLightingDescriptorResources() &&
				IsPreNGBSLightingContractDescriptorShader(a_shader, a_pixelDescriptor);
			const bool bsLightingVanillaBindActive =
				ShouldBindPreNGBSLightingVanillaDescriptorShader() &&
				IsPreNGBSLightingContractDescriptorShader(a_shader, a_pixelDescriptor);
			const bool bsLightingLLFBindActive =
				ShouldBindPreNGBSLightingLLFConsumerShader() &&
				IsPreNGBSLightingContractDescriptorShader(a_shader, a_pixelDescriptor);
			const bool bsLightingVanillaDump =
				ShouldDumpPreNGBSLightingVanillaShader() &&
				IsPreNGBSLightingVanillaDumpLookup(a_shader);
			const bool dflightVanillaDump =
				ShouldDumpPreNGDFLightVanillaShader() &&
				IsPreNGDFLightVanillaDumpLookup(a_shader, a_pixelDescriptor);
			const bool dflightDescriptorObserveActive =
				ShouldObservePreNGDFLightDescriptors() &&
				IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeObserveActive =
				ShouldObservePreNGDFCompositeDescriptors() &&
				IsPreNGDFCompositeDescriptorShader(a_shader);
			const bool dfCompositeDescriptorCompileActive =
				ShouldCompilePreNGDFCompositeDescriptorShader() &&
				IsPreNGDFCompositeContractDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeResourceBindActive =
				ShouldBindPreNGDFCompositeDescriptorResources() &&
				IsPreNGDFCompositeContractDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeSafeBindActive =
				ShouldBindPreNGDFCompositeSafeDescriptorShader() &&
				IsPreNGDFCompositeSafeBindDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool dfCompositeVanillaDump =
				ShouldDumpPreNGDFCompositeVanillaShader() &&
				IsPreNGDFCompositeVanillaDumpLookup(a_shader);
			if (!shaderLookupTraceActive &&
				!descriptorPathActive &&
				!dflightDescriptorObserveActive &&
				!dflightFullShadowedCandidate &&
				!bsLightingContractCompileActive &&
				!bsLightingConsumerCompileActive &&
				!bsLightingDescriptorObserveActive &&
				!bsLightingResourceBindActive &&
				!bsLightingVanillaBindActive &&
				!bsLightingLLFBindActive &&
				!bsLightingVanillaDump &&
				!dflightVanillaDump &&
				!dfCompositeObserveActive &&
				!dfCompositeDescriptorCompileActive &&
				!dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeVanillaDump) {
				return func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);
			}

			auto lookupVertexDescriptor = a_vertexDescriptor;
			auto lookupPixelDescriptor = a_pixelDescriptor;
			const bool isLightingDescriptor = IsPreNGLightingDescriptorShader(a_shader);
			const bool shaderLookupDiagnosticActive = shaderLookupTraceActive;
			const bool descriptorLookupActive = descriptorPathActive && isLightingDescriptor;
			if (!shaderLookupDiagnosticActive &&
				!descriptorLookupActive &&
				!dflightDescriptorObserveActive &&
				!dflightFullShadowedCandidate &&
				!bsLightingContractCompileActive &&
				!bsLightingConsumerCompileActive &&
				!bsLightingDescriptorObserveActive &&
				!bsLightingResourceBindActive &&
				!bsLightingVanillaBindActive &&
				!bsLightingLLFBindActive &&
				!bsLightingVanillaDump &&
				!dflightVanillaDump &&
				!dfCompositeObserveActive &&
				!dfCompositeDescriptorCompileActive &&
				!dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeVanillaDump) {
				return func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);
			}

			const bool isBSLightingDescriptor =
				descriptorLookupActive &&
				a_shader &&
				a_shader->shaderType == kPreNGBSLightingShaderType;
			const bool isDFLightLLFConsumerDescriptor =
				dflightDescriptorObserveActive ||
				(descriptorLookupActive &&
				 IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)));
			const bool isDFLightFullShadowedDescriptorConsumer =
				descriptorLookupActive &&
				IsPreNGDFLightFullShadowedDescriptorConsumerShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			const bool canActivelyReplaceDescriptor =
				descriptorLookupActive &&
				CanActivelyReplacePreNGLightingDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor));
			if (descriptorLookupActive) {
				if (ShouldMutatePreNGDescriptorShaders()) {
					if (canActivelyReplaceDescriptor) {
						if (isBSLightingDescriptor) {
							lookupVertexDescriptor = static_cast<std::int32_t>(
								NormalizePreNGLightingVertexDescriptor(static_cast<std::uint32_t>(a_vertexDescriptor)));
							lookupPixelDescriptor = static_cast<std::int32_t>(
								NormalizePreNGLightingPixelDescriptor(static_cast<std::uint32_t>(a_pixelDescriptor)));
						}
						Deferred::ShaderLookupDescriptorState deferredDescriptorState{};
						if (a_shader) {
							deferredDescriptorState = Deferred::GetSingleton()->BuildShaderLookupDescriptorState(
								*a_shader,
								static_cast<std::uint32_t>(lookupVertexDescriptor),
								static_cast<std::uint32_t>(lookupPixelDescriptor));
							lookupVertexDescriptor = static_cast<std::int32_t>(deferredDescriptorState.vertexDescriptor);
							lookupPixelDescriptor = static_cast<std::int32_t>(deferredDescriptorState.pixelDescriptor);
						}
						const auto changed = lookupVertexDescriptor != a_vertexDescriptor ||
						                     lookupPixelDescriptor != a_pixelDescriptor;
						const char* mutationReason = changed ? "fo4-lighting-normalized" : "fo4-lighting-normalized-unchanged";
						if (isDFLightLLFConsumerDescriptor) {
							mutationReason = "fo4-dflight-llf-consumer-preserved";
						}
						if (isDFLightFullShadowedDescriptorConsumer) {
							mutationReason = "fo4-dflight-full-shadowed-consumer-preserved";
						}
						if (deferredDescriptorState.modified) {
							mutationReason = deferredDescriptorState.reason;
						}
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"applied",
							mutationReason);
					} else if (shaderLookupTraceActive) {
						LogPreNGDescriptorMutation(
							a_shader,
							a_vertexDescriptor,
							a_pixelDescriptor,
							lookupVertexDescriptor,
							lookupPixelDescriptor,
							"held",
							"active-descriptor-path-held");
					}
				} else if (shaderLookupTraceActive || canActivelyReplaceDescriptor) {
					LogPreNGDescriptorMutation(
						a_shader,
						a_vertexDescriptor,
						a_pixelDescriptor,
						lookupVertexDescriptor,
						lookupPixelDescriptor,
						"gated",
						"FO4CS_LLF_PRENG_DESCRIPTOR_MUTATE-off");
				}
			}

			const auto result = func(
				a_shader,
				lookupVertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				lookupPixelDescriptor);
			if (shaderLookupDiagnosticActive || bsLightingDescriptorObserveActive) {
				TracePreNGShaderLookupEntry(
					a_shader,
					a_vertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					a_pixelDescriptor,
					lookupVertexDescriptor,
					lookupPixelDescriptor,
					result != 0);
				if (shaderLookupDiagnosticActive) {
					TracePreNGShaderLookup(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						result != 0);
					if (shaderLookupTraceActive) {
						MaybeCompletePreNGShaderLookupHeavyDiagnostics();
					}
				}
			}
			if (bsLightingVanillaDump) {
				DumpPreNGBSLightingVanillaShader(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (bsLightingContractCompileActive) {
				(void)ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
			}
			if (bsLightingConsumerCompileActive) {
				(void)ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
			}
			static std::atomic_bool bsLightingResourceProofComplete = false;
			static std::atomic_bool bsLightingResourceProofCompleteLogged = false;
			if (bsLightingResourceBindActive &&
				!bsLightingResourceProofComplete.load(std::memory_order_relaxed) &&
				globals::features::lightLimitFix.loaded) {
				const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
				const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
				globals::features::lightLimitFix.NotifyPreNGBSLightingLLFConsumerDescriptorObserved(
					static_cast<std::uint32_t>(lookupVertexDescriptor),
					static_cast<std::uint32_t>(lookupPixelDescriptor),
					result != 0,
					vanillaPixelD3D);
				if (globals::features::lightLimitFix.HasPreNGBSLightingDescriptorConsumerData()) {
					const auto resourceState =
						globals::features::lightLimitFix.BindPreNGBSLightingDescriptorResourcesToPixelShader();

					static std::atomic_uint32_t bsLightingResourceAuditCount = 0;
					const auto auditIndex = ++bsLightingResourceAuditCount;
					if (auditIndex <= 8 || auditIndex % 512 == 0) {
						globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
							"descriptor-bslighting-resource-bind",
							a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
							static_cast<std::uint32_t>(lookupVertexDescriptor),
							static_cast<std::uint32_t>(lookupPixelDescriptor),
							result != 0,
							vanillaPixelD3D);
					}
					if (resourceState.strictCBBound && resourceState.clusterSRVsBound &&
						!bsLightingResourceProofComplete.exchange(true, std::memory_order_relaxed) &&
						!bsLightingResourceProofCompleteLogged.exchange(true, std::memory_order_relaxed)) {
						logger::info(
							"[BSShaderHooks] PreNG BSLighting resource-only proof reached b3/t35-t37 completion on vanilla BSLighting shader; future resource-only BSLighting binds are held until a visible-safe consumer is implemented");
					}
				} else {
					static std::atomic_uint32_t bsLightingResourcePendingCount = 0;
					const auto pendingIndex = ++bsLightingResourcePendingCount;
					if (pendingIndex <= 8 || pendingIndex % 512 == 0) {
						logger::info(
							"[BSShaderHooks] PreNG BSLighting descriptor resource bind pending clustered payload pending={} vsDesc=0x{:X} psDesc=0x{:X} vanillaFound={} vanillaPS=0x{:X}",
							pendingIndex,
							static_cast<std::uint32_t>(lookupVertexDescriptor),
							static_cast<std::uint32_t>(lookupPixelDescriptor),
							result != 0,
							vanillaPixelD3D);
					}
				}
			}
			static std::atomic_bool bsLightingVanillaBindProofComplete = false;
			if (bsLightingVanillaBindActive &&
				!bsLightingVanillaBindProofComplete.load(std::memory_order_relaxed)) {
				if (TryBindPreNGBSLightingVanillaPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						result != 0)) {
					bsLightingVanillaBindProofComplete.store(true, std::memory_order_relaxed);
					return 1;
				}
			}
			// Visible LLF consumer bind. Unlike the vanilla proof bind above this
			// is a production path: it replaces the BSLighting PS entry on every
			// eligible draw so clustered lighting stays visible, not just once.
			if (bsLightingLLFBindActive) {
				if (TryBindPreNGBSLightingLLFConsumerPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						result != 0)) {
					return 1;
				}
			}
			if (dflightVanillaDump) {
				DumpPreNGDFLightVanillaShader(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (dfCompositeObserveActive) {
				TracePreNGDFCompositeDescriptor(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result != 0);
			}
			if (dfCompositeDescriptorCompileActive) {
				ObservePreNGDFCompositeDescriptorShader(
					a_shader,
					lookupVertexDescriptor,
					lookupPixelDescriptor,
					result != 0);
				(void)ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
			}
			static std::atomic_bool dfCompositeResourceProofComplete = false;
			static std::atomic_bool dfCompositeResourceProofCompleteLogged = false;
			if (dfCompositeResourceBindActive &&
				!dfCompositeSafeBindActive &&
				!dfCompositeResourceProofComplete.load(std::memory_order_relaxed) &&
				globals::features::lightLimitFix.loaded) {
				const auto vanillaPixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
				const auto vanillaPixelD3D = ReadPreNGShaderEntryD3DObject(vanillaPixelEntry);
				auto* ownedPixelShader = ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
				const auto ownedPixelD3D = reinterpret_cast<std::uintptr_t>(ownedPixelShader ? ownedPixelShader->shader : nullptr);
				globals::features::lightLimitFix.NotifyPreNGDFCompositeLLFConsumerDescriptorObserved(
					static_cast<std::uint32_t>(lookupVertexDescriptor),
					static_cast<std::uint32_t>(lookupPixelDescriptor),
					result != 0,
					vanillaPixelD3D,
					ownedPixelD3D);
				if (globals::features::lightLimitFix.HasPreNGDFCompositeDescriptorConsumerData()) {
					const auto resourceState =
						globals::features::lightLimitFix.BindPreNGDFCompositeDescriptorResourcesToPixelShader();

					static std::atomic_uint32_t dfCompositeResourceAuditCount = 0;
					const auto auditIndex = ++dfCompositeResourceAuditCount;
					if (auditIndex <= 8 || auditIndex % 512 == 0) {
						globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
							"descriptor-dfcomposite-resource-bind",
							a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
							static_cast<std::uint32_t>(lookupVertexDescriptor),
							static_cast<std::uint32_t>(lookupPixelDescriptor),
							result != 0,
							vanillaPixelD3D);
					}
					if (resourceState.strictCBBound && resourceState.clusterSRVsBound &&
						!dfCompositeResourceProofComplete.exchange(true, std::memory_order_relaxed) &&
						!dfCompositeResourceProofCompleteLogged.exchange(true, std::memory_order_relaxed)) {
						logger::info(
							"[BSShaderHooks] PreNG DFComposite resource-only proof reached b3/t35-t37 completion; future resource-only DFComposite binds are held until a visible-safe bind gate is enabled");
					}
				}
			}
			if (dfCompositeSafeBindActive) {
				auto* pixelShader = ShaderCache::GetSingleton()->GetPixelShader(
					*a_shader,
					static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (TryBindPreNGDFCompositeDescriptorPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						result != 0,
						pixelShader)) {
					return 1;
				}
				return result;
			}
			if (dfCompositeVanillaDump) {
				DumpPreNGDFCompositeVanillaShader(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (dflightFullShadowedCandidate) {
				TryBindPreNGDFLightFullShadowedCandidate(
					a_shader,
					lookupVertexDescriptor,
					a_hullDescriptor,
					a_domainDescriptor,
					lookupPixelDescriptor,
					result);
			}
			if (isDFLightLLFConsumerDescriptor && (canActivelyReplaceDescriptor || dflightDescriptorObserveActive)) {
				const bool allowDFLightDescriptorBind =
					isDFLightFullShadowedDescriptorConsumer ||
					ShouldBindPreNGDFLightFullContractDescriptorShader();
				const bool allowDFLightFullContractCompileOnly =
					!allowDFLightDescriptorBind &&
					ShouldCompilePreNGDFLightFullContractDescriptorShader() &&
					IsPreNGDFLightFullContractDescriptorShader(
						a_shader,
						static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (!allowDFLightDescriptorBind) {
					if (allowDFLightFullContractCompileOnly) {
						(void)ShaderCache::GetSingleton()->GetPixelShader(
							*a_shader,
							static_cast<std::uint32_t>(lookupPixelDescriptor));
					}
					LogPreNGDFLightFullContractDescriptorBindHeld(
						a_shader,
						lookupVertexDescriptor,
						lookupPixelDescriptor,
						result != 0);
					return result;
				}

				auto* pixelShader = ShaderCache::GetSingleton()->GetPixelShader(*a_shader, static_cast<std::uint32_t>(lookupPixelDescriptor));
				if (globals::features::lightLimitFix.loaded) {
					const auto ownedPixelD3D = reinterpret_cast<std::uintptr_t>(pixelShader ? pixelShader->shader : nullptr);
					globals::features::lightLimitFix.NotifyPreNGDFLightLLFConsumerDescriptorObserved(
						static_cast<std::uint32_t>(lookupVertexDescriptor),
						static_cast<std::uint32_t>(lookupPixelDescriptor),
						result != 0,
						ownedPixelD3D);
				}
				if (TryBindPreNGDFLightDescriptorPixelShader(
						a_shader,
						lookupVertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						lookupPixelDescriptor,
						pixelShader)) {
					return 1;
				}
				return result;
			}
			if (result != 0 || !canActivelyReplaceDescriptor) {
				return result;
			}

			auto* shaderCache = ShaderCache::GetSingleton();
			auto* vertexShader = shaderCache->GetVertexShader(*a_shader, static_cast<std::uint32_t>(lookupVertexDescriptor));
			auto* pixelShader = shaderCache->GetPixelShader(*a_shader, static_cast<std::uint32_t>(lookupPixelDescriptor));
			return TryBindPreNGDescriptorShaders(
				a_shader,
				lookupVertexDescriptor,
				a_hullDescriptor,
				a_domainDescriptor,
				lookupPixelDescriptor,
				vertexShader,
				pixelShader) ?
				1 :
				0;
		}

		static inline std::uint8_t (*func)(RE::BSShader*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr;
	};

	void LogPreNGShaderLookupDetourPatch(std::uintptr_t a_lookupAddr)
	{
		std::uint8_t opcode = 0;
		std::int32_t rel32 = 0;
		const bool readable = IsReadableMemory(a_lookupAddr, 5);
		if (readable) {
			ReadPreNGValue(a_lookupAddr, opcode);
			ReadPreNGValue(a_lookupAddr + 1, rel32);
		}

		std::uintptr_t branchTarget = 0;
		if (readable && opcode == 0xE9) {
			branchTarget = static_cast<std::uintptr_t>(
				static_cast<std::intptr_t>(a_lookupAddr + 5) + rel32);
		}

		const auto thunk = reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::thunk);
		const auto original = reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::func);
		const bool branchTargetReadable = branchTarget != 0 && IsReadableMemory(branchTarget, 16);
		const bool directThunk = branchTarget == thunk;
		const bool detoursTrampoline = original != 0 && branchTargetReadable && branchTarget != thunk;
		const bool patchVerified = directThunk || detoursTrampoline;
		logger::info(
			"[BSShaderHooks] PreNG shader lookup detour patch check lookup=0x{:X} readable={} opcode=0x{:02X} branchTarget=0x{:X} thunk=0x{:X} patchVerified={} branchTargetReadable={} directThunk={} detoursTrampoline={} original=0x{:X}",
			a_lookupAddr,
			readable,
			opcode,
			branchTarget,
			thunk,
			patchVerified,
			branchTargetReadable,
			directThunk,
			detoursTrampoline,
			original);
	}
#endif
#if !defined(FALLOUT_PRE_NG)
	// ── PostNG / PostAE forward clustered consumer ─────────────────
	// Mirrors the PreNG BSLighting LLF consumer bind, but for the PostNG/AE
	// runtime using the statically verified PostNG:: addresses (image-base
	// static VAs rebased by RuntimeAddressValue::address()). FO4 forward has
	// no b3 strict-light buffer; the consumer swaps the pixel shader and
	// re-asserts the cluster SRVs t35-t37 that RunClusterPrepass already binds.
	namespace F4Runtime = RE::FO4Runtime;

	constexpr std::int32_t kPostNGBSLightingShaderType =
		static_cast<std::int32_t>(F4Runtime::ShaderType::kLighting);
	constexpr const char* kPostNGBSLightingDescriptorObserveEnv =
		"FO4CS_LLF_POSTNG_BSLIGHTING_DESCRIPTOR_OBSERVE";
	constexpr const char* kPostNGBSLightingLLFBindEnv =
		"FO4CS_LLF_POSTNG_BSLIGHTING_LLF_BIND";

	std::atomic_uint32_t s_postNGBSLightingLLFConsumerBindAttempts = 0;
	std::atomic_uint32_t s_postNGBSLightingLLFConsumerBoundCount = 0;

	bool ShouldObservePostNGBSLightingDescriptors()
	{
		return DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingDescriptorObserveEnv);
	}

	bool ShouldBindPostNGBSLightingLLFConsumerShader()
	{
		return DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingLLFBindEnv);
	}

	std::uintptr_t ReadPostNGPointer(std::uintptr_t a_address)
	{
		return F4Runtime::ReadPointer(a_address);
	}

	std::uintptr_t ReadPostNGShaderEntryD3DObject(std::uintptr_t a_entry)
	{
		if (a_entry == 0)
		{
			return 0;
		}
		return F4Runtime::ReadPointer(F4Runtime::PostNG::SHADER_ENTRY_D3D_OBJECT.address(a_entry));
	}

	bool TryBindPostNGBSLightingLLFConsumerPixelShader(
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
		const auto attempt = ++s_postNGBSLightingLLFConsumerBindAttempts;
		const bool shouldLog = attempt <= 8 || (attempt & (attempt - 1)) == 0;

		auto logBind = [&](const char* a_state, const char* a_reason, std::uintptr_t a_vertexEntry,
		                   std::uintptr_t a_pixelEntry, std::uintptr_t a_consumerPSD3D) {
			if (!shouldLog && std::strcmp(a_state, "bound") != 0)
			{
				return;
			}
			logger::info(
				"[BSShaderHooks] PostNG/AE BSLighting LLF consumer bind attempts={} binds={} shaderType={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} vsEntry=0x{:X} psEntry=0x{:X} consumerPSD3D=0x{:X} found={} state={} reason={}",
				attempt,
				s_postNGBSLightingLLFConsumerBoundCount.load(std::memory_order_relaxed),
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_vertexEntry,
				a_pixelEntry,
				a_consumerPSD3D,
				a_found,
				a_state,
				a_reason);
		};

		if (!a_found || !a_shader)
		{
			logBind("failed", a_found ? "null-shader" : "vanilla-lookup-miss", 0, 0, 0);
			return false;
		}
		if (!llfFeature || !llfFeature->HasPostNGBSLightingDescriptorConsumerData())
		{
			logBind("held", "clustered-payload-pending", 0, 0, 0);
			return false;
		}

		const auto vanillaPixelEntry = ReadPostNGPointer(F4Runtime::PostNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vanillaPixelD3D = ReadPostNGShaderEntryD3DObject(vanillaPixelEntry);

		auto* consumerShader = ShaderCache::GetSingleton()->GetPixelShader(*a_shader, pixelDescriptor);
		const auto consumerPSD3D = consumerShader ?
			reinterpret_cast<std::uintptr_t>(consumerShader->shader) :
			0;
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(consumerShader);
		if (!consumerShader || consumerPSD3D == 0 || pixelEntry == 0)
		{
			logBind("failed", "consumer-ps-unavailable", 0, pixelEntry, consumerPSD3D);
			return false;
		}

		const auto vertexEntry = ReadPostNGPointer(F4Runtime::PostNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPostNGPointer(F4Runtime::PostNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPostNGPointer(F4Runtime::PostNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPostNGShaderEntryD3DObject(vertexEntry);
		if (vertexEntry == 0 || vertexD3D == 0)
		{
			logBind("failed", "current-vs-entry-unavailable", vertexEntry, pixelEntry, consumerPSD3D);
			return false;
		}

		const auto bindAddr = F4Runtime::PostNG::BIND_SHADERS.address();
		const auto pixelGlobal = F4Runtime::PostNG::CURRENT_PIXEL_SHADER_ENTRY.address();
		if (!F4Runtime::IsReadableAddress(bindAddr, 16) || !F4Runtime::IsWritableAddress(pixelGlobal, sizeof(std::uintptr_t)))
		{
			logBind("failed", "bind-helper-or-pixel-global-unavailable", vertexEntry, pixelEntry, consumerPSD3D);
			return false;
		}
		if (!F4Runtime::WriteValue(pixelGlobal, pixelEntry))
		{
			logBind("failed", "pixel-global-write-failed", vertexEntry, pixelEntry, consumerPSD3D);
			return false;
		}

		using PostNGBindShadersFn = void* (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
		auto bindShaders = reinterpret_cast<PostNGBindShadersFn>(bindAddr);
		bindShaders(F4Runtime::PostNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);

		llfFeature->BindPostNGBSLightingClusterResourcesToPixelShader();
		llfFeature->NotifyPostNGBSLightingLLFConsumerDescriptorObserved(
			static_cast<std::uint32_t>(a_vertexDescriptor),
			pixelDescriptor,
			a_found,
			vanillaPixelD3D);

		s_postNGBSLightingLLFConsumerBoundCount.fetch_add(1, std::memory_order_relaxed);
		logBind("bound", "llf-consumer-current-vs-bound", vertexEntry, pixelEntry, consumerPSD3D);
		return true;
	}

	struct PostNGShaderLookup
	{
		static std::uint8_t thunk(
			RE::BSShader* a_shader,
			std::int32_t a_vertexDescriptor,
			std::int32_t a_hullDescriptor,
			std::int32_t a_domainDescriptor,
			std::int32_t a_pixelDescriptor)
		{
			if (!func)
			{
				return 0;
			}
			const auto result = func(a_shader, a_vertexDescriptor, a_hullDescriptor, a_domainDescriptor, a_pixelDescriptor);

			const bool consumerActive = ShouldBindPostNGBSLightingLLFConsumerShader();
			const bool observeActive = ShouldObservePostNGBSLightingDescriptors();
			if (!consumerActive && !observeActive)
			{
				return result;
			}
			if (!a_shader || a_shader->shaderType != kPostNGBSLightingShaderType)
			{
				return result;
			}

			if (consumerActive)
			{
				if (TryBindPostNGBSLightingLLFConsumerPixelShader(
						a_shader,
						a_vertexDescriptor,
						a_hullDescriptor,
						a_domainDescriptor,
						a_pixelDescriptor,
						result != 0))
				{
					return 1;
				}
			}
			else if (observeActive && globals::features::lightLimitFix.loaded)
			{
				const auto vanillaPixelEntry = ReadPostNGPointer(F4Runtime::PostNG::CURRENT_PIXEL_SHADER_ENTRY.address());
				const auto vanillaPixelD3D = ReadPostNGShaderEntryD3DObject(vanillaPixelEntry);
				globals::features::lightLimitFix.NotifyPostNGBSLightingLLFConsumerDescriptorObserved(
					static_cast<std::uint32_t>(a_vertexDescriptor),
					static_cast<std::uint32_t>(a_pixelDescriptor),
					result != 0,
					vanillaPixelD3D);
			}

			return result;
		}

		static inline std::uint8_t (*func)(RE::BSShader*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) = nullptr;
	};
#endif

	// ── Deferred shader replacement queue ─────────────────────────

	struct PendingReplace { RE::BSShader* shader; };
	static std::vector<PendingReplace> s_pending;

	static void DrainPending()
	{
		if (s_pending.empty()) return;

		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		auto* device = runtime->GetDevice();
		if (!device || runtime->GetFrameCount() < kStableFrame) return;

		logger::info("[BSShaderHooks] DrainPending: {} queued shader(s) at frame {}",
		             s_pending.size(), runtime->GetFrameCount());

		// Move out so ReplacePixelShaders can re-enqueue on failure
		auto pending = std::move(s_pending);
		s_pending.clear();

		for (auto& item : pending)
			ReplacePixelShaders(item.shader);
	}

	// ── Hook thunk ────────────────────────────────────────────────

	struct BSShader_ReloadShaders
	{
		static void thunk(RE::BSShader* shader, bool a_clear)
		{
			logger::info("[BSShaderHooks] thunk: type={} clear={}", shader->shaderType, a_clear);

			func(shader, a_clear);  // let game load originals first

			auto* runtime = CommunityShaders::Runtime::GetSingleton();
			if (!runtime->GetDevice() || runtime->GetFrameCount() < kStableFrame) {
				// Defer: device not stable yet — enqueue for later
				s_pending.push_back({ shader });
				return;
			}

			ReplacePixelShaders(shader);
		}
		static inline void (*func)(RE::BSShader*, bool) = nullptr;
	};

	void ReplacePixelShaders(RE::BSShader* shader)
	{
#if defined(FALLOUT_PRE_NG)
		(void)shader;
		static bool loggedPreNGHold = false;
		if (!loggedPreNGHold) {
			logger::info("[BSShaderHooks] PreNG pixel shader replacement held; LLF is advancing through the Skyrim-style engine-lighting path, not ShaderDB hash activation");
			loggedPreNGHold = true;
		}
		return;
#else
		auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
		if (!device) return;

		for (auto* entry : shader->pixelShaders) {
			if (!entry->shader) continue;

			auto pixelDesc = entry->id;
			auto vertexDesc = entry->id;
			ModifyShaderLookup(*shader, vertexDesc, pixelDesc);

			auto* newPS = CompileReplacementPS(device, *shader, pixelDesc);
			if (newPS) {
				entry->shader->Release();
				entry->shader = reinterpret_cast<decltype(entry->shader)>(newPS);
				Deferred::GetSingleton()->RegisterLightingPixelShader(newPS);
				logger::info("[BSShaderHooks] Replaced PS: type={} desc=0x{:08X}",
				             shader->shaderType, pixelDesc);
			}
		}
#endif
	}

	// ── Shader compilation ─────────────────────────────────────
	//
	// Compiles Lighting.hlsl with Feature defines injected.
	// Called by ReplacePixelShaders for each pixel shader entry.

	static ID3D11PixelShader* CompileReplacementPS(
		ID3D11Device* device,
		const RE::BSShader& /*shader*/,
		std::uint32_t /*descriptor*/)
	{
		std::vector<D3D_SHADER_MACRO> defines;
		defines.push_back({ "DEFERRED", "1" });
		defines.push_back({ "FO4CS_DEFERRED_LIGHTING_DESCRIPTOR", "1" });

		for (auto* feature : Feature::GetFeatureList()) {
			if (!feature->loaded) continue;
			auto name = feature->GetShaderDefineName();
			if (name.empty()) continue;
			defines.push_back({ name.data(), "1" });
		}

		D3D_SHADER_MACRO nullTerm{};
		defines.push_back(nullTerm);

		auto* compiler = ShaderCompiler::GetSingleton();
		auto bytecode = compiler->CompileFromFile(
			"Lighting.hlsl", "ps_5_0", defines.data(), "main");

		if (!bytecode) return nullptr;

		ID3D11PixelShader* ps = nullptr;
		if (FAILED(device->CreatePixelShader(
			bytecode->data(), bytecode->size(), nullptr, &ps))) {
			return nullptr;
		}
		return ps;
	}

	// ── ModifyShaderLookup ──────────────────────────────────────

	void ModifyShaderLookup(const RE::BSShader& a_shader,
	                        std::uint32_t& a_vertexDescriptor,
	                        std::uint32_t& a_pixelDescriptor)
	{
#if defined(FALLOUT_PRE_NG)
		if (!CanActivelyReplacePreNGLightingDescriptorShader(&a_shader, a_pixelDescriptor) || !ShouldMutatePreNGDescriptorShaders()) {
			return;
		}

		if (a_shader.shaderType == kPreNGBSLightingShaderType) {
			a_vertexDescriptor = NormalizePreNGLightingVertexDescriptor(a_vertexDescriptor);
			a_pixelDescriptor = NormalizePreNGLightingPixelDescriptor(a_pixelDescriptor);
		}
#else
		(void)a_shader;
		(void)a_vertexDescriptor;
		(void)a_pixelDescriptor;
#endif
	}

	// ── Install / Frame hook ────────────────────────────────────

	void BSShaderHooks::Install()
	{
		const auto imageBase = RE::FO4Runtime::ModuleBase();

#if defined(FALLOUT_PRE_NG)
		const auto vtableAddr = F4Runtime::PreNG::BS_LIGHTING_SHADER_VTABLE.address();
		const bool preNGShaderPathValid = ValidatePreNGShaderPath(imageBase, vtableAddr);
#else
		auto vtableReloc = REL::Relocation<std::uintptr_t>(
			RE::VTABLE::BSLightingShader[0]);
		auto vtableAddr = vtableReloc.address();
#endif
		auto* vtable = reinterpret_cast<std::uintptr_t*>(vtableAddr);
		auto reloadShadersFn = vtable[0x0B];

		logger::info("[BSShaderHooks] imageBase=0x{:X} vtable=0x{:X} vfunc[0x0B]=0x{:X}",
		             imageBase, vtableAddr, reloadShadersFn);

		BSShader_ReloadShaders::func = reinterpret_cast<void(*)(RE::BSShader*, bool)>(
			Detours::X64::DetourFunction(
				reloadShadersFn,
				reinterpret_cast<std::uintptr_t>(BSShader_ReloadShaders::thunk)));

		logger::info("[BSShaderHooks] Detoured ReloadShaders; drain gate at frame {}",
		             kStableFrame);

#if defined(FALLOUT_PRE_NG)
		const bool unifiedDeferredEnabled = fo4cs::RuntimeAdapter::Get().GetCapabilities().supportsDeferredPipeline;
		const bool preNGShaderLookupDiagEnabled = ShouldEnablePreNGShaderLookupDiagnostic();
		const bool preNGDescriptorMutateEnabled = ShouldMutatePreNGDescriptorShaders();
		const bool preNGDescriptorCompileEnabled = ShouldCompilePreNGDescriptorShadersForDiagnostic();
		const bool preNGDescriptorBindEnabled = ShouldBindPreNGDescriptorShaders();
		const bool preNGDFLightFullContractDescriptorCompileEnabled = ShouldCompilePreNGDFLightFullContractDescriptorShader();
		const bool preNGDFLightFullContractDescriptorBindEnabled = ShouldBindPreNGDFLightFullContractDescriptorShader();
		const bool preNGDFLightDescriptorObserveEnabled = ShouldObservePreNGDFLightDescriptors();
		const bool preNGDFCompositeDescriptorObserveEnabled = ShouldObservePreNGDFCompositeDescriptors();
		const bool preNGDFCompositeDescriptorCompileEnabled = ShouldCompilePreNGDFCompositeDescriptorShader();
		const bool preNGDFCompositeResourceBindEnabled = ShouldBindPreNGDFCompositeDescriptorResources();
		const bool preNGDFCompositeSafeBindEnabled = ShouldBindPreNGDFCompositeSafeDescriptorShader();
		const bool preNGDFCompositeFogSafeBindEnabled = ShouldBindPreNGDFCompositeFogSafeDescriptorShader();
		const bool preNGDFCompositeVanillaDumpEnabled = ShouldDumpPreNGDFCompositeVanillaShader();
		const bool preNGBSLightingContractCompileEnabled = ShouldCompilePreNGBSLightingContractShader();
		const bool preNGBSLightingConsumerCompileEnabled = ShouldCompilePreNGBSLightingConsumerShader();
		const bool preNGBSLightingDescriptorObserveEnabled = ShouldObservePreNGBSLightingDescriptors();
		const bool preNGBSLightingResourceBindEnabled = ShouldBindPreNGBSLightingDescriptorResources();
		const bool preNGBSLightingVanillaBindEnabled = ShouldBindPreNGBSLightingVanillaDescriptorShader();
		const bool preNGBSLightingLLFBindEnabled = ShouldBindPreNGBSLightingLLFConsumerShader();
		const bool preNGDescriptorPathEnabled =
			preNGShaderLookupDiagEnabled ||
			preNGDescriptorMutateEnabled ||
			preNGDescriptorCompileEnabled ||
			preNGDescriptorBindEnabled ||
			preNGDFLightDescriptorObserveEnabled ||
			preNGBSLightingContractCompileEnabled ||
			preNGBSLightingConsumerCompileEnabled ||
			preNGBSLightingDescriptorObserveEnabled ||
			preNGBSLightingResourceBindEnabled ||
			preNGBSLightingVanillaBindEnabled ||
			preNGDFCompositeDescriptorObserveEnabled ||
			preNGDFCompositeDescriptorCompileEnabled ||
			preNGDFCompositeResourceBindEnabled ||
			preNGDFCompositeSafeBindEnabled;
		const bool preNGDFLightFullShadowedBindEnabled = ShouldBindPreNGDFLightFullShadowedCandidate();
		const bool preNGDFLightFullShadowedDescriptorConsumerEnabled = ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer();
		const bool preNGBSLightingVanillaDumpEnabled = ShouldDumpPreNGBSLightingVanillaShader();
		const bool preNGDFLightVanillaDumpEnabled = ShouldDumpPreNGDFLightVanillaShader();
		if (preNGShaderPathValid && (unifiedDeferredEnabled || preNGDescriptorPathEnabled || preNGDFLightFullShadowedBindEnabled || preNGBSLightingVanillaDumpEnabled || preNGDFLightVanillaDumpEnabled || preNGDFCompositeVanillaDumpEnabled)) {
			const auto lookupAddr = F4Runtime::PreNG::BS_SHADER_LOOKUP.address();
			PreNGBSShaderLookup::func = reinterpret_cast<decltype(PreNGBSShaderLookup::func)>(
				Detours::X64::DetourFunction(
					lookupAddr,
					reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::thunk)));
			logger::info(
				"[BSShaderHooks] Detoured PreNG shader lookup at 0x{:X}; original=0x{:X}; unifiedDeferred={} lookupDiag={} descriptorMutate={} descriptorCompile={} descriptorBind={} dflightDescriptorObserve={} bsLightingContractCompile={} bsLightingConsumerCompile={} bsLightingDescriptorObserve={} bsLightingResourceBind={} bsLightingVanillaBind={} bsLightingLLFBind={} dfCompositeDescriptorObserve={} dfCompositeDescriptorCompile={} dfCompositeResourceBind={} dfCompositeSafeBind={} dfCompositeFogSafeBind={} dflightFullContractDescriptorCompile={} dflightFullContractDescriptorBind={} dflightFullShadowedDescriptorConsumer={} dflightFullShadowedBind={} bsLightingVanillaDump={} dflightVanillaDump={} dfCompositeVanillaDump={}",
				lookupAddr,
				reinterpret_cast<std::uintptr_t>(PreNGBSShaderLookup::func),
				unifiedDeferredEnabled,
				preNGShaderLookupDiagEnabled,
				preNGDescriptorMutateEnabled,
				preNGDescriptorCompileEnabled,
				preNGDescriptorBindEnabled,
				preNGDFLightDescriptorObserveEnabled,
				preNGBSLightingContractCompileEnabled,
				preNGBSLightingConsumerCompileEnabled,
				preNGBSLightingDescriptorObserveEnabled,
				preNGBSLightingResourceBindEnabled,
				preNGBSLightingVanillaBindEnabled,
				preNGBSLightingLLFBindEnabled,
				preNGDFCompositeDescriptorObserveEnabled,
				preNGDFCompositeDescriptorCompileEnabled,
				preNGDFCompositeResourceBindEnabled,
				preNGDFCompositeSafeBindEnabled,
				preNGDFCompositeFogSafeBindEnabled,
				preNGDFLightFullContractDescriptorCompileEnabled,
				preNGDFLightFullContractDescriptorBindEnabled,
				preNGDFLightFullShadowedDescriptorConsumerEnabled,
				preNGDFLightFullShadowedBindEnabled,
				preNGBSLightingVanillaDumpEnabled,
				preNGDFLightVanillaDumpEnabled,
				preNGDFCompositeVanillaDumpEnabled);
			LogPreNGShaderLookupDetourPatch(lookupAddr);
		} else if (!preNGShaderPathValid) {
			logger::warn("[BSShaderHooks] PreNG shader lookup diagnostic skipped; active shader path validation failed");
		} else {
			logger::info(
				"[BSShaderHooks] PreNG shader lookup diagnostic/descriptor path held; set {}=1 to enable descriptor path tracing, {}=1/{}=1/{}=1 for descriptor mutation/compile/bind, {}=1 for the narrow DFLight full-shadowed candidate bind proof, {}=1 for BSLighting contract compile-only proof, {}=1 for descriptor-specific BSLighting LLF consumer compile-only proof, {}=1 for BSLighting descriptor observe-only tracing, {}=1 for BSLighting vanilla-equivalent bind proof, {}=1 for vanilla BSLighting shader dump, {}=1 for vanilla DFLight shader dump, or {}=1/{}=1/{}=1/{}=1/{}=1/{}=1 for DFComposite observe/compile/resource-bind/safe-bind/fog-safe-bind/dump; shader replacement remains held",
				PreNGEnvironment::kPreNGShaderLookupDiagEnv,
				kPreNGDescriptorMutateEnv,
				kPreNGDescriptorCompileEnv,
				kPreNGDescriptorBindEnv,
				kPreNGDFLightFullShadowedBindEnv,
				kPreNGBSLightingContractCompileEnv,
				kPreNGBSLightingConsumerCompileEnv,
				kPreNGBSLightingDescriptorObserveEnv,
				kPreNGBSLightingVanillaBindEnv,
				PreNGEnvironment::kPreNGBSLightingVanillaDumpEnv,
				PreNGEnvironment::kPreNGDFLightVanillaDumpEnv,
				kPreNGDFCompositeDescriptorObserveEnv,
				kPreNGDFCompositeDescriptorCompileEnv,
				kPreNGDFCompositeResourceBindEnv,
				kPreNGDFCompositeSafeBindEnv,
				kPreNGDFCompositeFogSafeBindEnv,
				PreNGEnvironment::kPreNGDFCompositeVanillaDumpEnv);
		}
#else
		if (ShouldBindPostNGBSLightingLLFConsumerShader() || ShouldObservePostNGBSLightingDescriptors())
		{
			const auto lookupAddr = F4Runtime::PostNG::BS_SHADER_LOOKUP.address();
			PostNGShaderLookup::func = reinterpret_cast<decltype(PostNGShaderLookup::func)>(
				Detours::X64::DetourFunction(
					lookupAddr,
					reinterpret_cast<std::uintptr_t>(PostNGShaderLookup::thunk)));
			logger::info(
				"[BSShaderHooks] Detoured PostNG/AE shader lookup at 0x{:X}; original=0x{:X}; consumerBind={} descriptorObserve={}",
				lookupAddr,
				reinterpret_cast<std::uintptr_t>(PostNGShaderLookup::func),
				ShouldBindPostNGBSLightingLLFConsumerShader(),
				ShouldObservePostNGBSLightingDescriptors());
		}
		else
		{
			logger::info("[BSShaderHooks] PostNG/AE shader lookup detour held; set {}=1 for the visible consumer or {}=1 for descriptor observation",
				kPostNGBSLightingLLFBindEnv, kPostNGBSLightingDescriptorObserveEnv);
		}
#endif
	}

	void BSShaderHooks::OnFrame()
	{
		DrainPending();
	}
}
