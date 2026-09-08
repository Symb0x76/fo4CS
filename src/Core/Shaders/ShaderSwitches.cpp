#include "Core/ShaderCache.h"

#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"
#include "Core/Feature.h"
#include "Core/PreNGEnvironment.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSShader.h"
#else
#include "RE/Bethesda/BSShader.h"
#endif
#include <RE/FO4Runtime.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

#include "Core/Shaders/ShaderCacheInternal.h"

// Debug.ini switch readers and the tunables derived from them. Each keeps its
// one-shot cache, so these definitions live here and nowhere else.
namespace CommunityShaders::shadercache
{
#if defined(FALLOUT_POST_NG)
		REX::W32::ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11VertexShader*>(a_shader);
		}

		REX::W32::ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return reinterpret_cast<REX::W32::ID3D11PixelShader*>(a_shader);
		}
#else
		ID3D11VertexShader* ToREVertexShader(ID3D11VertexShader* a_shader)
		{
			return a_shader;
		}

		ID3D11PixelShader* ToREPixelShader(ID3D11PixelShader* a_shader)
		{
			return a_shader;
		}
#endif

		bool ReadDescriptorEnvironmentSwitch(const char* a_name)
		{
			return DebugSwitches::ReadSwitchEnabled(a_name);
		}

		std::uint32_t ReadDescriptorEnvironmentUInt(
			const char* a_name,
			std::uint32_t a_defaultValue,
			std::uint32_t a_minValue,
			std::uint32_t a_maxValue)
		{
			return DebugSwitches::ReadUIntClamped(a_name, a_defaultValue, a_minValue, a_maxValue);
		}

		bool ReadDescriptorCompileSwitch()
		{
			return ReadDescriptorEnvironmentSwitch(PreNGEnvironment::kPreNGDescriptorCompileEnv);
		}

#if defined(FALLOUT_PRE_NG)
		bool ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerEnv);
				const bool unsafeOverride = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				if (requested && !unsafeOverride) {
					logger::warn(
						"[ShaderCache] PreNG DFLight full-shadowed descriptor compile held; DFLightFullShadowedPS is not vanilla-equivalent and can black out sky-light-only views. Set {}=1 only for focused diagnostics.",
						kPreNGDFLightFullShadowedDescriptorConsumerUnsafeEnv);
				}
				return requested && unsafeOverride;
			}();
			return enabled;
		}

		bool ShouldEnablePreNGDFLightFullContractVisibleLLF()
		{
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFLightFullContractVisibleLLFEnv);
			return enabled;
		}

		bool ShouldEnablePreNGDFLightForwardVisibleLLF()
		{
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFLightForwardLLFBindEnv);
			return enabled;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleStrictMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleStrictMaxLightsEnv,
				15,
				0,
				15);
			return maxLights;
		}

		std::uint32_t GetPreNGDFLightFullContractVisibleClusterMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFLightFullContractVisibleClusterMaxLightsEnv,
				64,
				0,
				64);
			return maxLights;
		}

		bool IsPreNGDFCompositeVisibleLLFDescriptor(std::uint32_t a_descriptor)
		{
			return a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 ||
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
		}

		bool ShouldEnablePreNGDFCompositeVisibleLLF()
		{
			static const bool enabled = [] {
				const bool requested = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeVisibleLLFEnv);
				if (requested) {
					logger::warn(
						"[ShaderCache] PreNG DFComposite visible LLF proof active; this screen-space contribution is diagnostic-only and uses the low-risk 0x88/0x10088 DFComposite consumers.");
				}
				return requested;
			}();
			return enabled;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFScale1024()
		{
			static const auto scale = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFScale1024Env,
				16,
				0,
				1024);
			return scale;
		}

		std::uint32_t GetPreNGDFCompositeVisibleLLFMaxLights()
		{
			static const auto maxLights = ReadDescriptorEnvironmentUInt(
				kPreNGDFCompositeVisibleLLFMaxLightsEnv,
				16,
				1,
				64);
			return maxLights;
		}
#endif
}
