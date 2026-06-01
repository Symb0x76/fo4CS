#include "Features/LightLimitFix.h"
#include <DirectXMath.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <Windows.h>
#include <cstdint>

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSFadeNode.h"
#else
#include "RE/Bethesda/BSFadeNode.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#else
#include "RE/Bethesda/TESObjectREFRs.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESBoundAnimObjects.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/N/NiAVObject.h"
#include "RE/N/NiBound.h"
#include "RE/N/NiColor.h"
#else
#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiBound.h"
#include "RE/NetImmerse/NiColor.h"
#endif

#include "SimpleIni.h"

#include <imgui.h>

namespace
{
	constexpr std::uint32_t kClusterMaxLights = 128;
	constexpr std::uint32_t kMaxLights = 1024;
#if defined(FALLOUT_PRE_NG)
	constexpr std::uint64_t kPreNGStableFrame = 5;
	constexpr bool kPreNGEnableInternalPointLightHook = false;
	constexpr const char* kPreNGPointLightHookOptInEnv = "FO4CS_LLF_PRENG_POINT_LIGHT_HOOK";
	constexpr const char* kPreNGStrictLightCBDiagnosticEnv = "FO4CS_LLF_PRENG_STRICT_CB_DIAG";
	constexpr const char* kPreNGStrictLightCBBindEnv = "FO4CS_LLF_PRENG_BIND_STRICT_CB";
	constexpr const char* kPreNGClusterSRVBindEnv = "FO4CS_LLF_PRENG_BIND_CLUSTER_SRVS";
	constexpr const char* kPreNGTraceLLFPixelEnv = "FO4CS_TRACE_LLF_PS";
#endif

	std::string GetShaderPath()
	{
		return "LightLimitFix\\";
	}

#pragma warning(push)
#pragma warning(disable: 4324)
	struct NiLightView : RE::NiAVObject
	{
		RE::NiColor amb;
		RE::NiColor diff;
		RE::NiColor spec;
		float dimmer;
		alignas(16) RE::NiBound modelBound;
		void* rendererData;
	};
#pragma warning(pop)

	static_assert(sizeof(NiLightView) == 0x170);
	static_assert(offsetof(NiLightView, diff) == 0x12C);
	static_assert(offsetof(NiLightView, modelBound) == 0x150);

	bool LogResourceFailure(const char* a_name, HRESULT a_hr)
	{
		logger::error("[LightLimitFix] {} failed (hr=0x{:08X})",
		              a_name, static_cast<std::uint32_t>(a_hr));
		return false;
	}

	bool IsFiniteMatrix(const DirectX::XMFLOAT4X4& a_matrix)
	{
		const auto* values = reinterpret_cast<const float*>(&a_matrix);
		for (std::size_t i = 0; i < 16; ++i) {
			if (!std::isfinite(values[i])) {
				return false;
			}
		}
		return true;
	}
#if defined(FALLOUT_PRE_NG)
	constexpr std::uintptr_t kPreNGStaticImageBase = 0x140000000ull;
	constexpr std::uintptr_t kPreNGBSLightingShaderSetupGeometryVA = 0x14289DD10ull;
	constexpr std::uintptr_t kPreNGPointLightCallVA = 0x14289E0BFull;
	constexpr std::uintptr_t kPreNGPointLightTargetVA = 0x14289F550ull;
	constexpr std::uintptr_t kPreNGBSLightingShaderVTableVA = 0x14309AAB8ull;
	constexpr std::uintptr_t kPreNGBSLightingShaderVFunc7VA = 0x14309AAF0ull;
	constexpr std::uint8_t kPreNGPointLightCallContext[] = {
		0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
		0xE8, 0x8C, 0x14, 0x00, 0x00
	};

	std::uintptr_t PreNGRuntimeAddress(std::uintptr_t a_staticVA)
	{
		return REL::Module::get().base() + (a_staticVA - kPreNGStaticImageBase);
	}

	bool IsReadableMemory(std::uintptr_t a_address, std::size_t a_size)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<const void*>(a_address), &mbi, sizeof(mbi)) == 0) {
			return false;
		}

		const auto regionBegin = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
		const auto regionEnd = regionBegin + mbi.RegionSize;
		const auto readEnd = a_address + a_size;
		if (mbi.State != MEM_COMMIT || a_address < regionBegin || readEnd > regionEnd) {
			return false;
		}

		return (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
	}

	constexpr std::uintptr_t kPreNGBSRenderPassSceneLightsOffset = 0x30;
	constexpr std::uintptr_t kPreNGBSRenderPassRawLightCountOffset = 0x50;
	constexpr std::uint32_t kPreNGBSRenderPassSceneLightFirstIndex = 1;
	constexpr std::uintptr_t kPreNGBSLightWrapperFadeOffset = 0x10;
	constexpr std::uintptr_t kPreNGBSLightWrapperNiLightOffset = 0xB8;
	constexpr std::uintptr_t kPreNGBSShadowLightMaskIndexOffset = 0x1B0;
	constexpr std::uint32_t kPreNGInvalidShadowLightMaskIndex = 255;
	constexpr std::uint32_t kPreNGMaxShadowLightMaskBits = 32;
	constexpr std::uintptr_t kPreNGNiLightWorldTranslateOffset = 0xA0;
	constexpr std::uintptr_t kPreNGNiLightDiffuseOffset = 0x12C;
	constexpr std::uintptr_t kPreNGNiLightRadiusOffset = 0x138;
	constexpr std::uintptr_t kPreNGNiLightDimmerOffset = 0x144;
	constexpr std::uintptr_t kPreNGShadowSceneNodeArrayVA = 0x146721B70ull;
	constexpr std::uintptr_t kPreNGShadowSceneNodeCurrentIndexVA = 0x146721C19ull;
	constexpr std::uint32_t kPreNGShadowSceneNodeArraySlots = 21;
	// FO4 keeps active lights in three buckets; +0x1A0/+0x1B8 are pending add/remove queues.
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveLightsOffset = 0x158;
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveLightsCountOffset = 0x168;
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveShadowLightsOffset = 0x170;
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveShadowLightsCountOffset = 0x180;
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveExtraLightsOffset = 0x188;
	constexpr std::uintptr_t kPreNGShadowSceneNodeActiveExtraLightsCountOffset = 0x198;
	constexpr std::uint32_t kPreNGMaxShadowSceneActiveLights = 512;
	constexpr float kPreNGLightContributionThreshold = 1.0e-4f;
	constexpr float kPreNGLightRadiusThreshold = 1.0e-4f;

	template <class T>
	bool ReadPreNGValue(std::uintptr_t a_address, T& a_value)
	{
		if (!IsReadableMemory(a_address, sizeof(T))) {
			return false;
		}

		std::memcpy(&a_value, reinterpret_cast<const void*>(a_address), sizeof(T));
		return true;
	}

	struct PreNGShadowSceneNodeRef
	{
		std::uintptr_t node{ 0 };
		std::uint8_t selectedIndex{ 0 };
		std::uint8_t currentIndex{ 0 };
		bool currentIndexRead{ false };
		bool usedFallback{ false };
	};

	PreNGShadowSceneNodeRef GetPreNGWorldShadowSceneNode()
	{
		PreNGShadowSceneNodeRef result{};
		const auto arrayBase = PreNGRuntimeAddress(kPreNGShadowSceneNodeArrayVA);

		std::uint8_t currentIndex = 0;
		if (ReadPreNGValue(PreNGRuntimeAddress(kPreNGShadowSceneNodeCurrentIndexVA), currentIndex)) {
			result.currentIndexRead = true;
			result.currentIndex = currentIndex;

			if (currentIndex < kPreNGShadowSceneNodeArraySlots) {
				std::uintptr_t indexedNode = 0;
				const auto indexedSlot = arrayBase + (sizeof(std::uintptr_t) * currentIndex);
				if (ReadPreNGValue(indexedSlot, indexedNode) && indexedNode != 0) {
					result.node = indexedNode;
					result.selectedIndex = currentIndex;
					return result;
				}
			}
		}

		std::uintptr_t fallbackNode = 0;
		if (ReadPreNGValue(arrayBase, fallbackNode)) {
			result.node = fallbackNode;
			result.selectedIndex = 0;
			result.usedFallback = result.currentIndexRead && result.currentIndex != 0;
		}

		return result;
	}

	enum class PreNGLightDecodeResult
	{
		Decoded,
		MissingWrapperData,
		InvalidNiLightData,
		NonContributingLightData
	};

	PreNGLightDecodeResult DecodePreNGBSLightWrapper(
		std::uintptr_t a_wrapperAddress,
		LightLimitFix::LightData& a_data,
		std::uintptr_t& a_niLightAddress,
		bool& a_shadowMaskUnreadable,
		bool& a_shadowMaskInvalid,
		std::uint32_t& a_shadowMaskBit)
	{
		a_data = {};
		a_niLightAddress = 0;
		a_shadowMaskUnreadable = false;
		a_shadowMaskInvalid = false;
		a_shadowMaskBit = 0;

		float wrapperFade = 1.0f;
		if (a_wrapperAddress == 0 ||
			!ReadPreNGValue(a_wrapperAddress + kPreNGBSLightWrapperFadeOffset, wrapperFade) ||
			!ReadPreNGValue(a_wrapperAddress + kPreNGBSLightWrapperNiLightOffset, a_niLightAddress) ||
			a_niLightAddress == 0 ||
			!std::isfinite(wrapperFade)) {
			return PreNGLightDecodeResult::MissingWrapperData;
		}

		float diffuseR = 0.0f;
		float diffuseG = 0.0f;
		float diffuseB = 0.0f;
		float radius = 0.0f;
		float dimmer = 0.0f;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		if (!ReadPreNGValue(a_niLightAddress + kPreNGNiLightDiffuseOffset, diffuseR) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightDiffuseOffset + sizeof(float), diffuseG) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightDiffuseOffset + (2 * sizeof(float)), diffuseB) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightRadiusOffset, radius) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightDimmerOffset, dimmer) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightWorldTranslateOffset, positionX) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightWorldTranslateOffset + sizeof(float), positionY) ||
			!ReadPreNGValue(a_niLightAddress + kPreNGNiLightWorldTranslateOffset + (2 * sizeof(float)), positionZ) ||
			!std::isfinite(diffuseR) ||
			!std::isfinite(diffuseG) ||
			!std::isfinite(diffuseB) ||
			!std::isfinite(radius) ||
			!std::isfinite(dimmer) ||
			!std::isfinite(positionX) ||
			!std::isfinite(positionY) ||
			!std::isfinite(positionZ) ||
			radius <= 0.0f) {
			return PreNGLightDecodeResult::InvalidNiLightData;
		}

		const float fade = dimmer * wrapperFade;
		const float contribution = (diffuseR + diffuseG + diffuseB) * fade;
		if (radius <= kPreNGLightRadiusThreshold || contribution <= kPreNGLightContributionThreshold) {
			return PreNGLightDecodeResult::NonContributingLightData;
		}

		a_data.color.x = diffuseR;
		a_data.color.y = diffuseG;
		a_data.color.z = diffuseB;
		a_data.fade = fade;
		a_data.radius = radius;
		a_data.invRadius = a_data.radius > 0.0f ? 1.0f / a_data.radius : 0.0f;
		a_data.positionWS[0].data.x = positionX;
		a_data.positionWS[0].data.y = positionY;
		a_data.positionWS[0].data.z = positionZ;
		a_data.lightFlags = static_cast<std::uint32_t>(LightLimitFix::LightFlags::Initialised);

		std::uint32_t shadowMaskIndex = kPreNGInvalidShadowLightMaskIndex;
		if (ReadPreNGValue(a_wrapperAddress + kPreNGBSShadowLightMaskIndexOffset, shadowMaskIndex)) {
			if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex && shadowMaskIndex < kPreNGMaxShadowLightMaskBits) {
				a_data.lightFlags |= static_cast<std::uint32_t>(LightLimitFix::LightFlags::Shadow);
				a_data.shadowLightIndex = shadowMaskIndex;
				a_shadowMaskBit = (1u << shadowMaskIndex);
			} else if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex) {
				a_shadowMaskInvalid = true;
			}
		} else {
			a_shadowMaskUnreadable = true;
		}

		return PreNGLightDecodeResult::Decoded;
	}

	struct PreNGCallPatchState
	{
		bool readable = false;
		std::uint8_t opcode = 0;
		std::int32_t rel32 = 0;
		std::uintptr_t callTarget = 0;
	};

	PreNGCallPatchState ReadPreNGCallPatch(std::uintptr_t a_call)
	{
		PreNGCallPatchState state{};
		if (!IsReadableMemory(a_call, 5)) {
			return state;
		}

		const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_call);
		state.readable = true;
		state.opcode = bytes[0];
		std::memcpy(&state.rel32, bytes + 1, sizeof(state.rel32));
		state.callTarget = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(a_call + 5) + state.rel32);
		return state;
	}

	std::uintptr_t ResolvePreNGAbsoluteJumpTarget(std::uintptr_t a_address)
	{
		std::uint8_t bytes[14]{};
		if (!IsReadableMemory(a_address, sizeof(bytes))) {
			return 0;
		}

		std::memcpy(bytes, reinterpret_cast<const void*>(a_address), sizeof(bytes));
		if (bytes[0] != 0xFF || bytes[1] != 0x25) {
			return 0;
		}

		std::int32_t disp = 0;
		std::memcpy(&disp, bytes + 2, sizeof(disp));
		const auto slot = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(a_address + 6) + disp);

		std::uintptr_t target = 0;
		if (!ReadPreNGValue(slot, target)) {
			return 0;
		}

		return target;
	}


	bool ValidatePreNGPointLightCallsite()
	{
		const auto imageBase = static_cast<std::uintptr_t>(REL::Module::get().base());
		const auto runtimeSetup = PreNGRuntimeAddress(kPreNGBSLightingShaderSetupGeometryVA);
		const auto runtimeCall = PreNGRuntimeAddress(kPreNGPointLightCallVA);
		const auto runtimeCallContext = runtimeCall - 8;
		const auto runtimeTarget = PreNGRuntimeAddress(kPreNGPointLightTargetVA);
		const auto runtimeVTable = PreNGRuntimeAddress(kPreNGBSLightingShaderVTableVA);
		const auto runtimeVFuncEntry = PreNGRuntimeAddress(kPreNGBSLightingShaderVFunc7VA);

		const bool vfuncReadable = IsReadableMemory(runtimeVFuncEntry, sizeof(std::uintptr_t));
		const bool callReadable = IsReadableMemory(runtimeCallContext, sizeof(kPreNGPointLightCallContext));
		if (!vfuncReadable || !callReadable) {
			logger::warn(
				"[LightLimitFix] PreNG point-light callsite validation skipped: unreadable memory base=0x{:X} vfuncReadable={} callReadable={} vfunc[7]=0x{:X} callContext=0x{:X}",
				imageBase, vfuncReadable, callReadable, runtimeVFuncEntry, runtimeCallContext);
			return false;
		}

		const auto observedVFunc = *reinterpret_cast<const std::uintptr_t*>(runtimeVFuncEntry);
		const auto* callBytes = reinterpret_cast<const std::uint8_t*>(runtimeCall);
		std::int32_t callRel = 0;
		std::memcpy(&callRel, callBytes + 1, sizeof(callRel));
		const auto observedTarget = static_cast<std::uintptr_t>(
			static_cast<std::intptr_t>(runtimeCall + 5) + callRel);
		const bool contextMatches = std::memcmp(
			reinterpret_cast<const void*>(runtimeCallContext),
			kPreNGPointLightCallContext,
			sizeof(kPreNGPointLightCallContext)) == 0;

		if (observedVFunc == runtimeSetup && callBytes[0] == 0xE8 && observedTarget == runtimeTarget && contextMatches) {
			logger::info(
				"[LightLimitFix] PreNG point-light callsite validated base=0x{:X} setup=0x{:X} call=0x{:X} target=0x{:X} vtable=0x{:X} vfunc[7]=0x{:X}->0x{:X}",
				imageBase, runtimeSetup, runtimeCall, observedTarget, runtimeVTable, runtimeVFuncEntry, observedVFunc);
			return true;
		}

		logger::warn(
			"[LightLimitFix] PreNG point-light callsite mismatch base=0x{:X} vfunc[7]=0x{:X}->0x{:X} expectedSetup=0x{:X} call=0x{:X} opcode=0x{:02X} rel32=0x{:08X} observedTarget=0x{:X} expectedTarget=0x{:X} contextMatch={}",
			imageBase,
			runtimeVFuncEntry,
			observedVFunc,
			runtimeSetup,
			runtimeCall,
			static_cast<std::uint32_t>(callBytes[0]),
			static_cast<std::uint32_t>(callRel),
			observedTarget,
			runtimeTarget,
			contextMatches);
		return false;
	}

	bool IsTruthyEnvironmentValue(const char* a_value)
	{
		return std::strcmp(a_value, "1") == 0 ||
		       std::strcmp(a_value, "true") == 0 ||
		       std::strcmp(a_value, "TRUE") == 0 ||
		       std::strcmp(a_value, "on") == 0 ||
		       std::strcmp(a_value, "ON") == 0;
	}

	enum class EnvironmentSwitchSource
	{
		kNone,
		kProcess,
		kUserRegistry,
		kMachineRegistry
	};

	struct EnvironmentSwitchState
	{
		bool enabled = false;
		EnvironmentSwitchSource source = EnvironmentSwitchSource::kNone;
	};

	const char* EnvironmentSwitchSourceName(EnvironmentSwitchSource a_source)
	{
		switch (a_source) {
		case EnvironmentSwitchSource::kProcess:
			return "process";
		case EnvironmentSwitchSource::kUserRegistry:
			return "user-reg";
		case EnvironmentSwitchSource::kMachineRegistry:
			return "machine-reg";
		default:
			return "none";
		}
	}

	bool ReadRegistryEnvironmentValue(
		HKEY a_root,
		const char* a_subKey,
		const char* a_name,
		char (&a_value)[16])
	{
		DWORD type = 0;
		DWORD size = static_cast<DWORD>(sizeof(a_value));
		const auto result = RegGetValueA(
			a_root,
			a_subKey,
			a_name,
			RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
			&type,
			a_value,
			&size);
		if (result != ERROR_SUCCESS || size == 0) {
			return false;
		}

		a_value[sizeof(a_value) - 1] = '\0';
		return true;
	}

	EnvironmentSwitchState ReadEnvironmentSwitch(const char* a_name)
	{
		char value[16]{};
		SetLastError(ERROR_SUCCESS);
		const auto length = GetEnvironmentVariableA(
			a_name,
			value,
			static_cast<DWORD>(sizeof(value)));
		if (length > 0) {
			return {
				length < sizeof(value) && IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kProcess
			};
		}
		if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
			return { false, EnvironmentSwitchSource::kProcess };
		}

		if (ReadRegistryEnvironmentValue(HKEY_CURRENT_USER, "Environment", a_name, value)) {
			return {
				IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kUserRegistry
			};
		}

		if (ReadRegistryEnvironmentValue(
				HKEY_LOCAL_MACHINE,
				"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
				a_name,
				value)) {
			return {
				IsTruthyEnvironmentValue(value),
				EnvironmentSwitchSource::kMachineRegistry
			};
		}

		return {};
	}

	bool IsTruthyEnvironmentSwitch(const char* a_name)
	{
		return ReadEnvironmentSwitch(a_name).enabled;
	}

	void LogPreNGDiagnosticEnvironmentSnapshot()
	{
		static bool logged = false;
		if (logged) {
			return;
		}
		logged = true;

		const auto hookState = ReadEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
		const auto strictCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
		const auto bindCBState = ReadEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
		const auto bindClusterSRVState = ReadEnvironmentSwitch(kPreNGClusterSRVBindEnv);
		const auto tracePSState = ReadEnvironmentSwitch(kPreNGTraceLLFPixelEnv);

		logger::info(
			"[LightLimitFix] PreNG diagnostic env snapshot {}={} {}={} {}={} {}={} {}={} sources hook={} strictCB={} bindCB={} bindSRV={} tracePS={}",
			kPreNGPointLightHookOptInEnv,
			hookState.enabled ? "on" : "off",
			kPreNGStrictLightCBDiagnosticEnv,
			strictCBState.enabled ? "on" : "off",
			kPreNGStrictLightCBBindEnv,
			bindCBState.enabled ? "on" : "off",
			kPreNGClusterSRVBindEnv,
			bindClusterSRVState.enabled ? "on" : "off",
			kPreNGTraceLLFPixelEnv,
			tracePSState.enabled ? "on" : "off",
			EnvironmentSwitchSourceName(hookState.source),
			EnvironmentSwitchSourceName(strictCBState.source),
			EnvironmentSwitchSourceName(bindCBState.source),
			EnvironmentSwitchSourceName(bindClusterSRVState.source),
			EnvironmentSwitchSourceName(tracePSState.source));
	}

	bool ShouldInstallPreNGInternalPointLightHook()
	{
		if constexpr (kPreNGEnableInternalPointLightHook) {
			return true;
		}

		return IsTruthyEnvironmentSwitch(kPreNGPointLightHookOptInEnv);
	}

	bool ShouldUpdatePreNGStrictLightCB()
	{
		return IsTruthyEnvironmentSwitch(kPreNGStrictLightCBDiagnosticEnv);
	}

	bool ShouldBindPreNGStrictLightCB()
	{
		return IsTruthyEnvironmentSwitch(kPreNGStrictLightCBBindEnv);
	}

	bool ShouldBindPreNGClusterSRVs()
	{
		return IsTruthyEnvironmentSwitch(kPreNGClusterSRVBindEnv);
	}

	struct PreNGPointLightSetupCall
	{
		static std::int64_t thunk(
			std::uintptr_t a_pixelShader,
			RE::BSRenderPass* a_pass,
			DirectX::XMMATRIX* a_transform,
			std::int32_t a_lightCount,
			std::int32_t a_shadowArg,
			float a_worldScale,
			std::int32_t a_unknown)
		{
			static std::atomic_uint32_t callCount = 0;
			const auto callIndex = ++callCount;
			std::uint32_t collected = 0;
			std::uint32_t strict = 0;
			std::uint32_t strictShadowBitMask = 0;
			bool strictCBUploaded = false;
			bool strictCBBound = false;
			bool clusterSRVsBound = false;
			std::uint32_t requestedLightCount = 0;
			LightLimitFix* self = nullptr;
			if (globals::features::lightLimitFix.loaded && a_lightCount > 0) {
				self = &globals::features::lightLimitFix;
				requestedLightCount = static_cast<std::uint32_t>(a_lightCount);
				collected = self->CollectLightsFromPreNGSceneLights(
					a_pass,
					requestedLightCount,
					a_shadowArg > 0 ? static_cast<std::uint32_t>(a_shadowArg) : 0);
				strict = self->currentStrictLightCount;
				strictShadowBitMask = self->strictLightDataTemp.ShadowBitMask;
			}

			const auto result = func(a_pixelShader, a_pass, a_transform, a_lightCount, a_shadowArg, a_worldScale, a_unknown);

			if (self) {
				strictCBUploaded = self->UploadPreNGStrictLightDataDiagnostic();
				strictCBBound = self->BindPreNGStrictLightDataCBToPixelShader(a_pass, requestedLightCount, strictCBUploaded);
				clusterSRVsBound = self->BindPreNGClusterSRVsToPixelShader(a_pass, requestedLightCount, strictCBBound);
				strict = self->currentStrictLightCount;
				strictShadowBitMask = self->strictLightDataTemp.ShadowBitMask;
			}

			if (callIndex <= 8 || callIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG internal point-light hook reached calls={} pixelShader=0x{:X} pass=0x{:X} requested={} collected={} strict={} strictCB={} b3={} t35t37={} bindOrder=post-vanilla shadowArg={} strictShadowMask=0x{:08X} worldScale={:.3f} unknown={}",
					callIndex,
					a_pixelShader,
					reinterpret_cast<std::uintptr_t>(a_pass),
					a_lightCount,
					collected,
					strict,
					strictCBUploaded ? "uploaded" : "held",
					strictCBBound ? "bound" : "held",
					clusterSRVsBound ? "bound" : "held",
					a_shadowArg,
					strictShadowBitMask,
					a_worldScale,
					a_unknown);
			}

			return result;
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	bool VerifyPreNGPointLightHookPatch(std::uintptr_t a_runtimeCall)
	{
		const auto patch = ReadPreNGCallPatch(a_runtimeCall);
		const auto branchTarget = patch.readable ? ResolvePreNGAbsoluteJumpTarget(patch.callTarget) : 0;
		const auto thunkTarget = reinterpret_cast<std::uintptr_t>(&PreNGPointLightSetupCall::thunk);
		const auto originalTarget = PreNGRuntimeAddress(kPreNGPointLightTargetVA);
		const bool directToThunk = patch.callTarget == thunkTarget;
		const bool branchToThunk = branchTarget == thunkTarget;
		const bool verified = patch.readable && patch.opcode == 0xE8 && (directToThunk || branchToThunk);

		if (verified) {
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook patch verified call=0x{:X} callTarget=0x{:X} branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
				a_runtimeCall,
				patch.callTarget,
				branchTarget,
				thunkTarget,
				originalTarget,
				static_cast<std::uint32_t>(patch.rel32));
		} else {
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook patch verification failed call=0x{:X} readable={} opcode=0x{:02X} callTarget=0x{:X} branchTarget=0x{:X} thunk=0x{:X} original=0x{:X} rel32=0x{:08X}",
				a_runtimeCall,
				patch.readable,
				static_cast<std::uint32_t>(patch.opcode),
				patch.callTarget,
				branchTarget,
				thunkTarget,
				originalTarget,
				static_cast<std::uint32_t>(patch.rel32));
		}

		return verified;
	}


	enum class PreNGPointLightHookState
	{
		Failed,
		Prepared,
		Installed,
		InstalledUnverified
	};

	PreNGPointLightHookState PreparePreNGPointLightHook()
	{
		if (!ValidatePreNGPointLightCallsite()) {
			logger::warn("[LightLimitFix] PreNG internal point-light hook not prepared; callsite validation failed");
			return PreNGPointLightHookState::Failed;
		}

		const auto runtimeCall = PreNGRuntimeAddress(kPreNGPointLightCallVA);
		if (ShouldInstallPreNGInternalPointLightHook()) {
			stl::write_thunk_call<PreNGPointLightSetupCall>(runtimeCall);
			const bool patchVerified = VerifyPreNGPointLightHookPatch(runtimeCall);
			logger::warn(
				"[LightLimitFix] PreNG internal point-light hook installed at call=0x{:X}; diagnostic opt-in is active; patchVerified={}",
				runtimeCall,
				patchVerified);
			return patchVerified ? PreNGPointLightHookState::Installed : PreNGPointLightHookState::InstalledUnverified;
		}

		logger::info(
			"[LightLimitFix] PreNG internal point-light hook prepared at call=0x{:X}; install gate is off (set {}=1 for diagnostic activation)",
			runtimeCall,
			kPreNGPointLightHookOptInEnv);
		return PreNGPointLightHookState::Prepared;
	}
#endif
}

void LightLimitFix::LoadSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	const auto path = GetSettingsPath();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		ini.LoadFile(path.string().c_str());
	}

	settings.EnableLightsVisualisation = ini.GetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	settings.LightsVisualisationMode = static_cast<std::uint32_t>(
		ini.GetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode)));
}

void LightLimitFix::SaveSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	ini.SetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	ini.SetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode));

	const auto path = GetSettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	ini.SaveFile(path.string().c_str());
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

void LightLimitFix::DrawSettings()
{
	if (ImGui::CollapsingHeader("Light Limit Fix")) {
		int changed = 0;
		changed |= ImGui::Checkbox("Lights Visualisation", &settings.EnableLightsVisualisation) ? 1 : 0;

		const char* modes[] = { "Clusters", "Lights", "Both" };
		int mode = static_cast<int>(settings.LightsVisualisationMode);
		if (ImGui::Combo("Visualisation Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
			settings.LightsVisualisationMode = static_cast<std::uint32_t>(std::clamp(mode, 0, IM_ARRAYSIZE(modes) - 1));
			changed = 1;
		}

		ImGui::Text("Lights: %u", currentLightCount);
		ImGui::Text("Strict lights: %u", currentStrictLightCount);
		ImGui::Text("Clusters: %ux%ux%u", clusterSize[0], clusterSize[1], clusterSize[2]);

		if (changed) {
			SaveSettings();
		}
	}
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.CameraNear = CameraNear;
	perFrame.CameraFar = CameraFar;
	perFrame.ClusterSize[0] = clusterSize[0];
	perFrame.ClusterSize[1] = clusterSize[1];
	perFrame.ClusterSize[2] = clusterSize[2];
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
	if (!device) {
		logger::warn("[LightLimitFix] SetupResources: D3D11 device not available");
		return;
	}

	// com_ptr auto-releases previous resources on reassignment — no manual ClearShaderCache needed

	auto shaderPath = GetShaderPath();

	auto compileOrLoad = [&](const char* a_name, winrt::com_ptr<ID3D11ComputeShader>& a_out) {
		auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
			shaderPath + a_name);
		if (!compiled) {
			logger::warn("[LightLimitFix] Failed to compile: {}{}", shaderPath, a_name);
			return false;
		}
		const auto hr = device->CreateComputeShader(compiled->data(), compiled->size(), nullptr, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	if (!compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS) ||
	    !compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS)) {
		logger::warn("[LightLimitFix] GPU resources pending - compute shaders not available");
		return;
	}

	auto createBuffer = [&](const char* a_name, const D3D11_BUFFER_DESC& a_desc,
	                        winrt::com_ptr<ID3D11Buffer>& a_out) {
		const auto hr = device->CreateBuffer(&a_desc, nullptr, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	auto createSRV = [&](const char* a_name, ID3D11Resource* a_resource,
	                     const D3D11_SHADER_RESOURCE_VIEW_DESC& a_desc,
	                     winrt::com_ptr<ID3D11ShaderResourceView>& a_out) {
		const auto hr = device->CreateShaderResourceView(a_resource, &a_desc, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	auto createUAV = [&](const char* a_name, ID3D11Resource* a_resource,
	                     const D3D11_UNORDERED_ACCESS_VIEW_DESC& a_desc,
	                     winrt::com_ptr<ID3D11UnorderedAccessView>& a_out) {
		const auto hr = device->CreateUnorderedAccessView(a_resource, &a_desc, a_out.put());
		if (FAILED(hr)) {
			return LogResourceFailure(a_name, hr);
		}
		return true;
	};

	// Constant buffers
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightBuildingCB);
		if (!createBuffer("CreateBuffer(lightBuildingCB)", desc, lightBuildingCB)) return;
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightCullingCB);
		if (!createBuffer("CreateBuffer(lightCullingCB)", desc, lightCullingCB)) return;
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(StrictLightDataCB);
		if (!createBuffer("CreateBuffer(strictLightDataCB)", desc, strictLightDataCB)) {
			logger::warn("[LightLimitFix] Strict light diagnostic CB unavailable; continuing without it");
		}
	}

	// Lights structured buffer
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightData);
		desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
		if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = kMaxLights;
		if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffer.get(), srvDesc, lightsSRV)) return;
	}

	// Clusters structured buffer
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(ClusterAABB);
		desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(ClusterAABB));
		if (!createBuffer("CreateBuffer(clustersBuffer)", desc, clustersBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		if (!createSRV("CreateShaderResourceView(clustersSRV)", clustersBuffer.get(), srvDesc, clustersSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		if (!createUAV("CreateUnorderedAccessView(clustersUAV)", clustersBuffer.get(), uavDesc, clustersUAV)) return;
	}

	// Light index counter
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = sizeof(std::uint32_t);
		if (!createBuffer("CreateBuffer(lightIndexCounterBuffer)", desc, lightIndexCounterBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = 1;
		if (!createSRV("CreateShaderResourceView(lightIndexCounterSRV)", lightIndexCounterBuffer.get(), srvDesc, lightIndexCounterSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = 1;
		if (!createUAV("CreateUnorderedAccessView(lightIndexCounterUAV)", lightIndexCounterBuffer.get(), uavDesc, lightIndexCounterUAV)) return;
	}

	// Light index list
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = static_cast<UINT>(clusterCount * kClusterMaxLights * sizeof(std::uint32_t));
		if (!createBuffer("CreateBuffer(lightIndexListBuffer)", desc, lightIndexListBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		if (!createSRV("CreateShaderResourceView(lightIndexListSRV)", lightIndexListBuffer.get(), srvDesc, lightIndexListSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		if (!createUAV("CreateUnorderedAccessView(lightIndexListUAV)", lightIndexListBuffer.get(), uavDesc, lightIndexListUAV)) return;
	}

	// Light grid
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightGrid);
		desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(LightGrid));
		if (!createBuffer("CreateBuffer(lightGridBuffer)", desc, lightGridBuffer)) return;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		if (!createSRV("CreateShaderResourceView(lightGridSRV)", lightGridBuffer.get(), srvDesc, lightGridSRV)) return;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		if (!createUAV("CreateUnorderedAccessView(lightGridUAV)", lightGridBuffer.get(), uavDesc, lightGridUAV)) return;
	}

	if (!HasResources()) {
		logger::error("[LightLimitFix] GPU resource creation finished with incomplete resources");
		return;
	}

	logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
	             clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_AE)
	auto* setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
	if (setting) {
		setting->SetInt(0x7FFFFFFF);
		logger::info("[LightLimitFix] Unlocked magic light limit");
	}
#endif
}

void LightLimitFix::PostPostLoad()
{
#if defined(FALLOUT_PRE_NG)
	LogPreNGDiagnosticEnvironmentSnapshot();
	const auto pointLightHookState = PreparePreNGPointLightHook();
	switch (pointLightHookState) {
	case PreNGPointLightHookState::Installed:
		logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook diagnostic active and patch verified");
		break;
	case PreNGPointLightHookState::InstalledUnverified:
		logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook patch unverified; diagnostic evidence is not trusted");
		break;
	case PreNGPointLightHookState::Prepared:
		logger::info("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook remains gated");
		break;
	case PreNGPointLightHookState::Failed:
		logger::warn("[LightLimitFix] PreNG SetupGeometry hooks held; scene-light decoder prepared, internal point-light hook not prepared");
		break;
	}
	return;
#else
	Hooks::Install();
#endif
}

void LightLimitFix::Prepass()
{
	const auto frameNumber = ++diagFrameCounter;

#if defined(FALLOUT_PRE_NG)
	auto* runtime = CommunityShaders::Runtime::GetSingleton();
	if (!runtime) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: runtime unavailable");
		}
		return;
	}
	if (runtime->GetFrameCount() < kPreNGStableFrame) {
		if (frameNumber == 1) {
			logger::info("[LightLimitFix] PreNG Prepass waiting for stable frame gate ({})", kPreNGStableFrame);
		}
		return;
	}
#endif

	if (!HasResources()) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: GPU resources are incomplete");
		}
		return;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: renderer data unavailable");
		}
		return;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: D3D11 context unavailable");
		}
		return;
	}

	auto clearComputeBindings = [&] {
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[3]{};
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
		ID3D11Buffer* nullCB = nullptr;
		context->CSSetConstantBuffers(0, 1, &nullCB);
		context->CSSetShader(nullptr, nullptr, 0);
	};
	auto clearPixelClusterSRVs = [&] {
		ID3D11ShaderResourceView* nullSRVs[3]{};
		context->PSSetShaderResources(35, ARRAYSIZE(nullSRVs), nullSRVs);
	};

	const auto& gState = RE::BSGraphics::State::GetSingleton();
	const auto& camView = gState.cameraState.camViewData;

	DirectX::XMFLOAT4X4 projInvTransposed;
	{
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.projMat));
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
	}
	if (!IsFiniteMatrix(projInvTransposed)) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: camera projection matrix is not invertible");
		}
		return;
	}

	DirectX::XMFLOAT4X4 viewTransposed;
	{
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
	}
	if (!IsFiniteMatrix(viewTransposed)) {
		if (frameNumber == 1 || frameNumber % 300 == 0) {
			logger::warn("[LightLimitFix] Prepass skipped: camera view matrix is invalid");
		}
		return;
	}

#if defined(FALLOUT_PRE_NG)
	std::vector<LightData> preNGSceneLightFallback;
	preNGSceneLightFallback.swap(frameLights);
	const auto preNGSceneLightFallbackStrictData = strictLightDataTemp;
	const auto preNGSceneLightFallbackStrictCount = currentStrictLightCount;

	seenLights.clear();
	seenThisPass.clear();

	if (CollectLightsFromPreNGShadowScene() == 0) {
		if (!preNGSceneLightFallback.empty()) {
			frameLights.swap(preNGSceneLightFallback);
			strictLightDataTemp = preNGSceneLightFallbackStrictData;
			currentStrictLightCount = preNGSceneLightFallbackStrictCount;

			static std::atomic_uint32_t fallbackUseCount = 0;
			const auto fallbackIndex = ++fallbackUseCount;
			if (fallbackIndex <= 8 || fallbackIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG scene-light fallback feeds clustered prepass uses={} lights={} strict={} shadowMask=0x{:08X}",
					fallbackIndex,
					static_cast<std::uint32_t>(frameLights.size()),
					currentStrictLightCount,
					strictLightDataTemp.ShadowBitMask);
			}
		} else {
			CollectLightsFromScene();
		}
	}
#else
	if (!seenLights.empty()) {
		CollectLightsFromBSLight();
	} else {
		CollectLightsFromScene();
	}
#endif

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());
	clearPixelClusterSRVs();
	if (currentLightCount > 0) {
		const auto lightUploadBytes = static_cast<UINT>(currentLightCount * sizeof(LightData));
		D3D11_BOX lightUploadBox{};
		lightUploadBox.right = lightUploadBytes;
		lightUploadBox.bottom = 1;
		lightUploadBox.back = 1;
		context->UpdateSubresource(lightsBuffer.get(), 0, &lightUploadBox, frameLights.data(), lightUploadBytes, 0);
	}

#if defined(FALLOUT_PRE_NG)
	if (currentLightCount > 0) {
		static std::atomic_uint32_t nonZeroClusterUploadCount = 0;
		const auto uploadIndex = ++nonZeroClusterUploadCount;
		if (uploadIndex <= 8 || uploadIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG clustered prepass uploaded uploads={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
				uploadIndex,
				frameNumber,
				currentLightCount,
				currentStrictLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask);
		}
	}
#endif

	if (frameNumber % 300 == 0) {
		logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}",
		             frameNumber, currentLightCount,
		             clusterSize[0], clusterSize[1], clusterSize[2],
		             CameraNear, CameraFar);
	}

	seenLights.clear();
	seenCBHashes.clear();
	frameLights.clear();

	const UINT counterReset[4] = { 0, 0, 0, 0 };
	context->ClearUnorderedAccessViewUint(lightIndexCounterUAV.get(), counterReset);

	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		const auto hr = context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) {
			LogResourceFailure("Map(lightBuildingCB)", hr);
			clearComputeBindings();
			return;
		}
		auto* cb = static_cast<LightBuildingCB*>(mapped.pData);
		cb->LightsNear = CameraNear;
		cb->LightsFar = CameraFar;
		cb->pad0[0] = cb->pad0[1] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));
		context->Unmap(lightBuildingCB.get(), 0);

		context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
		ID3D11Buffer* cbPtr = lightBuildingCB.get();
		context->CSSetConstantBuffers(0, 1, &cbPtr);
		ID3D11UnorderedAccessView* buildingUAVs[] = { clustersUAV.get() };
		context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
	}

	clearComputeBindings();

	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		const auto hr = context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) {
			LogResourceFailure("Map(lightCullingCB)", hr);
			clearComputeBindings();
			return;
		}
		auto* cb = static_cast<LightCullingCB*>(mapped.pData);
		cb->LightCount = currentLightCount;
		cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraView, &viewTransposed, sizeof(viewTransposed));
		context->Unmap(lightCullingCB.get(), 0);

		ID3D11ShaderResourceView* cullingSRVs[] = { clustersSRV.get(), lightsSRV.get() };
		context->CSSetShaderResources(0, 2, cullingSRVs);

		ID3D11UnorderedAccessView* cullingUAVs[] = { lightIndexCounterUAV.get(), lightIndexListUAV.get(), lightGridUAV.get() };
		context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

		context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
		ID3D11Buffer* cullCBPtr = lightCullingCB.get();
		context->CSSetConstantBuffers(0, 1, &cullCBPtr);

		context->Dispatch(
			(clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
			(clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
			(clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
	}

	clearComputeBindings();

#if defined(FALLOUT_PRE_NG)
	bool prepassStrictCBUploaded = UploadPreNGStrictLightDataDiagnostic();
	const bool prepassStrictCBBound = BindPreNGStrictLightDataCBToPixelShader(nullptr, currentLightCount, prepassStrictCBUploaded);
	if (prepassStrictCBBound) {
		static std::atomic_uint32_t prepassStrictCBBindCount = 0;
		const auto prepassStrictCBBindIndex = ++prepassStrictCBBindCount;
		if (prepassStrictCBBindIndex <= 8 || prepassStrictCBBindIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG strict-light CB bound to PS b3 from Prepass (Skyrim parity) binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X} uploadedBeforeBind={}",
				prepassStrictCBBindIndex,
				frameNumber,
				currentLightCount,
				currentStrictLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask,
				prepassStrictCBUploaded);
		}
	}
	if (ShouldBindPreNGClusterSRVs()) {
		ID3D11ShaderResourceView* views[3]{
			lightsSRV.get(),
			lightIndexListSRV.get(),
			lightGridSRV.get()
		};
		context->PSSetShaderResources(35, ARRAYSIZE(views), views);

		static std::atomic_uint32_t prepassBindCount = 0;
		const auto prepassBindIndex = ++prepassBindCount;
		if (prepassBindIndex <= 8 || prepassBindIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 from Prepass (Skyrim parity) binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
				prepassBindIndex,
				frameNumber,
				currentLightCount,
				currentStrictLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask);
		}

		if (currentLightCount > 0) {
			static std::atomic_uint32_t nonZeroPrepassBindCount = 0;
			const auto nonZeroPrepassBindIndex = ++nonZeroPrepassBindCount;
			if (nonZeroPrepassBindIndex <= 8 || nonZeroPrepassBindIndex % 512 == 0) {
				logger::info(
					"[LightLimitFix] PreNG cluster SRVs Prepass nonzero bind proof nonzeroBinds={} binds={} frame={} lights={} strict={} clusters={} shadowMask=0x{:08X}",
					nonZeroPrepassBindIndex,
					prepassBindIndex,
					frameNumber,
					currentLightCount,
					currentStrictLightCount,
					clusterSize[0] * clusterSize[1] * clusterSize[2],
					strictLightDataTemp.ShadowBitMask);
			}
		}
	} else {
		static bool loggedPreNGBindingHold = false;
		if (!loggedPreNGBindingHold) {
			logger::info("[LightLimitFix] PreNG compute Prepass active; Skyrim-style PS t35-t37 Prepass binding is gated by FO4CS_LLF_PRENG_BIND_CLUSTER_SRVS, strict CB b3 Prepass binding is gated by FO4CS_LLF_PRENG_BIND_STRICT_CB");
			loggedPreNGBindingHold = true;
		}
	}
#else
	if (frameNumber >= 3 && currentLightCount > 0) {
		ID3D11ShaderResourceView* views[3]{
			lightsSRV.get(),
			lightIndexListSRV.get(),
			lightGridSRV.get()
		};
		context->PSSetShaderResources(35, ARRAYSIZE(views), views);

		if (frameNumber % 300 == 0) {
			logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)",
			             currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
		}
	}
#endif
}

bool LightLimitFix::HasResources() const
{
	return clusterBuildingCS &&
	       clusterCullingCS &&
	       lightBuildingCB &&
	       lightCullingCB &&
	       lightsBuffer &&
	       lightsSRV &&
	       clustersBuffer &&
	       clustersSRV &&
	       clustersUAV &&
	       lightIndexCounterBuffer &&
	       lightIndexCounterSRV &&
	       lightIndexCounterUAV &&
	       lightIndexListBuffer &&
	       lightIndexListSRV &&
	       lightIndexListUAV &&
	       lightGridBuffer &&
	       lightGridSRV &&
	       lightGridUAV;
}

void LightLimitFix::Reset()
{
	if (!HasResources()) return;

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) return;

	ID3D11ShaderResourceView* nullViews[3]{};
	context->PSSetShaderResources(35, 3, nullViews);
}

void LightLimitFix::CollectLightsFromPass(RE::BSRenderPass* a_pass)
{
	if (!a_pass) return;

	auto* shaderProp = *reinterpret_cast<RE::BSShaderProperty**>(reinterpret_cast<std::uintptr_t>(a_pass) + 0x10);
	if (!shaderProp) return;

	auto* fadeNode = *reinterpret_cast<RE::BSFadeNode**>(reinterpret_cast<std::uintptr_t>(shaderProp) + 0x48);
	if (!fadeNode) return;

	auto* lightData = reinterpret_cast<RE::BSShaderPropertyLightData*>(reinterpret_cast<std::uintptr_t>(fadeNode) + 0x140);
	if (lightData->lightList.empty()) return;

	for (auto* light : lightData->lightList) {
		if (!light || seenLights.contains(light)) continue;
		seenLights.insert(light);
		seenThisPass.push_back(light);
	}
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::CollectLightsFromPreNGSceneLights(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, std::uint32_t a_shadowArg)
{
	strictLightDataTemp = {};
	strictLightDataTemp.RoomIndex = -1;
	currentStrictLightCount = 0;

	if (!a_pass) {
		return 0;
	}

	const auto passAddress = reinterpret_cast<std::uintptr_t>(a_pass);

	std::uintptr_t sceneLightsAddress = 0;
	std::uint8_t rawLightCount = 0;
	if (!ReadPreNGValue(passAddress + kPreNGBSRenderPassSceneLightsOffset, sceneLightsAddress) ||
		!ReadPreNGValue(passAddress + kPreNGBSRenderPassRawLightCountOffset, rawLightCount) ||
		sceneLightsAddress == 0 ||
		rawLightCount <= kPreNGBSRenderPassSceneLightFirstIndex) {
		return 0;
	}

	// FO4 vanilla passes (pass->numLights - 1) into the point-light writer and
	// reads physical sceneLights entries starting at index 1.
	auto availableLightCount = static_cast<std::uint32_t>(rawLightCount - kPreNGBSRenderPassSceneLightFirstIndex);
	if (a_requestedLightCount < availableLightCount) {
		availableLightCount = a_requestedLightCount;
	}

	std::uint32_t collected = 0;
	std::uint32_t strictWriteCount = 0;
	std::uint32_t missingEntryCount = 0;
	std::uint32_t missingWrapperDataCount = 0;
	std::uint32_t invalidNiLightDataCount = 0;
	std::uint32_t inactiveLightDataCount = 0;
	std::uint32_t duplicateLightCount = 0;
	std::uint32_t unreadableShadowMaskCount = 0;
	std::uint32_t invalidShadowMaskCount = 0;

	for (std::uint32_t i = 0; i < availableLightCount && (frameLights.size() < kMaxLights || strictWriteCount < kMaxStrictLights); ++i) {
		std::uintptr_t wrapperAddress = 0;
		const auto entryAddress = sceneLightsAddress + ((i + kPreNGBSRenderPassSceneLightFirstIndex) * sizeof(std::uintptr_t));
		if (!ReadPreNGValue(entryAddress, wrapperAddress) || wrapperAddress == 0) {
			++missingEntryCount;
			continue;
		}

		LightData data{};
		std::uintptr_t niLightAddress = 0;
		bool shadowMaskUnreadable = false;
		bool shadowMaskInvalid = false;
		std::uint32_t shadowMaskBit = 0;
		const auto decodeResult = DecodePreNGBSLightWrapper(
			wrapperAddress,
			data,
			niLightAddress,
			shadowMaskUnreadable,
			shadowMaskInvalid,
			shadowMaskBit);
		if (decodeResult == PreNGLightDecodeResult::MissingWrapperData) {
			++missingWrapperDataCount;
			continue;
		}
		if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData) {
			++invalidNiLightDataCount;
			continue;
		}
		if (decodeResult == PreNGLightDecodeResult::NonContributingLightData) {
			++inactiveLightDataCount;
			continue;
		}

		if (shadowMaskBit != 0) {
			strictLightDataTemp.ShadowBitMask |= shadowMaskBit;
		}
		if (shadowMaskUnreadable) {
			++unreadableShadowMaskCount;
		} else if (shadowMaskInvalid) {
			++invalidShadowMaskCount;
		}

		if (strictWriteCount < kMaxStrictLights) {
			strictLightDataTemp.StrictLights[strictWriteCount++] = data;
		}

		auto* lightKey = reinterpret_cast<RE::BSLight*>(niLightAddress);
		if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey)) {
			++duplicateLightCount;
			continue;
		}

		seenLights.insert(lightKey);
		seenThisPass.push_back(lightKey);
		frameLights.push_back(data);
		++collected;
	}

	strictLightDataTemp.NumStrictLights = strictWriteCount;
	currentStrictLightCount = strictWriteCount;

	static std::atomic_uint32_t decodeDiagCount = 0;
	if (availableLightCount > 0 && (collected > 0 || missingEntryCount > 0 || missingWrapperDataCount > 0 || invalidNiLightDataCount > 0 || inactiveLightDataCount > 0)) {
		const auto diagIndex = ++decodeDiagCount;
		if (diagIndex <= 8 || diagIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG scene-light decode pass=0x{:X} table=0x{:X} raw={} requested={} available={} collected={} strict={} shadowArg={} strictShadowMask=0x{:08X} skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
				passAddress,
				sceneLightsAddress,
				static_cast<std::uint32_t>(rawLightCount),
				a_requestedLightCount,
				availableLightCount,
				collected,
				strictWriteCount,
				a_shadowArg,
				strictLightDataTemp.ShadowBitMask,
				missingEntryCount,
				missingWrapperDataCount,
				invalidNiLightDataCount,
				inactiveLightDataCount,
				duplicateLightCount,
				unreadableShadowMaskCount,
				invalidShadowMaskCount);
		}
	}

	return collected;
}

std::uint32_t LightLimitFix::CollectLightsFromPreNGShadowScene()
{
	strictLightDataTemp = {};
	strictLightDataTemp.RoomIndex = -1;
	currentStrictLightCount = 0;

	std::uintptr_t activeLightsAddress = 0;
	std::uintptr_t activeShadowLightsAddress = 0;
	std::uintptr_t activeExtraLightsAddress = 0;
	std::uint32_t activeLightCount = 0;
	std::uint32_t activeShadowLightCount = 0;
	std::uint32_t activeExtraLightCount = 0;
	const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
	const auto shadowSceneNode = shadowSceneNodeRef.node;

	auto logFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG shadow-scene light decode held failures={} reason={} node=0x{:X} selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}) shadow=(ptr=0x{:X}, count={}) extra=(ptr=0x{:X}, count={})",
				failureIndex,
				a_reason,
				shadowSceneNode,
				static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
				static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex),
				shadowSceneNodeRef.currentIndexRead,
				shadowSceneNodeRef.usedFallback,
				activeLightsAddress,
				activeLightCount,
				activeShadowLightsAddress,
				activeShadowLightCount,
				activeExtraLightsAddress,
				activeExtraLightCount);
		}
	};

	if (shadowSceneNode == 0) {
		logFailure("world-shadow-scene-node-unavailable");
		return 0;
	}

	if (!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveLightsOffset, activeLightsAddress) ||
		!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveLightsCountOffset, activeLightCount) ||
		!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveShadowLightsOffset, activeShadowLightsAddress) ||
		!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveShadowLightsCountOffset, activeShadowLightCount) ||
		!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveExtraLightsOffset, activeExtraLightsAddress) ||
		!ReadPreNGValue(shadowSceneNode + kPreNGShadowSceneNodeActiveExtraLightsCountOffset, activeExtraLightCount)) {
		logFailure("arrays-unreadable");
		return 0;
	}

	if (activeLightCount > kPreNGMaxShadowSceneActiveLights ||
		activeShadowLightCount > kPreNGMaxShadowSceneActiveLights ||
		activeExtraLightCount > kPreNGMaxShadowSceneActiveLights) {
		logFailure("count-out-of-range");
		return 0;
	}

	if ((activeLightCount > 0 && activeLightsAddress == 0) ||
		(activeShadowLightCount > 0 && activeShadowLightsAddress == 0) ||
		(activeExtraLightCount > 0 && activeExtraLightsAddress == 0)) {
		logFailure("array-null");
		return 0;
	}

	std::uint32_t collected = 0;
	std::uint32_t strictWriteCount = 0;
	std::uint32_t missingEntryCount = 0;
	std::uint32_t missingWrapperDataCount = 0;
	std::uint32_t invalidNiLightDataCount = 0;
	std::uint32_t inactiveLightDataCount = 0;
	std::uint32_t duplicateLightCount = 0;
	std::uint32_t unreadableShadowMaskCount = 0;
	std::uint32_t invalidShadowMaskCount = 0;

	auto decodeArray = [&](std::uintptr_t a_arrayAddress, std::uint32_t a_count) {
		for (std::uint32_t i = 0; i < a_count && (frameLights.size() < kMaxLights || strictWriteCount < kMaxStrictLights); ++i) {
			std::uintptr_t wrapperAddress = 0;
			const auto entryAddress = a_arrayAddress + (i * sizeof(std::uintptr_t));
			if (!ReadPreNGValue(entryAddress, wrapperAddress) || wrapperAddress == 0) {
				++missingEntryCount;
				continue;
			}

			LightData data{};
			std::uintptr_t niLightAddress = 0;
			bool shadowMaskUnreadable = false;
			bool shadowMaskInvalid = false;
			std::uint32_t shadowMaskBit = 0;
			const auto decodeResult = DecodePreNGBSLightWrapper(
				wrapperAddress,
				data,
				niLightAddress,
				shadowMaskUnreadable,
				shadowMaskInvalid,
				shadowMaskBit);
			if (decodeResult == PreNGLightDecodeResult::MissingWrapperData) {
				++missingWrapperDataCount;
				continue;
			}
			if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData) {
				++invalidNiLightDataCount;
				continue;
			}
			if (decodeResult == PreNGLightDecodeResult::NonContributingLightData) {
				++inactiveLightDataCount;
				continue;
			}

			if (shadowMaskBit != 0) {
				strictLightDataTemp.ShadowBitMask |= shadowMaskBit;
			}
			if (shadowMaskUnreadable) {
				++unreadableShadowMaskCount;
			} else if (shadowMaskInvalid) {
				++invalidShadowMaskCount;
			}

			if (strictWriteCount < kMaxStrictLights) {
				strictLightDataTemp.StrictLights[strictWriteCount++] = data;
			}

			auto* lightKey = reinterpret_cast<RE::BSLight*>(niLightAddress);
			if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey)) {
				++duplicateLightCount;
				continue;
			}

			seenLights.insert(lightKey);
			seenThisPass.push_back(lightKey);
			frameLights.push_back(data);
			++collected;
		}
	};

	decodeArray(activeLightsAddress, activeLightCount);
	decodeArray(activeShadowLightsAddress, activeShadowLightCount);
	decodeArray(activeExtraLightsAddress, activeExtraLightCount);

	strictLightDataTemp.NumStrictLights = strictWriteCount;
	currentStrictLightCount = strictWriteCount;

	static std::atomic_uint32_t decodeDiagCount = 0;
	const auto diagIndex = ++decodeDiagCount;
	if (diagIndex <= 8 || diagIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG shadow-scene light decode node=0x{:X} selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}) shadow=(ptr=0x{:X}, count={}) extra=(ptr=0x{:X}, count={}) collected={} strict={} shadowMask=0x{:08X} skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
			shadowSceneNode,
			static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
			static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex),
			shadowSceneNodeRef.currentIndexRead,
			shadowSceneNodeRef.usedFallback,
			activeLightsAddress,
			activeLightCount,
			activeShadowLightsAddress,
			activeShadowLightCount,
			activeExtraLightsAddress,
			activeExtraLightCount,
			collected,
			strictWriteCount,
			strictLightDataTemp.ShadowBitMask,
			missingEntryCount,
			missingWrapperDataCount,
			invalidNiLightDataCount,
			inactiveLightDataCount,
			duplicateLightCount,
			unreadableShadowMaskCount,
			invalidShadowMaskCount);
	}

	return collected;
}

bool LightLimitFix::UpdatePreNGStrictLightDataCB()
{
	static bool loggedMissing = false;
	if (!strictLightDataCB) {
		if (!loggedMissing) {
			logger::warn("[LightLimitFix] PreNG strict-light CB update requested but resource is unavailable");
			loggedMissing = true;
		}
		return false;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};
	const auto hr = context->Map(strictLightDataCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) {
		LogResourceFailure("Map(strictLightDataCB)", hr);
		return false;
	}

	std::memcpy(mapped.pData, &strictLightDataTemp, sizeof(strictLightDataTemp));
	context->Unmap(strictLightDataCB.get(), 0);
	return true;
}

bool LightLimitFix::UploadPreNGStrictLightDataDiagnostic()
{
	if (!ShouldUpdatePreNGStrictLightCB()) {
		return false;
	}

	return UpdatePreNGStrictLightDataCB();
}

bool LightLimitFix::BindPreNGStrictLightDataCBToPixelShader(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, bool a_bufferAlreadyUploaded)
{
	if (!ShouldBindPreNGStrictLightCB()) {
		return false;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG strict-light CB b3 bind held failures={} reason={} pass=0x{:X} requested={} strict={} shadowMask=0x{:08X} uploadedBeforeBind={}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				strictLightDataTemp.ShadowBitMask,
				a_bufferAlreadyUploaded);
		}
	};

	if (!strictLightDataCB) {
		logBindFailure("missing-strict-cb");
		return false;
	}

	if (!a_bufferAlreadyUploaded && !UpdatePreNGStrictLightDataCB()) {
		logBindFailure("upload-failed");
		return false;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		logBindFailure("renderer-data-unavailable");
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		logBindFailure("context-unavailable");
		return false;
	}

	ID3D11Buffer* buffer = strictLightDataCB.get();
	context->PSSetConstantBuffers(3, 1, &buffer);

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG strict-light CB bound to PS b3 binds={} pass=0x{:X} requested={} strict={} shadowMask=0x{:08X} uploadedBeforeBind={}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_pass),
			a_requestedLightCount,
			currentStrictLightCount,
			strictLightDataTemp.ShadowBitMask,
			a_bufferAlreadyUploaded);
	}

	return true;
}

bool LightLimitFix::BindPreNGClusterSRVsToPixelShader(RE::BSRenderPass* a_pass, std::uint32_t a_requestedLightCount, bool a_strictCBBound)
{
	if (!ShouldBindPreNGClusterSRVs()) {
		return false;
	}

	auto logBindFailure = [&](const char* a_reason) {
		static std::atomic_uint32_t failureCount = 0;
		const auto failureIndex = ++failureCount;
		if (failureIndex <= 8 || failureIndex % 512 == 0) {
			logger::warn(
				"[LightLimitFix] PreNG cluster SRV t35-t37 bind held failures={} reason={} pass=0x{:X} requested={} strict={} clusterLights={} shadowMask=0x{:08X} strictCBBound={}",
				failureIndex,
				a_reason,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				currentLightCount,
				strictLightDataTemp.ShadowBitMask,
				a_strictCBBound);
		}
	};

	if (!a_strictCBBound) {
		logBindFailure("strict-cb-not-bound");
		return false;
	}

	if (!HasResources()) {
		logBindFailure("gpu-resources-incomplete");
		return false;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) {
		logBindFailure("renderer-data-unavailable");
		return false;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) {
		logBindFailure("context-unavailable");
		return false;
	}

	ID3D11ShaderResourceView* views[3]{
		lightsSRV.get(),
		lightIndexListSRV.get(),
		lightGridSRV.get()
	};
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);

	static std::atomic_uint32_t bindCount = 0;
	const auto bindIndex = ++bindCount;
	if (bindIndex <= 8 || bindIndex % 512 == 0) {
		logger::info(
			"[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 binds={} pass=0x{:X} requested={} strict={} clusterLights={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
			bindIndex,
			reinterpret_cast<std::uintptr_t>(a_pass),
			a_requestedLightCount,
			currentStrictLightCount,
			currentLightCount,
			clusterSize[0] * clusterSize[1] * clusterSize[2],
			strictLightDataTemp.ShadowBitMask,
			a_strictCBBound);
	}

	if (currentLightCount > 0) {
		static std::atomic_uint32_t nonZeroBindCount = 0;
		const auto nonZeroBindIndex = ++nonZeroBindCount;
		if (nonZeroBindIndex <= 8 || nonZeroBindIndex % 512 == 0) {
			logger::info(
				"[LightLimitFix] PreNG cluster SRVs nonzero bind proof nonzeroBinds={} binds={} pass=0x{:X} requested={} strict={} clusterLights={} clusters={} shadowMask=0x{:08X} strictCBBound={}",
				nonZeroBindIndex,
				bindIndex,
				reinterpret_cast<std::uintptr_t>(a_pass),
				a_requestedLightCount,
				currentStrictLightCount,
				currentLightCount,
				clusterSize[0] * clusterSize[1] * clusterSize[2],
				strictLightDataTemp.ShadowBitMask,
				a_strictCBBound);
		}
	}

	return true;
}
#endif

void LightLimitFix::CollectLightCB()
{
	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!ctx || !device) return;

	ID3D11Buffer* lightCB = nullptr;
	ctx->PSGetConstantBuffers(2, 1, &lightCB);
	if (!lightCB) return;

	D3D11_BUFFER_DESC desc;
	lightCB->GetDesc(&desc);
	if (desc.ByteWidth < 48) { lightCB->Release(); return; }

	D3D11_BUFFER_DESC stagingDesc{};
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.ByteWidth = desc.ByteWidth;

	ID3D11Buffer* stagingCB = nullptr;
	if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &stagingCB))) {
		lightCB->Release();
		return;
	}

	ctx->CopyResource(stagingCB, lightCB);
	lightCB->Release();

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped))) {
		stagingCB->Release();
		return;
	}

	const float* rawData = static_cast<const float*>(mapped.pData);
	std::uint32_t lightCount = desc.ByteWidth / 48;
	if (lightCount > 4) lightCount = 4;

	for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++) {
		const float* l = rawData + i * 12;

		if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f) continue;

		auto cbHash = static_cast<std::uint64_t>(l[0] * 1000.0f) ^
		              (static_cast<std::uint64_t>(l[1] * 1000.0f) << 20) ^
		              (static_cast<std::uint64_t>(l[4] * 255.0f) << 40);

		if (seenCBHashes.contains(cbHash)) continue;
		seenCBHashes.insert(cbHash);

		LightData data{};
		data.positionWS[0].data.x = l[0];
		data.positionWS[0].data.y = l[1];
		data.positionWS[0].data.z = l[2];
		data.radius       = l[3];
		data.color.x      = l[4];
		data.color.y      = l[5];
		data.color.z      = l[6];
		data.fade         = l[7];
		data.invRadius    = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.lightFlags   = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}

	ctx->Unmap(stagingCB, 0);
	stagingCB->Release();
}

void LightLimitFix::CollectLightsFromBSLight()
{
	for (auto* light : seenLights) {
		if (!light || frameLights.size() >= kMaxLights) break;
		auto* niLight = reinterpret_cast<NiLightView*>(light);
		LightData data{};
		data.color.x = niLight->diff.r;
		data.color.y = niLight->diff.g;
		data.color.z = niLight->diff.b;
		data.fade = niLight->dimmer;
		data.radius = niLight->modelBound.fRadius;
		data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.positionWS[0].data.x = niLight->world.translate.x;
		data.positionWS[0].data.y = niLight->world.translate.y;
		data.positionWS[0].data.z = niLight->world.translate.z;
		data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}
}

void LightLimitFix::CollectLightsFromScene()
{
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return;

	auto& refs = dh->GetFormArray<RE::TESObjectREFR>();
	for (auto* ref : refs) {
		if (!ref || frameLights.size() >= kMaxLights) break;

		auto* baseObj = ref->GetObjectReference();
		auto* lightForm = baseObj ? baseObj->As<RE::TESObjectLIGH>() : nullptr;
		if (!lightForm) continue;

		auto* niObj = ref->Get3D();
		if (!niObj) continue;

		auto* niLight = reinterpret_cast<NiLightView*>(niObj);
		if (!niLight) continue;

		auto* lightKey = reinterpret_cast<RE::BSLight*>(niLight);
		if (seenLights.contains(lightKey)) continue;
		seenLights.insert(lightKey);

		LightData data{};
		data.color.x = niLight->diff.r;
		data.color.y = niLight->diff.g;
		data.color.z = niLight->diff.b;
		data.fade = niLight->dimmer;
		data.radius = niLight->modelBound.fRadius;
		data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.positionWS[0].data.x = niLight->world.translate.x;
		data.positionWS[0].data.y = niLight->world.translate.y;
		data.positionWS[0].data.z = niLight->world.translate.z;
		data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass* /*a_pass*/)
{
	seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass* a_pass)
{
#if defined(FALLOUT_PRE_NG)
	CollectLightsFromPreNGSceneLights(a_pass);
#else
	CollectLightsFromPass(a_pass);
#endif
}

namespace RE::VTABLE
{
}

void LightLimitFix::Hooks::Install()
{
	stl::write_vfunc<0x7, BSLightingShader_SetupGeometry>(RE::VTABLE::BSLightingShader[0]);
	stl::write_vfunc<0x7, BSEffectShader_SetupGeometry>(RE::VTABLE::BSEffectShader[0]);
	logger::info("[LightLimitFix] Installed SetupGeometry hooks (vfunc index 7)");
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass)
{
	auto& self = globals::features::lightLimitFix;
	self.SetupGeometryBefore(a_pass);
	func(a_this, a_pass);
	self.SetupGeometryAfter(a_pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass)
{
	func(a_this, a_pass);
	auto& self = globals::features::lightLimitFix;
	self.SetupGeometryBefore(a_pass);
	self.SetupGeometryAfter(a_pass);
}
