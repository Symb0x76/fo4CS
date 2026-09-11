#include "Core/ShaderHooks/PostNGShaderLookupHook.h"

#include "Core/DebugSwitches.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Features/LightLimitFix.h"

#include <atomic>
#include <cstdint>

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSShader.h"
#include "RE/Bethesda/BSGraphics.h"
#endif

namespace CommunityShaders
{
#if !defined(FALLOUT_PRE_NG)

	namespace
	{
		std::atomic_uint32_t s_postNGBSLightingLLFConsumerBindAttempts = 0;
		std::atomic_uint32_t s_postNGBSLightingLLFConsumerBoundCount = 0;
	}

	bool ShouldObservePostNGBSLightingDescriptors()
	{
		return DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingDescriptorObserveEnv);
	}

	bool ShouldBindPostNGBSLightingLLFConsumerShader()
	{
		return DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingLLFBindEnv);
	}

	namespace
	{
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
	}

	namespace
	{
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
	}

	std::uint8_t PostNGShaderLookup::thunk(
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

#endif
}
