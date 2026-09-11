#include "Core/ShaderHooks/PreNGVanillaDumps.h"

#include "Core/ShaderCache.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"

#include <atomic>
#include <cstring>
#include <format>
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
		// One dump per distinct (descriptor, D3D object) pair. The engine resolves the
		// same shader thousands of times per second, so without this the dumpers would
		// rewrite the same file on every draw.
		struct PreNGDFLightVanillaDumpKey
		{
			std::uint32_t pixelDescriptor = 0;
			std::uintptr_t pixelD3D = 0;
		};

		std::mutex s_preNGBSLightingVanillaDumpLock;
		std::vector<PreNGDFLightVanillaDumpKey> s_preNGBSLightingVanillaDumpKeys;
		std::atomic_uint32_t s_preNGBSLightingVanillaDumpAttempts = 0;
		std::atomic_bool s_preNGBSLightingVanillaDumpLimitLogged = false;
		std::mutex s_preNGDFLightVanillaDumpLock;
		std::vector<PreNGDFLightVanillaDumpKey> s_preNGDFLightVanillaDumpKeys;
		std::atomic_uint32_t s_preNGDFLightVanillaDumpAttempts = 0;
		std::atomic_bool s_preNGDFLightVanillaDumpLimitLogged = false;
		std::mutex s_preNGDFCompositeVanillaDumpLock;
		std::vector<PreNGDFLightVanillaDumpKey> s_preNGDFCompositeVanillaDumpKeys;
		std::atomic_uint32_t s_preNGDFCompositeVanillaDumpAttempts = 0;
		std::atomic_bool s_preNGDFCompositeVanillaDumpLimitLogged = false;

		bool ShouldDumpPreNGBSLightingVanillaObject(std::uint32_t a_pixelDescriptor, std::uintptr_t a_pixelD3D)
		{
			std::scoped_lock lock(s_preNGBSLightingVanillaDumpLock);
			for (const auto& key : s_preNGBSLightingVanillaDumpKeys) {
				if (key.pixelDescriptor == a_pixelDescriptor && key.pixelD3D == a_pixelD3D) {
					return false;
				}
			}

			if (s_preNGBSLightingVanillaDumpKeys.size() >= kPreNGMaxBSLightingVanillaDumpDiagnostics) {
				return false;
			}

			s_preNGBSLightingVanillaDumpKeys.push_back({ a_pixelDescriptor, a_pixelD3D });
			return true;
		}

		bool ShouldDumpPreNGDFLightVanillaObject(std::uint32_t a_pixelDescriptor, std::uintptr_t a_pixelD3D)
		{
			std::scoped_lock lock(s_preNGDFLightVanillaDumpLock);
			for (const auto& key : s_preNGDFLightVanillaDumpKeys) {
				if (key.pixelDescriptor == a_pixelDescriptor && key.pixelD3D == a_pixelD3D) {
					return false;
				}
			}

			if (s_preNGDFLightVanillaDumpKeys.size() >= kPreNGMaxDFLightVanillaDumpDiagnostics) {
				return false;
			}

			s_preNGDFLightVanillaDumpKeys.push_back({ a_pixelDescriptor, a_pixelD3D });
			return true;
		}

		bool ShouldDumpPreNGDFCompositeVanillaObject(std::uint32_t a_pixelDescriptor, std::uintptr_t a_pixelD3D)
		{
			std::scoped_lock lock(s_preNGDFCompositeVanillaDumpLock);
			for (const auto& key : s_preNGDFCompositeVanillaDumpKeys) {
				if (key.pixelDescriptor == a_pixelDescriptor && key.pixelD3D == a_pixelD3D) {
					return false;
				}
			}

			if (s_preNGDFCompositeVanillaDumpKeys.size() >= kPreNGMaxDFCompositeVanillaDumpDiagnostics) {
				return false;
			}

			s_preNGDFCompositeVanillaDumpKeys.push_back({ a_pixelDescriptor, a_pixelD3D });
			return true;
		}
	}

	// The three dumpers below are deliberately near-identical rather than folded into
	// one parameterised helper. They differ in family name, counter set, dump limit and
	// label, and DFComposite/BSLighting pass a category argument to
	// DumpObservedD3DShaderObject that DFLight does not. Keeping them separate is what
	// let this cluster move verbatim out of BSShaderHooks.cpp; deduplicating is a
	// behaviour-affecting change and belongs in its own commit.
	void DumpPreNGBSLightingVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGBSLightingVanillaDumpAttempts;
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logDump = [&](const char* a_state, const char* a_reason, std::uintptr_t a_pixelEntry, std::uintptr_t a_pixelD3D) {
			const bool forceLog = std::strcmp(a_reason, "dump-limit-reached") == 0;
			if (!forceLog && attempt > 16 && !IsPreNGPowerOfTwo(attempt)) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG BSLighting vanilla shader dump attempts={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} lookupResult={} currentPS=0x{:X} psD3D=0x{:X} dump={} reason={} maxDumps={}",
				attempt,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_lookupResult,
				a_pixelEntry,
				a_pixelD3D,
				a_state,
				a_reason,
				kPreNGMaxBSLightingVanillaDumpDiagnostics);
		};

		if (a_lookupResult == 0) {
			logDump("failed", "vanilla-lookup-miss", 0, 0);
			return;
		}

		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		if (pixelEntry == 0 || pixelD3D == 0) {
			logDump("failed", "current-ps-unavailable", pixelEntry, pixelD3D);
			return;
		}

		if (!ShouldDumpPreNGBSLightingVanillaObject(pixelDescriptor, pixelD3D)) {
			bool dumpLimitReached = false;
			{
				std::scoped_lock lock(s_preNGBSLightingVanillaDumpLock);
				dumpLimitReached = s_preNGBSLightingVanillaDumpKeys.size() >= kPreNGMaxBSLightingVanillaDumpDiagnostics;
			}
			if (dumpLimitReached && !s_preNGBSLightingVanillaDumpLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logDump("held", "dump-limit-reached", pixelEntry, pixelD3D);
			}
			return;
		}

		const auto label = std::format(
			"BSLighting_PS0x{:08X}_VS0x{:08X}",
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(
			ShaderStage::Pixel,
			pixelD3D,
			label,
			"BSLighting");
		logDump(dumped ? "dumped" : "failed", dumped ? "targeted-vanilla-dump-written" : "bytecode-not-observed", pixelEntry, pixelD3D);
	}

	void DumpPreNGDFLightVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFLightVanillaDumpAttempts;
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logDump = [&](const char* a_state, const char* a_reason, std::uintptr_t a_pixelEntry, std::uintptr_t a_pixelD3D) {
			const bool forceLog = std::strcmp(a_reason, "dump-limit-reached") == 0;
			if (!forceLog && attempt > 16 && !IsPreNGPowerOfTwo(attempt)) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFLight vanilla shader dump attempts={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} lookupResult={} currentPS=0x{:X} psD3D=0x{:X} dump={} reason={} maxDumps={}",
				attempt,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_lookupResult,
				a_pixelEntry,
				a_pixelD3D,
				a_state,
				a_reason,
				kPreNGMaxDFLightVanillaDumpDiagnostics);
		};

		if (a_lookupResult == 0) {
			logDump("failed", "vanilla-lookup-miss", 0, 0);
			return;
		}

		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		if (pixelEntry == 0 || pixelD3D == 0) {
			logDump("failed", "current-ps-unavailable", pixelEntry, pixelD3D);
			return;
		}

		if (!ShouldDumpPreNGDFLightVanillaObject(pixelDescriptor, pixelD3D)) {
			bool dumpLimitReached = false;
			{
				std::scoped_lock lock(s_preNGDFLightVanillaDumpLock);
				dumpLimitReached = s_preNGDFLightVanillaDumpKeys.size() >= kPreNGMaxDFLightVanillaDumpDiagnostics;
			}
			if (dumpLimitReached && !s_preNGDFLightVanillaDumpLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logDump("held", "dump-limit-reached", pixelEntry, pixelD3D);
			}
			return;
		}

		const auto label = std::format(
			"{}_PS0x{:08X}_VS0x{:08X}",
			GetPreNGDFLightVanillaDumpFamily(pixelDescriptor),
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(ShaderStage::Pixel, pixelD3D, label);
		logDump(dumped ? "dumped" : "failed", dumped ? "targeted-vanilla-dump-written" : "bytecode-not-observed", pixelEntry, pixelD3D);
	}

	void DumpPreNGDFCompositeVanillaShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		std::uint8_t a_lookupResult)
	{
		const auto attempt = ++s_preNGDFCompositeVanillaDumpAttempts;
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = a_shader ?
			ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength) :
			std::string("<null>");

		auto logDump = [&](const char* a_state, const char* a_reason, std::uintptr_t a_pixelEntry, std::uintptr_t a_pixelD3D) {
			const bool forceLog = std::strcmp(a_reason, "dump-limit-reached") == 0;
			if (!forceLog && attempt > 16 && !IsPreNGPowerOfTwo(attempt)) {
				return;
			}

			logger::info(
				"[BSShaderHooks] PreNG DFComposite vanilla shader dump attempts={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} lookupResult={} currentPS=0x{:X} psD3D=0x{:X} dump={} reason={} maxDumps={}",
				attempt,
				a_shader ? static_cast<std::int32_t>(a_shader->shaderType) : -1,
				fxpFilename,
				static_cast<std::uint32_t>(a_vertexDescriptor),
				static_cast<std::uint32_t>(a_hullDescriptor),
				static_cast<std::uint32_t>(a_domainDescriptor),
				pixelDescriptor,
				a_lookupResult,
				a_pixelEntry,
				a_pixelD3D,
				a_state,
				a_reason,
				kPreNGMaxDFCompositeVanillaDumpDiagnostics);
		};

		if (a_lookupResult == 0) {
			logDump("failed", "vanilla-lookup-miss", 0, 0);
			return;
		}

		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		if (pixelEntry == 0 || pixelD3D == 0) {
			logDump("failed", "current-ps-unavailable", pixelEntry, pixelD3D);
			return;
		}

		if (!ShouldDumpPreNGDFCompositeVanillaObject(pixelDescriptor, pixelD3D)) {
			bool dumpLimitReached = false;
			{
				std::scoped_lock lock(s_preNGDFCompositeVanillaDumpLock);
				dumpLimitReached = s_preNGDFCompositeVanillaDumpKeys.size() >= kPreNGMaxDFCompositeVanillaDumpDiagnostics;
			}
			if (dumpLimitReached && !s_preNGDFCompositeVanillaDumpLimitLogged.exchange(true, std::memory_order_relaxed)) {
				logDump("held", "dump-limit-reached", pixelEntry, pixelD3D);
			}
			return;
		}

		const auto label = std::format(
			"DFComposite_PS0x{:08X}_VS0x{:08X}",
			pixelDescriptor,
			static_cast<std::uint32_t>(a_vertexDescriptor));
		const auto dumped = ShaderCache::GetSingleton()->DumpObservedD3DShaderObject(
			ShaderStage::Pixel,
			pixelD3D,
			label,
			"DFComposite");
		logDump(dumped ? "dumped" : "failed", dumped ? "targeted-vanilla-dump-written" : "bytecode-not-observed", pixelEntry, pixelD3D);
	}
#endif
}
