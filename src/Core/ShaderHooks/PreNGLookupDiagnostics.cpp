#include "Core/ShaderHooks/PreNGLookupDiagnostics.h"

// globals::features::lightLimitFix is the widest coupling in BSShaderHooks and the
// outline is explicit that the split should not try to abstract it. The traces read
// it to report whether LLF is live alongside the lookup they are describing.
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"
#include "Core/ShaderHooks/PreNGSwitches.h"
#include "Features/LightLimitFix.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <RE/FO4Runtime.h>

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif

namespace CommunityShaders
{
#if defined(FALLOUT_PRE_NG)

	namespace
	{
		struct PreNGShaderLookupDiagnosticKey
		{
			std::int32_t shaderType = 0;
			std::int32_t vertexDescriptor = 0;
			std::int32_t hullDescriptor = 0;
			std::int32_t domainDescriptor = 0;
			std::int32_t pixelDescriptor = 0;
			bool found = false;
		};

		bool SamePreNGShaderLookupKey(const PreNGShaderLookupDiagnosticKey& a_lhs, const PreNGShaderLookupDiagnosticKey& a_rhs)
		{
			return a_lhs.shaderType == a_rhs.shaderType &&
			       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
			       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
			       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
			       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
			       a_lhs.found == a_rhs.found;
		}

		struct PreNGShaderLookupFirstSeenKey
		{
			std::int32_t shaderType = 0;
			std::int32_t vertexDescriptor = 0;
			std::int32_t hullDescriptor = 0;
			std::int32_t domainDescriptor = 0;
			std::int32_t pixelDescriptor = 0;
			bool found = false;
			std::string fxpFilename;
		};

		bool SamePreNGShaderLookupFirstSeenKey(
			const PreNGShaderLookupFirstSeenKey& a_lhs,
			const PreNGShaderLookupFirstSeenKey& a_rhs)
		{
			return a_lhs.shaderType == a_rhs.shaderType &&
			       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
			       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
			       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
			       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
			       a_lhs.found == a_rhs.found &&
			       a_lhs.fxpFilename == a_rhs.fxpFilename;
		}

		std::mutex s_preNGShaderLookupDiagnosticLock;
		std::vector<PreNGShaderLookupDiagnosticKey> s_preNGShaderLookupDiagnosticKeys;
		std::mutex s_preNGShaderLookupFirstSeenLock;
		std::vector<PreNGShaderLookupFirstSeenKey> s_preNGShaderLookupFirstSeenKeys;
		std::atomic_uint32_t s_preNGBSShaderLookupEntryCalls = 0;
		std::atomic_uint32_t s_preNGBSShaderLookupLightingEntryCalls = 0;
		std::atomic_uint32_t s_preNGBSShaderLookupBSLightingEntryCalls = 0;
		std::atomic_uint32_t s_preNGBSShaderLookupNullEntryCalls = 0;
		std::atomic_uint32_t s_preNGBSLightingShaderLookupCalls = 0;
		std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsComplete = false;
		std::atomic_bool s_preNGShaderLookupHeavyDiagnosticsLogged = false;
		std::atomic_bool s_preNGShaderLookupFirstSeenLimitLogged = false;
	}

	bool ShouldBypassPreNGShaderLookupHeavyDiagnostics()
	{
		return s_preNGShaderLookupHeavyDiagnosticsComplete.load(std::memory_order_relaxed);
	}

	void MaybeCompletePreNGShaderLookupHeavyDiagnostics()
	{
		const auto lightingCalls = s_preNGBSLightingShaderLookupCalls.load(std::memory_order_relaxed);
		const auto bsLightingCalls = s_preNGBSShaderLookupBSLightingEntryCalls.load(std::memory_order_relaxed);
		const auto totalEntryCalls = s_preNGBSShaderLookupEntryCalls.load(std::memory_order_relaxed);
		constexpr std::uint32_t kPreNGShaderLookupNoBSLightingTotalCallBudget = 250000;
		if (bsLightingCalls < kPreNGMaxShaderLookupHeavyDiagnostics &&
			totalEntryCalls < kPreNGShaderLookupNoBSLightingTotalCallBudget) {
			return;
		}

		s_preNGShaderLookupHeavyDiagnosticsComplete.store(true, std::memory_order_relaxed);
		if (!s_preNGShaderLookupHeavyDiagnosticsLogged.exchange(true, std::memory_order_relaxed)) {
			std::size_t uniqueLookups = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupDiagnosticLock);
				uniqueLookups = s_preNGShaderLookupDiagnosticKeys.size();
			}
			std::size_t firstSeenLookups = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupFirstSeenLock);
				firstSeenLookups = s_preNGShaderLookupFirstSeenKeys.size();
			}
			logger::info(
				"[BSShaderHooks] PreNG shader lookup heavy diagnostic complete; bypassing hot-path metadata/audit work after lightingCalls={} bsLightingCalls={} totalEntryCalls={} uniqueLookups={} firstSeenLookups={} max={} noBSLightingTotalBudget={}; shader replacement remains held",
				lightingCalls,
				bsLightingCalls,
				totalEntryCalls,
				uniqueLookups,
				firstSeenLookups,
				kPreNGMaxShaderLookupHeavyDiagnostics,
				kPreNGShaderLookupNoBSLightingTotalCallBudget);
		}
	}

	namespace
	{
		bool ShouldLogPreNGShaderLookupEntry(
			std::uint32_t a_totalCalls,
			std::uint32_t a_lightingCalls,
			std::uint32_t a_bsLightingCalls,
			std::uint32_t a_nullCalls,
			bool a_isLighting,
			bool a_isBSLighting,
			bool a_isNull)
		{
			return a_totalCalls <= 16 ||
			       IsPreNGPowerOfTwo(a_totalCalls) ||
			       (a_isLighting && (a_lightingCalls <= 16 || IsPreNGPowerOfTwo(a_lightingCalls))) ||
			       (a_isBSLighting && (a_bsLightingCalls <= 16 || IsPreNGPowerOfTwo(a_bsLightingCalls))) ||
			       (a_isNull && a_nullCalls <= 8);
		}

		void TracePreNGShaderLookupFirstSeen(
			std::int32_t a_shaderType,
			std::string_view a_fxpFilename,
			std::int32_t a_originalVertexDescriptor,
			std::int32_t a_originalHullDescriptor,
			std::int32_t a_originalDomainDescriptor,
			std::int32_t a_originalPixelDescriptor,
			std::int32_t a_lookupVertexDescriptor,
			std::int32_t a_lookupPixelDescriptor,
			std::uint32_t a_totalCalls,
			std::uint32_t a_lightingCalls,
			std::uint32_t a_bsLightingCalls,
			bool a_found,
			bool a_mutated,
			bool a_isLighting,
			bool a_isBSLighting,
			bool a_isNull)
		{
			const PreNGShaderLookupFirstSeenKey key{
				a_shaderType,
				a_originalVertexDescriptor,
				a_originalHullDescriptor,
				a_originalDomainDescriptor,
				a_originalPixelDescriptor,
				a_found,
				std::string{ a_fxpFilename }
			};

			std::size_t seenCount = 0;
			{
				std::scoped_lock lock(s_preNGShaderLookupFirstSeenLock);
				for (const auto& loggedKey : s_preNGShaderLookupFirstSeenKeys) {
					if (SamePreNGShaderLookupFirstSeenKey(loggedKey, key)) {
						return;
					}
				}
				if (s_preNGShaderLookupFirstSeenKeys.size() >= kPreNGMaxShaderLookupFirstSeenDiagnostics) {
					if (!s_preNGShaderLookupFirstSeenLimitLogged.exchange(true, std::memory_order_relaxed)) {
						logger::info(
							"[BSShaderHooks] PreNG shader lookup first-seen diagnostic limit reached max={} total={} lighting={} bsLighting={}; further first-seen entries are held",
							kPreNGMaxShaderLookupFirstSeenDiagnostics,
							a_totalCalls,
							a_lightingCalls,
							a_bsLightingCalls);
					}
					return;
				}
				s_preNGShaderLookupFirstSeenKeys.push_back(key);
				seenCount = s_preNGShaderLookupFirstSeenKeys.size();
			}

			const char* classification = a_isBSLighting ? "bs-lighting" :
				(a_isLighting ? "lighting" :
					(a_shaderType == kPreNGDFCompositeShaderType ? "df-composite" : (a_isNull ? "null" : "non-lighting")));
			logger::info(
				"[BSShaderHooks] PreNG shader lookup first-seen diagnostic seen={} max={} total={} lighting={} bsLighting={} class={} shaderType={} fxp={} originalVS=0x{:X} originalHS=0x{:X} originalDS=0x{:X} originalPS=0x{:X} lookupVS=0x{:X} lookupPS=0x{:X} found={} mutated={} mutateGate={} bindGate={}",
				seenCount,
				kPreNGMaxShaderLookupFirstSeenDiagnostics,
				a_totalCalls,
				a_lightingCalls,
				a_bsLightingCalls,
				classification,
				a_shaderType,
				a_fxpFilename,
				static_cast<std::uint32_t>(a_originalVertexDescriptor),
				static_cast<std::uint32_t>(a_originalHullDescriptor),
				static_cast<std::uint32_t>(a_originalDomainDescriptor),
				static_cast<std::uint32_t>(a_originalPixelDescriptor),
				static_cast<std::uint32_t>(a_lookupVertexDescriptor),
				static_cast<std::uint32_t>(a_lookupPixelDescriptor),
				a_found,
				a_mutated,
				ShouldMutatePreNGDescriptorShaders() ? "on" : "off",
				ShouldBindPreNGDescriptorShaders() ? "on" : "off");
		}
	}

	void TracePreNGShaderLookupEntry(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalHullDescriptor,
		std::int32_t a_originalDomainDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_lookupVertexDescriptor,
		std::int32_t a_lookupPixelDescriptor,
		bool a_found)
	{
		const auto totalCalls = ++s_preNGBSShaderLookupEntryCalls;
		const bool isNull = a_shader == nullptr;
		const auto shaderType = isNull ? -1 : static_cast<std::int32_t>(a_shader->shaderType);
		const auto fxpFilename = isNull ? std::string("<null>") : ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const bool isLighting = !isNull && IsPreNGLightingDescriptorShader(shaderType, fxpFilename);
		const bool isBSLighting = !isNull && shaderType == kPreNGBSLightingShaderType;
		const auto lightingCalls = isLighting ? ++s_preNGBSShaderLookupLightingEntryCalls : s_preNGBSShaderLookupLightingEntryCalls.load();
		const auto bsLightingCalls = isBSLighting ? ++s_preNGBSShaderLookupBSLightingEntryCalls : s_preNGBSShaderLookupBSLightingEntryCalls.load();
		const auto nullCalls = isNull ? ++s_preNGBSShaderLookupNullEntryCalls : s_preNGBSShaderLookupNullEntryCalls.load();
		const bool mutated = a_originalVertexDescriptor != a_lookupVertexDescriptor ||
		                     a_originalPixelDescriptor != a_lookupPixelDescriptor;

		TracePreNGShaderLookupFirstSeen(
			shaderType,
			fxpFilename,
			a_originalVertexDescriptor,
			a_originalHullDescriptor,
			a_originalDomainDescriptor,
			a_originalPixelDescriptor,
			a_lookupVertexDescriptor,
			a_lookupPixelDescriptor,
			totalCalls,
			lightingCalls,
			bsLightingCalls,
			a_found,
			mutated,
			isLighting,
			isBSLighting,
			isNull);
		if (!ShouldLogPreNGShaderLookupEntry(totalCalls, lightingCalls, bsLightingCalls, nullCalls, isLighting, isBSLighting, isNull)) {
			return;
		}

		logger::info(
			"[BSShaderHooks] PreNG shader lookup entry diagnostic total={} lighting={} bsLighting={} null={} shaderType={} fxp={} originalVS=0x{:X} originalHS=0x{:X} originalDS=0x{:X} originalPS=0x{:X} lookupVS=0x{:X} lookupPS=0x{:X} found={} mutated={} mutateGate={} bindGate={}",
			totalCalls,
			lightingCalls,
			bsLightingCalls,
			nullCalls,
			shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_originalVertexDescriptor),
			static_cast<std::uint32_t>(a_originalHullDescriptor),
			static_cast<std::uint32_t>(a_originalDomainDescriptor),
			static_cast<std::uint32_t>(a_originalPixelDescriptor),
			static_cast<std::uint32_t>(a_lookupVertexDescriptor),
			static_cast<std::uint32_t>(a_lookupPixelDescriptor),
			a_found,
			mutated,
			ShouldMutatePreNGDescriptorShaders() ? "on" : "off",
			ShouldBindPreNGDescriptorShaders() ? "on" : "off");
	}

	namespace
	{
		bool ShouldLogPreNGShaderLookup(const PreNGShaderLookupDiagnosticKey& a_key)
		{
			std::scoped_lock lock(s_preNGShaderLookupDiagnosticLock);
			for (const auto& loggedKey : s_preNGShaderLookupDiagnosticKeys) {
				if (SamePreNGShaderLookupKey(loggedKey, a_key)) {
					return false;
				}
			}

			if (s_preNGShaderLookupDiagnosticKeys.size() >= kPreNGMaxShaderLookupDiagnostics) {
				return false;
			}

			s_preNGShaderLookupDiagnosticKeys.push_back(a_key);
			return true;
		}
	}

	void TracePreNGShaderLookup(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader)) {
			return;
		}

		const auto calls = ++s_preNGBSLightingShaderLookupCalls;
		const PreNGShaderLookupDiagnosticKey key{
			a_shader->shaderType,
			a_vertexDescriptor,
			a_hullDescriptor,
			a_domainDescriptor,
			a_pixelDescriptor,
			a_found
		};
		if (!ShouldLogPreNGShaderLookup(key)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto vertexDescriptor = static_cast<std::uint32_t>(a_vertexDescriptor);
		const auto hullDescriptor = static_cast<std::uint32_t>(a_hullDescriptor);
		const auto domainDescriptor = static_cast<std::uint32_t>(a_domainDescriptor);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto techniqueFamily = (pixelDescriptor >> 8) & 0x3F;
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(ShaderStage::Vertex, *a_shader, vertexDescriptor, fxpFilename, vertexD3D != 0, vertexEntry, vertexD3D);
		shaderCache->ObserveDescriptorShader(ShaderStage::Pixel, *a_shader, pixelDescriptor, fxpFilename, pixelD3D != 0, pixelEntry, pixelD3D);
		const auto pixelDescriptorState = shaderCache->GetDescriptorShaderState(ShaderStage::Pixel, a_shader->shaderType, pixelDescriptor, fxpFilename);
		const auto descriptorCacheState = pixelDescriptorState ?
			(pixelDescriptorState->found ? "vanilla-observed" : "miss-observed") :
			"missing";
		constexpr bool descriptorBridgeAvailable = true;

		logger::info(
			"[BSShaderHooks] PreNG shader lookup reached calls={} shaderType={} fxp={} techniqueFamily={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} found={} currentVS=0x{:X} currentHS=0x{:X} currentDS=0x{:X} currentPS=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} descriptorBridge={} descriptorCache={} shaderDB=held replacement=held customCompile=held customBind=held",
			calls,
			a_shader->shaderType,
			fxpFilename,
			techniqueFamily,
			vertexDescriptor,
			hullDescriptor,
			domainDescriptor,
			pixelDescriptor,
			a_found,
			vertexEntry,
			hullEntry,
			domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			descriptorBridgeAvailable ? "available" : "missing",
			descriptorCacheState);

		globals::features::lightLimitFix.TracePreNGActiveLightingBindings(
			"shader-lookup",
			static_cast<std::int32_t>(a_shader->shaderType),
			vertexDescriptor,
			pixelDescriptor,
			a_found,
			pixelD3D);
	}

#endif
}
