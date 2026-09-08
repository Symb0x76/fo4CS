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

// Descriptor classification: which .fxp/shader-type/descriptor combinations the
// LightLimitFix replacement pipeline may compile or bind, and the #define set
// each one compiles with.
namespace CommunityShaders::shadercache
{
		std::string ToLowerAscii(std::string a_value)
		{
			std::ranges::transform(a_value, a_value.begin(), [](unsigned char a_ch) {
				return static_cast<char>(std::tolower(a_ch));
			});
			return a_value;
		}

		bool StartsWith(std::string_view a_value, std::string_view a_prefix) noexcept
		{
			return a_value.size() >= a_prefix.size() &&
			       a_value.substr(0, a_prefix.size()) == a_prefix;
		}

		bool IsPreNGDFLightFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGDFLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGBSLightingFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGBSLightingFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFCompositeFxpName(std::string_view a_normalizedFxpFilename)
		{
#if defined(FALLOUT_PRE_NG)
			std::string name{ a_normalizedFxpFilename };
			if (name.empty() || name.front() == '<' || name.find('<') != std::string::npos) {
				return false;
			}
			if (const auto slash = name.find_last_of('/'); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			for (const auto extension : { std::string_view{ ".hlsl" }, std::string_view{ ".fxp" }, std::string_view{ ".fx" } }) {
				if (name.ends_with(extension)) {
					name.resize(name.size() - extension.size());
					break;
				}
			}
			return name == PreNGEnvironment::kPreNGDFCompositeFxpName;
#else
			(void)a_normalizedFxpFilename;
			return false;
#endif
		}

		bool IsPreNGDFLightFullShadowedDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGBSLightingContractPixelDescriptor(std::uint32_t a_descriptor)
		{
			return F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(a_descriptor);
		}

		bool IsPreNGBSLightingContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGBSLightingShaderType &&
			       IsPreNGBSLightingFxpName(a_normalizedFxpFilename) &&
			       IsPreNGBSLightingContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingContractShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingContractCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingConsumerCompileEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGBSLightingLLFBindEnv);
			return enabled &&
			       IsPreNGBSLightingContractDescriptorShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFLightFullContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool IsPreNGDFCompositeContractDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldCompilePreNGDFCompositeDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeDescriptorCompileEnv);
			return enabled &&
			       IsPreNGDFCompositeContractDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor);
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldEnablePreNGDFCompositeFogSafeBindShader()
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeFogSafeBindEnv);
			return enabled;
#else
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla40Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla88Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10040Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       ShouldEnablePreNGDFCompositeFogSafeBindShader() &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanilla10088Shader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			static const bool enabled = ReadDescriptorEnvironmentSwitch(kPreNGDFCompositeSafeBindEnv);
			return enabled &&
			       a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFCompositeShaderType &&
			       IsPreNGDFCompositeFxpName(a_normalizedFxpFilename) &&
			       a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088;
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool ShouldUsePreNGDFCompositeVanillaSafeBindShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return ShouldUsePreNGDFCompositeVanilla40Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla88Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10040Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanilla10088Shader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor);
		}

		bool IsPreNGDFLightLLFConsumerDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
#if defined(FALLOUT_PRE_NG)
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGDFLightingShaderType &&
			       IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			       (F4Runtime::PreNG::IsDFLightLLFConsumerPixelDescriptor(a_descriptor) ||
			        (ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer() &&
			         F4Runtime::PreNG::IsDFLightFullShadowedPixelDescriptor(a_descriptor)) ||
			        (ShouldEnablePreNGDFLightForwardVisibleLLF() &&
			         F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_descriptor)));
#else
			(void)a_stage;
			(void)a_shaderType;
			(void)a_normalizedFxpFilename;
			(void)a_descriptor;
			return false;
#endif
		}

		bool CanActivelyCompilePreNGLightingDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return a_shaderType == kPreNGBSLightingShaderType ||
			       IsPreNGDFLightLLFConsumerDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor) ||
			       IsPreNGDFCompositeContractDescriptorShader(a_stage, a_shaderType, a_normalizedFxpFilename, a_descriptor);
		}

		bool ShouldCompilePostNGBSLightingConsumerShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType)
		{
#if defined(FALLOUT_PRE_NG)
			(void)a_stage;
			(void)a_shaderType;
			return false;
#else
			return a_stage == ShaderStage::Pixel &&
			       a_shaderType == kPreNGBSLightingShaderType &&
			       (DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingLLFBindEnv) ||
			        DebugSwitches::ReadSwitchEnabled(kPostNGBSLightingDescriptorObserveEnv));
#endif
		}

		bool CanCompileDescriptorShader(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::string_view a_normalizedFxpFilename,
			std::uint32_t a_descriptor)
		{
			return (a_stage == ShaderStage::Pixel &&
			           a_shaderType == kPreNGBSLightingShaderType &&
			           F4Runtime::IsDeferredLightingPixelDescriptor(a_descriptor)) ||
			       ReadDescriptorCompileSwitch() ||
			       (a_stage == ShaderStage::Pixel &&
			           a_shaderType == kPreNGDFLightingShaderType &&
			           IsPreNGDFLightFxpName(a_normalizedFxpFilename) &&
			           ShouldEnablePreNGDFLightForwardVisibleLLF() &&
			           F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(a_descriptor)) ||
			       ShouldCompilePreNGBSLightingContractShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGBSLightingConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldUsePreNGDFCompositeVanillaSafeBindShader(
				       a_stage,
				       a_shaderType,
				       a_normalizedFxpFilename,
				       a_descriptor) ||
			       ShouldCompilePreNGDFCompositeDescriptorShader(
					   a_stage,
					   a_shaderType,
					   a_normalizedFxpFilename,
					   a_descriptor) ||
			       ShouldCompilePostNGBSLightingConsumerShader(a_stage, a_shaderType);
		}

		std::string BuildFeatureDefineList(const std::vector<D3D_SHADER_MACRO>& a_defines)
		{
			std::string result;
			for (const auto& define : a_defines) {
				if (!define.Name) {
					break;
				}
				if (!result.empty()) {
					result += ',';
				}
				result += define.Name;
			}
			return result.empty() ? "<none>" : result;
		}


		DescriptorDefineSet BuildDescriptorDefineSet(
			ShaderStage a_stage,
			std::int32_t a_shaderType,
			std::uint32_t a_descriptor)
		{
			DescriptorDefineSet result;

			for (auto* feature : Feature::GetFeatureList()) {
				if (!feature || !feature->loaded) {
					continue;
				}

				const auto name = feature->GetShaderDefineName();
				if (!name.empty()) {
					result.storage.emplace_back(std::string{ name }, "1");
				}
			}

			result.storage.emplace_back("FO4CS_DESCRIPTOR_SHADER", "1");
			result.storage.emplace_back("FO4CS_SHADER_TYPE", std::to_string(a_shaderType));
			result.storage.emplace_back("FO4CS_SHADER_DESCRIPTOR", std::to_string(a_descriptor));
			result.storage.emplace_back(
				a_stage == ShaderStage::Vertex ? "FO4CS_DESCRIPTOR_VERTEX_SHADER" : "FO4CS_DESCRIPTOR_PIXEL_SHADER",
				"1");

#if defined(FALLOUT_PRE_NG)
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGBSLightingShaderType &&
				F4Runtime::IsDeferredLightingPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("DEFERRED", "1");
				result.storage.emplace_back("FO4CS_DEFERRED_LIGHTING_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingContractShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_CONTRACT_DESCRIPTOR", "1");
			}
			if (ShouldCompilePreNGBSLightingConsumerShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
			}
			if (ShouldBindPreNGBSLightingLLFVisibleConsumerShader(
					a_stage,
					a_shaderType,
					PreNGEnvironment::kPreNGBSLightingFxpName,
					a_descriptor)) {
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_VISIBLE_CONSUMER", "1");
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFLightingShaderType &&
				F4Runtime::PreNG::IsDFLightFullContractPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_DESCRIPTOR", "1");
				if (ShouldEnablePreNGDFLightFullContractVisibleLLF()) {
					result.storage.emplace_back("FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_STRICT_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleStrictMaxLights()));
					result.storage.emplace_back(
						"FO4CS_DFLIGHT_FULL_CONTRACT_VISIBLE_CLUSTER_MAX_LIGHTS",
						std::to_string(GetPreNGDFLightFullContractVisibleClusterMaxLights()));
				}
			}
			if (a_stage == ShaderStage::Pixel &&
				a_shaderType == kPreNGDFCompositeShaderType &&
				F4Runtime::PreNG::IsDFCompositeObservedPixelDescriptor(a_descriptor)) {
				result.storage.emplace_back("FO4CS_DFCOMPOSITE_CONTRACT_DESCRIPTOR", "1");
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_40 &&
					ShouldUsePreNGDFCompositeVanilla40Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_40", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_88 &&
					ShouldUsePreNGDFCompositeVanilla88Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_88", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10040 &&
					ShouldUsePreNGDFCompositeVanilla10040Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10040", "1");
				}
				if (a_descriptor == F4Runtime::PreNG::DF_COMPOSITE_PIXEL_DESCRIPTOR_10088 &&
					ShouldUsePreNGDFCompositeVanilla10088Shader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VANILLA_10088", "1");
				}
				if (ShouldEnablePreNGDFCompositeVisibleLLF() &&
					IsPreNGDFCompositeVisibleLLFDescriptor(a_descriptor) &&
					ShouldUsePreNGDFCompositeVanillaSafeBindShader(
						a_stage,
						a_shaderType,
						PreNGEnvironment::kPreNGDFCompositeFxpName,
						a_descriptor)) {
					result.storage.emplace_back("FO4CS_DFCOMPOSITE_VISIBLE_LLF", "1");
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_SCALE_1024",
						std::to_string(GetPreNGDFCompositeVisibleLLFScale1024()));
					result.storage.emplace_back(
						"FO4CS_DFCOMPOSITE_VISIBLE_LLF_MAX_LIGHTS",
						std::to_string(GetPreNGDFCompositeVisibleLLFMaxLights()));
				}
			}
#else
			if (a_stage == ShaderStage::Pixel && a_shaderType == kPreNGBSLightingShaderType) {
				// FO4 forward consumer: clusters-only (no b3 strict-light buffer).
				result.storage.emplace_back("LLF_CLUSTERS_ONLY", "1");
				result.storage.emplace_back("FO4CS_BSLIGHTING_LLF_CONSUMER_DESCRIPTOR", "1");
			}
#endif

			result.macros.reserve(result.storage.size() + 1);
			for (auto& [name, value] : result.storage) {
				result.macros.push_back({ name.c_str(), value.c_str() });
			}
			result.macros.push_back({});
			return result;
		}

		std::string_view GetDescriptorTarget(ShaderStage a_stage) noexcept
		{
			return a_stage == ShaderStage::Vertex ? "vs_5_0" : "ps_5_0";
		}
}
