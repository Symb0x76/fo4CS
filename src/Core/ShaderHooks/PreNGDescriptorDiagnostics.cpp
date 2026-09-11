#include "Core/ShaderHooks/PreNGDescriptorDiagnostics.h"

#include "Core/ShaderCache.h"
#include "Core/ShaderHooks/PreNGDescriptorPredicates.h"
#include "Core/ShaderHooks/PreNGRuntime.h"
#include "Core/ShaderHooks/PreNGShaderHookConstants.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

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
		std::atomic_uint32_t s_preNGDFCompositeDescriptorObservations = 0;

		struct PreNGDescriptorMutationDiagnosticKey
		{
			std::int32_t shaderType = 0;
			std::int32_t originalVertexDescriptor = 0;
			std::int32_t originalPixelDescriptor = 0;
			std::int32_t modifiedVertexDescriptor = 0;
			std::int32_t modifiedPixelDescriptor = 0;
			std::string mutateState;
			std::string reason;
		};

		bool SamePreNGDescriptorMutationKey(const PreNGDescriptorMutationDiagnosticKey& a_lhs, const PreNGDescriptorMutationDiagnosticKey& a_rhs)
		{
			return a_lhs.shaderType == a_rhs.shaderType &&
			       a_lhs.originalVertexDescriptor == a_rhs.originalVertexDescriptor &&
			       a_lhs.originalPixelDescriptor == a_rhs.originalPixelDescriptor &&
			       a_lhs.modifiedVertexDescriptor == a_rhs.modifiedVertexDescriptor &&
			       a_lhs.modifiedPixelDescriptor == a_rhs.modifiedPixelDescriptor &&
			       a_lhs.mutateState == a_rhs.mutateState &&
			       a_lhs.reason == a_rhs.reason;
		}

		std::mutex s_preNGDescriptorMutationDiagnosticLock;
		std::vector<PreNGDescriptorMutationDiagnosticKey> s_preNGDescriptorMutationDiagnosticKeys;

		bool ShouldLogPreNGDescriptorMutation(const PreNGDescriptorMutationDiagnosticKey& a_key)
		{
			std::scoped_lock lock(s_preNGDescriptorMutationDiagnosticLock);
			for (const auto& loggedKey : s_preNGDescriptorMutationDiagnosticKeys) {
				if (SamePreNGDescriptorMutationKey(loggedKey, a_key)) {
					return false;
				}
			}

			if (s_preNGDescriptorMutationDiagnosticKeys.size() >= kPreNGMaxDescriptorMutationDiagnostics) {
				return false;
			}

			s_preNGDescriptorMutationDiagnosticKeys.push_back(a_key);
			return true;
		}

		struct PreNGDescriptorBindDiagnosticKey
		{
			std::int32_t shaderType = 0;
			std::int32_t vertexDescriptor = 0;
			std::int32_t hullDescriptor = 0;
			std::int32_t domainDescriptor = 0;
			std::int32_t pixelDescriptor = 0;
			std::string bindState;
			std::string reason;
		};

		bool SamePreNGDescriptorBindKey(const PreNGDescriptorBindDiagnosticKey& a_lhs, const PreNGDescriptorBindDiagnosticKey& a_rhs)
		{
			return a_lhs.shaderType == a_rhs.shaderType &&
			       a_lhs.vertexDescriptor == a_rhs.vertexDescriptor &&
			       a_lhs.hullDescriptor == a_rhs.hullDescriptor &&
			       a_lhs.domainDescriptor == a_rhs.domainDescriptor &&
			       a_lhs.pixelDescriptor == a_rhs.pixelDescriptor &&
			       a_lhs.bindState == a_rhs.bindState &&
			       a_lhs.reason == a_rhs.reason;
		}

		std::mutex s_preNGDescriptorBindDiagnosticLock;
		std::vector<PreNGDescriptorBindDiagnosticKey> s_preNGDescriptorBindDiagnosticKeys;

		bool ShouldLogPreNGDescriptorBind(const PreNGDescriptorBindDiagnosticKey& a_key)
		{
			std::scoped_lock lock(s_preNGDescriptorBindDiagnosticLock);
			for (const auto& loggedKey : s_preNGDescriptorBindDiagnosticKeys) {
				if (SamePreNGDescriptorBindKey(loggedKey, a_key)) {
					return false;
				}
			}

			if (s_preNGDescriptorBindDiagnosticKeys.size() >= kPreNGMaxDescriptorBindDiagnostics) {
				return false;
			}

			s_preNGDescriptorBindDiagnosticKeys.push_back(a_key);
			return true;
		}

	}

	void LogPreNGDescriptorMutation(
		RE::BSShader* a_shader,
		std::int32_t a_originalVertexDescriptor,
		std::int32_t a_originalPixelDescriptor,
		std::int32_t a_modifiedVertexDescriptor,
		std::int32_t a_modifiedPixelDescriptor,
		const char* a_mutateState,
		const char* a_reason)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader) && !IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const PreNGDescriptorMutationDiagnosticKey key{
			a_shader->shaderType,
			a_originalVertexDescriptor,
			a_originalPixelDescriptor,
			a_modifiedVertexDescriptor,
			a_modifiedPixelDescriptor,
			a_mutateState ? a_mutateState : "<null>",
			a_reason ? a_reason : "<null>"
		};
		if (!ShouldLogPreNGDescriptorMutation(key)) {
			return;
		}

		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const auto techniqueFamily = (static_cast<std::uint32_t>(a_modifiedPixelDescriptor) >> 8) & 0x3F;
		logger::info(
			"[BSShaderHooks] PreNG descriptor lookup mutation shaderType={} fxp={} techniqueFamily={} originalVS=0x{:X} originalPS=0x{:X} modifiedVS=0x{:X} modifiedPS=0x{:X} descriptorBridge=available shaderDB=held replacement=held customMutate={} reason={}",
			a_shader->shaderType,
			fxpFilename,
			techniqueFamily,
			static_cast<std::uint32_t>(a_originalVertexDescriptor),
			static_cast<std::uint32_t>(a_originalPixelDescriptor),
			static_cast<std::uint32_t>(a_modifiedVertexDescriptor),
			static_cast<std::uint32_t>(a_modifiedPixelDescriptor),
			a_mutateState,
			a_reason);
	}

	void TracePreNGDFCompositeDescriptor(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto hullEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
		const auto domainEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto vertexDescriptor = static_cast<std::uint32_t>(a_vertexDescriptor);
		const auto pixelDescriptor = static_cast<std::uint32_t>(a_pixelDescriptor);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);

		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(ShaderStage::Vertex, *a_shader, vertexDescriptor, fxpFilename, vertexD3D != 0, vertexEntry, vertexD3D);
		shaderCache->ObserveDescriptorShader(ShaderStage::Pixel, *a_shader, pixelDescriptor, fxpFilename, pixelD3D != 0, pixelEntry, pixelD3D);
		const auto pixelDescriptorState = shaderCache->GetDescriptorShaderState(ShaderStage::Pixel, a_shader->shaderType, pixelDescriptor, fxpFilename);
		const auto descriptorCacheState = pixelDescriptorState ?
			(pixelDescriptorState->found ? "vanilla-observed" : "miss-observed") :
			"missing";
		const auto metadata = shaderCache->GetMetadataForD3DShaderObject(ShaderStage::Pixel, pixelD3D);

		const auto observation = ++s_preNGDFCompositeDescriptorObservations;
		if (observation > 16 && !IsPreNGPowerOfTwo(observation)) {
			return;
		}

		logger::info(
			"[BSShaderHooks] PreNG DFComposite descriptor observed observations={} shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} found={} currentVS=0x{:X} currentHS=0x{:X} currentDS=0x{:X} currentPS=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} descriptorCache={} metadata={} asm=0x{:08X} hash=0x{:08X} size={} buffers={} textures={} samples={} replacement=held bind=held",
			observation,
			a_shader->shaderType,
			fxpFilename,
			vertexDescriptor,
			static_cast<std::uint32_t>(a_hullDescriptor),
			static_cast<std::uint32_t>(a_domainDescriptor),
			pixelDescriptor,
			a_found,
			vertexEntry,
			hullEntry,
			domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			descriptorCacheState,
			metadata ? metadata->uid : "<none>",
			metadata ? metadata->asmHash : 0,
			metadata ? metadata->hash : 0,
			metadata ? metadata->size : 0,
			metadata ? std::to_string(metadata->constantBufferSizes[2]) : "<none>",
			metadata ? std::to_string(metadata->textureSlotMask) : "<none>",
			metadata ? std::to_string(metadata->sampleInstructionCount) : "<none>");
	}

	void ObservePreNGDFCompositeDescriptorShader(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_pixelDescriptor,
		bool a_found)
	{
		if (!IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const auto vertexEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
		const auto pixelEntry = ReadPreNGPointer(F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address());
		const auto vertexD3D = ReadPreNGShaderEntryD3DObject(vertexEntry);
		const auto pixelD3D = ReadPreNGShaderEntryD3DObject(pixelEntry);
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);

		auto* shaderCache = ShaderCache::GetSingleton();
		shaderCache->ObserveDescriptorShader(
			ShaderStage::Vertex,
			*a_shader,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			fxpFilename,
			vertexD3D != 0,
			vertexEntry,
			vertexD3D);
		shaderCache->ObserveDescriptorShader(
			ShaderStage::Pixel,
			*a_shader,
			static_cast<std::uint32_t>(a_pixelDescriptor),
			fxpFilename,
			a_found,
			pixelEntry,
			pixelD3D);
	}

	void LogPreNGDescriptorBind(
		RE::BSShader* a_shader,
		std::int32_t a_vertexDescriptor,
		std::int32_t a_hullDescriptor,
		std::int32_t a_domainDescriptor,
		std::int32_t a_pixelDescriptor,
		RE::BSGraphics::VertexShader* a_vertexShader,
		RE::BSGraphics::PixelShader* a_pixelShader,
		std::uintptr_t a_hullEntry,
		std::uintptr_t a_domainEntry,
		const char* a_bindState,
		const char* a_reason)
	{
		if (!IsPreNGLightingDescriptorShader(a_shader) && !IsPreNGDFCompositeDescriptorShader(a_shader)) {
			return;
		}

		const PreNGDescriptorBindDiagnosticKey key{
			a_shader->shaderType,
			a_vertexDescriptor,
			a_hullDescriptor,
			a_domainDescriptor,
			a_pixelDescriptor,
			a_bindState ? a_bindState : "<null>",
			a_reason ? a_reason : "<null>"
		};
		if (!ShouldLogPreNGDescriptorBind(key)) {
			return;
		}

		const auto vertexEntry = reinterpret_cast<std::uintptr_t>(a_vertexShader);
		const auto pixelEntry = reinterpret_cast<std::uintptr_t>(a_pixelShader);
		const auto vertexD3D = vertexEntry != 0 ? ReadPreNGShaderEntryD3DObject(vertexEntry) : 0;
		const auto pixelD3D = pixelEntry != 0 ? ReadPreNGShaderEntryD3DObject(pixelEntry) : 0;
		const auto fxpFilename = ReadPreNGCString(a_shader->fxpFilename, kPreNGMaxFxpFilenameLength);
		const auto compileState =
			a_vertexShader && a_pixelShader ?
				(IsPreNGDFLightLLFConsumerDescriptorShader(a_shader, static_cast<std::uint32_t>(a_pixelDescriptor)) ?
						"owned-pixel-current-vs-ready" :
						"owned-entry-ready") :
				"owned-entry-missing";

		logger::info(
			"[BSShaderHooks] PreNG descriptor custom bind shaderType={} fxp={} vsDesc=0x{:X} hsDesc=0x{:X} dsDesc=0x{:X} psDesc=0x{:X} vsEntry=0x{:X} hsEntry=0x{:X} dsEntry=0x{:X} psEntry=0x{:X} vsD3D=0x{:X} psD3D=0x{:X} customCompile={} customBind={} reason={}",
			a_shader->shaderType,
			fxpFilename,
			static_cast<std::uint32_t>(a_vertexDescriptor),
			static_cast<std::uint32_t>(a_hullDescriptor),
			static_cast<std::uint32_t>(a_domainDescriptor),
			static_cast<std::uint32_t>(a_pixelDescriptor),
			vertexEntry,
			a_hullEntry,
			a_domainEntry,
			pixelEntry,
			vertexD3D,
			pixelD3D,
			compileState,
			a_bindState,
			a_reason);
	}

#endif
}
