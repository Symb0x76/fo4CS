#include "Core/Hooks.h"

#include "Core/ShaderCache.h"

#include <bit>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
	namespace
	{
		using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
		using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11PixelShader**);
		using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11ComputeShader**);

		CreateVertexShaderFn createVertexShader = nullptr;
		CreatePixelShaderFn createPixelShader = nullptr;
		CreateComputeShaderFn createComputeShader = nullptr;
		bool installedDeviceHooks = false;

#if defined(FALLOUT_PRE_NG)
		using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
		using PSSetShaderResourcesFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
		using PSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
		using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
		using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
		using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
		using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
		using DrawAutoFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
		using DrawIndexedInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using DrawInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
		using IASetPrimitiveTopologyFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY);
		using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
		using RSSetViewportsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);
		using CopyResourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
		using ExecuteCommandListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);

		CreateDeferredContextFn createDeferredContext = nullptr;

		struct RenderTargetInfo
		{
			bool valid = false;
			UINT width = 0;
			UINT height = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		};

		struct DrawContextHooks
		{
			PSSetShaderResourcesFn psSetShaderResources = nullptr;
			PSSetShaderFn psSetShader = nullptr;
			DrawIndexedFn drawIndexed = nullptr;
			DrawFn draw = nullptr;
			DrawIndexedInstancedFn drawIndexedInstanced = nullptr;
			DrawInstancedFn drawInstanced = nullptr;
			DrawAutoFn drawAuto = nullptr;
			DrawIndexedInstancedIndirectFn drawIndexedInstancedIndirect = nullptr;
			DrawInstancedIndirectFn drawInstancedIndirect = nullptr;
			IASetPrimitiveTopologyFn iaSetPrimitiveTopology = nullptr;
			OMSetRenderTargetsFn omSetRenderTargets = nullptr;
			RSSetViewportsFn rsSetViewports = nullptr;
			CopyResourceFn copyResource = nullptr;
			ExecuteCommandListFn executeCommandList = nullptr;
		};

		DrawContextHooks fallbackDrawContextHooks;
		std::unordered_map<std::uintptr_t, DrawContextHooks> drawContextHooksByVTable;
		ID3D11Device* observedD3D11Device = nullptr;
		ID3D11DeviceContext* observedImmediateContext = nullptr;
		ID3D11DeviceContext* observedRendererContext = nullptr;
		bool installedContextHooks = false;
		bool rendererContextUnavailableLogged = false;
		bool llfOnFrameLogged = false;
		std::mutex llfCandidateLock;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> llfCandidatePixelShaders;
		std::unordered_set<std::string> loggedLLFPixelCandidates;
		std::unordered_set<std::string> loggedLLFDrawHookKinds;
		std::unordered_set<std::string> loggedLLFContextHookKinds;
		std::unordered_set<std::string> loggedLLFDrawContexts;
		std::unordered_set<std::string> loggedLLFContextDiagnostics;
		std::unordered_set<std::string> loggedLLFMissingOriginals;
		std::unordered_set<std::string> loggedLLFPixelShaderBindings;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundObservedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTrackedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTargetPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundUnknownPixelShaders;
		std::unordered_set<ID3D11PixelShader*> loggedLLFUnknownPixelShaderBindings;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> countedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyReasons;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyDraws;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderNearTargetDraws;
		std::unordered_map<std::string, std::size_t> llfBoundPixelShaderSurveyReasonCounts;
		bool loggedLLFBoundPixelShaderSurveyLimit = false;
		bool loggedLLFBoundPixelShaderSurveyDrawLimit = false;
		bool loggedLLFBoundPixelShaderNearTargetDrawLimit = false;
		std::unordered_map<std::string, std::size_t> llfStateSnapshotCounts;
		std::unordered_map<std::uintptr_t, std::uintptr_t> llfDirectDrawTrampolines;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> observedPixelShaderMetadata;
		std::unordered_map<ID3D11DeviceContext*, ShaderCache::ShaderMetadata> llfBoundPixelShaderMetadataByContext;

		constexpr std::size_t kMaxLLFStateSnapshotsPerShaderKind = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyLogs = 96;
		constexpr std::size_t kLLFBoundPixelShaderSurveySummaryInterval = 128;
		constexpr std::size_t kMaxLLFUnknownPixelShaderBindingLogs = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyDrawLogs = 128;
		constexpr std::size_t kMaxLLFBoundPixelShaderNearTargetDrawLogs = 64;
		constexpr std::size_t kLLFBoundPixelShaderInventorySummaryInterval = 128;
		constexpr UINT kMaxLLFLoggedShaderResourceViews = 8;

		std::string FormatBufferSlots(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
				const auto size = a_metadata.constantBufferSizes[slot];
				if (size == 0) {
					continue;
				}

				if (!first) {
					result << ',';
				}
				result << slot << ':' << size;
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::string FormatTextureSlots(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			for (std::size_t index = 0; index < a_metadata.textureSlots.size(); ++index) {
				if (index > 0) {
					result << ',';
				}
				result << a_metadata.textureSlots[index];
			}

			return a_metadata.textureSlots.empty() ? "none" : result.str();
		}

		std::string FormatTextureDimensions(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			for (std::size_t index = 0; index < a_metadata.textureDimensions.size(); ++index) {
				if (index > 0) {
					result << ',';
				}
				const auto [dimension, slot] = a_metadata.textureDimensions[index];
				result << dimension << '@' << slot;
			}

			return a_metadata.textureDimensions.empty() ? "none" : result.str();
		}

		std::string FormatTextureSampleCounts(const ShaderCache::ShaderMetadata& a_metadata)
		{
			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.textureSampleCounts.size(); ++slot) {
				const auto count = a_metadata.textureSampleCounts[slot];
				if (count == 0) {
					continue;
				}
				if (!first) {
					result << ',';
				}
				result << slot << ':' << count;
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::uintptr_t ToAddress(const void* a_pointer)
		{
			return reinterpret_cast<std::uintptr_t>(a_pointer);
		}

		template <class T>
		std::uintptr_t ToFunctionAddress(T a_function)
		{
			static_assert(sizeof(T) == sizeof(std::uintptr_t));
			return std::bit_cast<std::uintptr_t>(a_function);
		}

		std::uintptr_t GetContextVTablePointer(ID3D11DeviceContext* a_context)
		{
			if (!a_context) {
				return 0;
			}

			return *reinterpret_cast<std::uintptr_t*>(a_context);
		}

		std::string FormatDrawVTableFunctions(std::uintptr_t a_vtable)
		{
			if (!a_vtable) {
				return "none";
			}

			const auto* entries = reinterpret_cast<const std::uintptr_t*>(a_vtable);
			return std::format(
				"8=0x{:X},9=0x{:X},12=0x{:X},13=0x{:X},20=0x{:X},21=0x{:X},24=0x{:X},33=0x{:X},38=0x{:X},39=0x{:X},40=0x{:X},44=0x{:X},47=0x{:X},58=0x{:X}",
				entries[8],
				entries[9],
				entries[12],
				entries[13],
				entries[20],
				entries[21],
				entries[24],
				entries[33],
				entries[38],
				entries[39],
				entries[40],
				entries[44],
				entries[47],
				entries[58]);
		}

		void TraceLightLimitFixContextDiagnostics(const char* a_source, const char* a_phase, ID3D11DeviceContext* a_context, const void* a_rendererData, const void* a_rendererDevice)
		{
			if (!a_context) {
				return;
			}

			const auto contextAddress = ToAddress(a_context);
			const auto vtable = GetContextVTablePointer(a_context);
			const auto functions = FormatDrawVTableFunctions(vtable);
			const bool sameAsImmediate = observedImmediateContext && a_context == observedImmediateContext;
			const auto key = std::format("{}:{}:{:X}:{:X}:{}", a_source, a_phase, contextAddress, vtable, functions);
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFContextDiagnostics.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] PreNG context diagnostics source={} phase={} context=0x{:X} vtable=0x{:X} immediateContext=0x{:X} sameAsImmediate={} rendererData=0x{:X} rendererDevice=0x{:X} funcs={}",
				a_source,
				a_phase,
				contextAddress,
				vtable,
				ToAddress(observedImmediateContext),
				sameAsImmediate,
				ToAddress(a_rendererData),
				ToAddress(a_rendererDevice),
				functions);
		}

		bool ShouldEnableLightLimitFixPixelCandidateDiagnostics()
		{
			static const bool traceLLFEnv = GetEnvironmentVariableW(L"FO4CS_TRACE_LLF_PS", nullptr, 0) > 0;
			return traceLLFEnv;
		}

		bool ShouldTraceLLFPixelCandidates(const ShaderCache& a_cache)
		{
			(void)a_cache;
			return ShouldEnableLightLimitFixPixelCandidateDiagnostics();
		}

		bool HasTextureDimension(const ShaderCache::ShaderMetadata& a_metadata, std::uint32_t a_dimension, std::uint32_t a_slot)
		{
			for (const auto [dimension, slot] : a_metadata.textureDimensions) {
				if (dimension == a_dimension && slot == a_slot) {
					return true;
				}
			}

			return false;
		}

		bool IsLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       !a_metadata.hasImmediateConstantBuffer &&
			       a_metadata.immediateConstantBufferRows == 0;
		}

		bool IsLightLimitFixPixelImmediateConstantNearTarget(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0);
		}

		bool IsLightLimitFixPixelTrackedCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			return IsLightLimitFixPixelCandidate(a_metadata) ||
			       IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata);
		}

		bool IsLightLimitFixPixelSurveyMatch(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			return hasLightCB && hasLightingTexture && a_metadata.outputCount == 2;
		}

		std::string FormatLightLimitFixPixelShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTextureCubeAtSlot5 = HasTextureDimension(a_metadata, 5, 5);
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return std::format(
				"cb2={} t5={} tex5dim={} out2={} posInput={} target={} tracked={} samples={} t5samples={} immRows={}",
				hasLightCB,
				hasLightingTexture,
				hasTextureCubeAtSlot5,
				hasTwoOutputs,
				positionOnlyInput,
				IsLightLimitFixPixelCandidate(a_metadata),
				IsLightLimitFixPixelTrackedCandidate(a_metadata),
				a_metadata.sampleInstructionCount,
				a_metadata.textureSampleCounts[5],
				a_metadata.immediateConstantBufferRows);
		}

		std::string ClassifyLightLimitFixSurveyRejection(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "target";
			}

			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			if (!hasLightCB) {
				return "noCB2";
			}

			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			if (!hasLightingTexture) {
				return "noT5";
			}

			if (!HasTextureDimension(a_metadata, 5, 5)) {
				return "noT5Cube";
			}

			if (a_metadata.textureSampleCounts[5] == 0) {
				return "noT5Sample";
			}

			if (a_metadata.outputCount != 2) {
				return "output";
			}

			if (a_metadata.inputCount != 1 || a_metadata.inputMask != 0x1) {
				return "input";
			}

			if (a_metadata.hasDiscard) {
				return "discard";
			}

			if (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0) {
				return "immediateCB";
			}

			return "other";
		}

		std::string FormatLightLimitFixSurveyReasonCountsLocked()
		{
			constexpr std::string_view kReasonOrder[] = {
				"noCB2",
				"noT5",
				"noT5Cube",
				"noT5Sample",
				"output",
				"input",
				"discard",
				"immediateCB",
				"other"
			};

			std::ostringstream result;
			bool first = true;
			for (const auto reason : kReasonOrder) {
				const auto it = llfBoundPixelShaderSurveyReasonCounts.find(std::string(reason));
				if (it == llfBoundPixelShaderSurveyReasonCounts.end() || it->second == 0) {
					continue;
				}

				if (!first) {
					result << ',';
				}
				result << reason << ':' << it->second;
				first = false;
			}

			return first ? "none" : result.str();
		}

		bool ShouldSurveyBoundPixelShader(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return IsLightLimitFixPixelSurveyMatch(a_metadata) ||
			       hasLightCB ||
			       hasLightingTexture ||
			       (hasTwoOutputs && positionOnlyInput);
		}

		std::string ClassifyLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "fullscreen-lighting";
			}

			if (IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata)) {
				return "fullscreen-lighting-immediate";
			}

			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			if (!positionOnlyInput || a_metadata.hasDiscard) {
				return "geometry-material";
			}

			if (a_metadata.hasImmediateConstantBuffer) {
				return "fullscreen-other";
			}

			return HasTextureDimension(a_metadata, 5, 5) ? "fullscreen-lighting" : "fullscreen-other";
		}

		std::size_t CountTrackedLightLimitFixPixelTargetsLocked()
		{
			std::size_t result = 0;
			for (const auto& [shader, metadata] : llfCandidatePixelShaders) {
				if (IsLightLimitFixPixelCandidate(metadata)) {
					++result;
				}
			}

			return result;
		}

		std::optional<ShaderCache::ShaderMetadata> GetTracePixelShaderMetadata(const void* a_bytecode, SIZE_T a_bytecodeLength)
		{
			auto* cache = ShaderCache::GetSingleton();
			if (!ShouldTraceLLFPixelCandidates(*cache)) {
				return std::nullopt;
			}

			return cache->GetMetadataForBytecode(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
		}

		void TrackLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_pixelShader) {
				return;
			}

			std::scoped_lock lock(llfCandidateLock);
			llfCandidatePixelShaders[a_pixelShader] = a_metadata;
		}

		void TrackObservedPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_pixelShader) {
				return;
			}

			std::scoped_lock lock(llfCandidateLock);
			observedPixelShaderMetadata[a_pixelShader] = a_metadata;
		}

		void TraceLightLimitFixPixelCandidate(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
		{
			const auto key = std::format("{}:{:08X}", a_metadata.uid, a_metadata.hash);
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFPixelCandidates.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] Candidate PS asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} device=0x{:X} shader=0x{:X} size={} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				a_metadata.asmHash,
				a_metadata.hash,
				a_metadata.uid,
				ClassifyLightLimitFixPixelCandidate(a_metadata),
				IsLightLimitFixPixelCandidate(a_metadata),
				ToAddress(a_device),
				ToAddress(a_pixelShader),
				a_metadata.size,
				FormatBufferSlots(a_metadata),
				FormatTextureSlots(a_metadata),
				FormatTextureDimensions(a_metadata),
				a_metadata.inputCount,
				a_metadata.outputCount,
				a_metadata.inputMask,
				a_metadata.outputMask,
				a_metadata.instructionCount,
				a_metadata.sampleInstructionCount,
				FormatTextureSampleCounts(a_metadata),
				a_metadata.hasDiscard,
				a_metadata.hasImmediateConstantBuffer,
				a_metadata.immediateConstantBufferRows);
		}

		std::optional<ShaderCache::ShaderMetadata> GetTrackedLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = llfCandidatePixelShaders.find(a_pixelShader); it != llfCandidatePixelShaders.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		std::optional<ShaderCache::ShaderMetadata> GetObservedPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = observedPixelShaderMetadata.find(a_pixelShader); it != observedPixelShaderMetadata.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		void TrackLightLimitFixBoundPixelShader(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context) {
				return;
			}

			auto metadata = a_pixelShader ? GetTrackedLightLimitFixPixelShader(a_pixelShader) : std::nullopt;
			std::scoped_lock lock(llfCandidateLock);
			if (metadata) {
				llfBoundPixelShaderMetadataByContext[a_context] = *metadata;
			} else {
				llfBoundPixelShaderMetadataByContext.erase(a_context);
			}
		}

		std::optional<ShaderCache::ShaderMetadata> GetBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context) {
				return std::nullopt;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = llfBoundPixelShaderMetadataByContext.find(a_context); it != llfBoundPixelShaderMetadataByContext.end()) {
					return it->second;
				}
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return std::nullopt;
			}

			return GetTrackedLightLimitFixPixelShader(pixelShader.get());
		}

		bool HasCachedBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return false;
			}

			std::scoped_lock lock(llfCandidateLock);
			return llfBoundPixelShaderMetadataByContext.find(a_context) != llfBoundPixelShaderMetadataByContext.end();
		}

		void TraceLightLimitFixDrawHookHealth(const char* a_drawKind)
		{
			auto* cache = ShaderCache::GetSingleton();
			if (!ShouldTraceLLFPixelCandidates(*cache)) {
				return;
			}

			std::size_t trackedCandidateCount = 0;
			std::size_t trackedTargetCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFDrawHookKinds.insert(a_drawKind).second) {
					return;
				}
				trackedCandidateCount = llfCandidatePixelShaders.size();
				trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
			}

			logger::info(
				"[LightLimitFix] PreNG draw hook reached draw={} trackedCandidatePS={} trackedTargetPS={}",
				a_drawKind,
				trackedCandidateCount,
				trackedTargetCount);
		}

		void TraceLightLimitFixContextHookHealth(ID3D11DeviceContext* a_context, const char* a_hookKind)
		{
			auto* cache = ShaderCache::GetSingleton();
			if (!ShouldTraceLLFPixelCandidates(*cache)) {
				return;
			}

			std::size_t trackedCandidateCount = 0;
			std::size_t trackedTargetCount = 0;
			const auto key = std::format("{}:{:X}:{:X}", a_hookKind, ToAddress(a_context), GetContextVTablePointer(a_context));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFContextHookKinds.insert(key).second) {
					return;
				}
				trackedCandidateCount = llfCandidatePixelShaders.size();
				trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
			}

			logger::info(
				"[LightLimitFix] PreNG context hook reached kind={} context=0x{:X} vtable=0x{:X} trackedCandidatePS={} trackedTargetPS={}",
				a_hookKind,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				trackedCandidateCount,
				trackedTargetCount);
		}

		void TraceLightLimitFixPixelShaderBinding(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetTrackedLightLimitFixPixelShader(a_pixelShader);
			if (!metadata) {
				return;
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto key = std::format("{}:{:08X}:{:X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_context), ToAddress(a_pixelShader));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFPixelShaderBindings.insert(key).second) {
					return;
				}
			}

			logger::info(
				"[LightLimitFix] Candidate PS bound asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader),
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		void TraceLightLimitFixBoundPixelShaderInventory(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetObservedPixelShader(a_pixelShader);
			const bool latestObserved = metadata.has_value();
			const bool latestTracked = metadata && IsLightLimitFixPixelTrackedCandidate(*metadata);
			const bool latestTarget = metadata && IsLightLimitFixPixelCandidate(*metadata);

			bool shouldLogSummary = false;
			bool shouldLogUnknownSample = false;
			std::size_t boundUniqueCount = 0;
			std::size_t observedBoundCount = 0;
			std::size_t unknownBoundCount = 0;
			std::size_t trackedBoundCount = 0;
			std::size_t targetBoundCount = 0;
			std::size_t observedCreatedCount = 0;
			std::size_t trackedCreatedCount = 0;
			std::size_t targetCreatedCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				const bool inserted = countedLLFBoundPixelShaders.insert(a_pixelShader).second;
				if (latestObserved) {
					countedLLFBoundObservedPixelShaders.insert(a_pixelShader);
				} else {
					countedLLFBoundUnknownPixelShaders.insert(a_pixelShader);
					if (loggedLLFUnknownPixelShaderBindings.size() < kMaxLLFUnknownPixelShaderBindingLogs &&
						loggedLLFUnknownPixelShaderBindings.insert(a_pixelShader).second) {
						shouldLogUnknownSample = true;
					}
				}

				if (latestTracked) {
					countedLLFBoundTrackedPixelShaders.insert(a_pixelShader);
				}

				if (latestTarget) {
					countedLLFBoundTargetPixelShaders.insert(a_pixelShader);
				}

				boundUniqueCount = countedLLFBoundPixelShaders.size();
				observedBoundCount = countedLLFBoundObservedPixelShaders.size();
				unknownBoundCount = countedLLFBoundUnknownPixelShaders.size();
				trackedBoundCount = countedLLFBoundTrackedPixelShaders.size();
				targetBoundCount = countedLLFBoundTargetPixelShaders.size();
				observedCreatedCount = observedPixelShaderMetadata.size();
				trackedCreatedCount = llfCandidatePixelShaders.size();
				targetCreatedCount = CountTrackedLightLimitFixPixelTargetsLocked();

				shouldLogSummary = inserted &&
					(boundUniqueCount == 1 ||
					 boundUniqueCount == 32 ||
					 boundUniqueCount == 64 ||
					 boundUniqueCount % kLLFBoundPixelShaderInventorySummaryInterval == 0 ||
					 latestTracked ||
					 latestTarget);
			}

			if (shouldLogUnknownSample) {
				logger::info(
					"[LightLimitFix] Bound PS unknown sample uniqueUnknown={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
					unknownBoundCount,
					ToAddress(a_context),
					GetContextVTablePointer(a_context),
					ToAddress(a_pixelShader));
			}

			if (!shouldLogSummary) {
				return;
			}

			logger::info(
				"[LightLimitFix] Bound PS inventory unique={} observed={} unknown={} trackedBound={} targetBound={} observedCreated={} trackedCreated={} targetCreated={} latestObserved={} latestTracked={} latestTarget={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
				boundUniqueCount,
				observedBoundCount,
				unknownBoundCount,
				trackedBoundCount,
				targetBoundCount,
				observedCreatedCount,
				trackedCreatedCount,
				targetCreatedCount,
				latestObserved,
				latestTracked,
				latestTarget,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader));
		}

		void TraceLightLimitFixBoundPixelShaderSurvey(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
		{
			if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetObservedPixelShader(a_pixelShader);
			if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
				return;
			}

			const auto key = std::format("{}:{:08X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_pixelShader));
			const auto reason = ClassifyLightLimitFixSurveyRejection(*metadata);
			bool limitReached = false;
			bool shouldLogReasonSample = false;
			bool shouldLogSummary = false;
			bool shouldLog = false;
			std::size_t uniqueSurveyCount = 0;
			std::size_t exampleCount = 0;
			std::string reasonCounts;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!countedLLFBoundPixelShaderSurvey.insert(key).second) {
					return;
				}

				++llfBoundPixelShaderSurveyReasonCounts[reason];
				uniqueSurveyCount = countedLLFBoundPixelShaderSurvey.size();

				if (loggedLLFBoundPixelShaderSurveyReasons.insert(reason).second) {
					shouldLogReasonSample = true;
				}

				if (loggedLLFBoundPixelShaderSurvey.size() < kMaxLLFBoundPixelShaderSurveyLogs) {
					loggedLLFBoundPixelShaderSurvey.insert(key);
					shouldLog = true;
				} else if (!loggedLLFBoundPixelShaderSurveyLimit) {
					loggedLLFBoundPixelShaderSurveyLimit = true;
					limitReached = true;
				}

				exampleCount = loggedLLFBoundPixelShaderSurvey.size();
				if (uniqueSurveyCount == kMaxLLFBoundPixelShaderSurveyLogs + 1 ||
					(uniqueSurveyCount > kMaxLLFBoundPixelShaderSurveyLogs && uniqueSurveyCount % kLLFBoundPixelShaderSurveySummaryInterval == 0)) {
					shouldLogSummary = true;
					reasonCounts = FormatLightLimitFixSurveyReasonCountsLocked();
				}
			}

			if (shouldLogSummary) {
				logger::info(
					"[LightLimitFix] Bound PS survey reason summary unique={} examplesLogged={} counts={}",
					uniqueSurveyCount,
					exampleCount,
					reasonCounts);
			}

			if (shouldLogReasonSample && !shouldLog) {
				logger::info(
					"[LightLimitFix] Bound PS survey reason sample reason={} asmHash=0x{:08X} hash=0x{:08X} uid={} shape=\"{}\" buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
					reason,
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					FormatLightLimitFixPixelShape(*metadata),
					FormatBufferSlots(*metadata),
					FormatTextureSlots(*metadata),
					FormatTextureDimensions(*metadata),
					metadata->inputCount,
					metadata->outputCount,
					metadata->inputMask,
					metadata->outputMask,
					metadata->instructionCount,
					metadata->sampleInstructionCount,
					FormatTextureSampleCounts(*metadata),
					metadata->hasDiscard,
					metadata->hasImmediateConstantBuffer,
					metadata->immediateConstantBufferRows);
			}

			if (limitReached) {
				logger::info(
					"[LightLimitFix] Bound PS survey limit reached; suppressing additional noncandidate bound shader summaries limit={} unique={} counts={}",
					kMaxLLFBoundPixelShaderSurveyLogs,
					uniqueSurveyCount,
					reasonCounts.empty() ? "none" : reasonCounts);
			}
			if (!shouldLog) {
				return;
			}

			logger::info(
				"[LightLimitFix] Bound PS survey asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				reason,
				FormatLightLimitFixPixelShape(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader),
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->inputCount,
				metadata->outputCount,
				metadata->inputMask,
				metadata->outputMask,
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		RenderTargetInfo GetRenderTargetInfo(ID3D11RenderTargetView* a_renderTargetView)
		{
			RenderTargetInfo result;
			if (!a_renderTargetView) {
				return result;
			}

			result.valid = true;

			D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
			a_renderTargetView->GetDesc(&viewDesc);
			result.format = viewDesc.Format;

			ID3D11Resource* resource = nullptr;
			a_renderTargetView->GetResource(&resource);
			if (!resource) {
				return result;
			}

			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				result.width = textureDesc.Width;
				result.height = textureDesc.Height;
				if (result.format == DXGI_FORMAT_UNKNOWN) {
					result.format = textureDesc.Format;
				}
				texture->Release();
			}
			resource->Release();

			return result;
		}

		std::string FormatRenderTargetInfo(const RenderTargetInfo& a_info)
		{
			if (!a_info.valid) {
				return "none";
			}

			return std::format("{}x{}:fmt{}", a_info.width, a_info.height, static_cast<std::uint32_t>(a_info.format));
		}

		std::string FormatViewport(const D3D11_VIEWPORT& a_viewport, UINT a_viewportCount)
		{
			if (a_viewportCount == 0) {
				return "none";
			}

			return std::format(
				"{:.0f}x{:.0f}+{:.0f},{:.0f}",
				a_viewport.Width,
				a_viewport.Height,
				a_viewport.TopLeftX,
				a_viewport.TopLeftY);
		}

		std::string FormatShaderResourceViewInfo(ID3D11ShaderResourceView* a_shaderResourceView)
		{
			if (!a_shaderResourceView) {
				return "null";
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
			a_shaderResourceView->GetDesc(&viewDesc);

			ID3D11Resource* resource = nullptr;
			a_shaderResourceView->GetResource(&resource);
			if (!resource) {
				return std::format("viewDim{}:no-resource", static_cast<std::uint32_t>(viewDesc.ViewDimension));
			}

			std::string result;
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				result = std::format(
					"viewDim{}:{}x{}:fmt{}",
					static_cast<std::uint32_t>(viewDesc.ViewDimension),
					textureDesc.Width,
					textureDesc.Height,
					static_cast<std::uint32_t>(textureDesc.Format));
				texture->Release();
			} else {
				D3D11_RESOURCE_DIMENSION resourceDimension{};
				resource->GetType(&resourceDimension);
				result = std::format(
					"viewDim{}:resourceDim{}:fmt{}",
					static_cast<std::uint32_t>(viewDesc.ViewDimension),
					static_cast<std::uint32_t>(resourceDimension),
					static_cast<std::uint32_t>(viewDesc.Format));
			}

			resource->Release();
			return result;
		}

		std::string FormatShaderResourceViews(UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews)
		{
			if (!a_shaderResourceViews || a_viewCount == 0) {
				return "none";
			}

			std::ostringstream result;
			const auto loggedViewCount = a_viewCount < kMaxLLFLoggedShaderResourceViews ? a_viewCount : kMaxLLFLoggedShaderResourceViews;
			for (UINT index = 0; index < loggedViewCount; ++index) {
				if (index > 0) {
					result << ',';
				}
				result << (a_startSlot + index) << '=' << FormatShaderResourceViewInfo(a_shaderResourceViews[index]);
			}
			if (a_viewCount > loggedViewCount) {
				result << ",...+" << (a_viewCount - loggedViewCount);
			}
			return result.str();
		}

		std::string FormatConstantBufferInfo(ID3D11Buffer* a_buffer)
		{
			if (!a_buffer) {
				return "null";
			}

			D3D11_BUFFER_DESC desc{};
			a_buffer->GetDesc(&desc);
			return std::format(
				"{}:usage{}:bind0x{:X}:cpu0x{:X}",
				desc.ByteWidth,
				static_cast<std::uint32_t>(desc.Usage),
				desc.BindFlags,
				desc.CPUAccessFlags);
		}

		std::string FormatCurrentPixelShaderConstantBuffers(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_context) {
				return "none";
			}

			std::ostringstream result;
			bool first = true;
			for (std::size_t slot = 0; slot < a_metadata.constantBufferSizes.size(); ++slot) {
				if (a_metadata.constantBufferSizes[slot] == 0 || slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
					continue;
				}

				ID3D11Buffer* buffer = nullptr;
				a_context->PSGetConstantBuffers(static_cast<UINT>(slot), 1, &buffer);
				if (!first) {
					result << ',';
				}
				result << "cb" << slot << '=' << FormatConstantBufferInfo(buffer);
				if (buffer) {
					buffer->Release();
				}
				first = false;
			}

			return first ? "none" : result.str();
		}

		std::string FormatCurrentPixelShaderResourceViews(ID3D11DeviceContext* a_context, const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (!a_context || a_metadata.textureSlots.empty()) {
				return "none";
			}

			std::ostringstream result;
			bool first = true;
			std::unordered_set<std::uint32_t> seenSlots;
			for (const auto slot : a_metadata.textureSlots) {
				if (slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT || !seenSlots.insert(slot).second) {
					continue;
				}

				ID3D11ShaderResourceView* view = nullptr;
				a_context->PSGetShaderResources(slot, 1, &view);
				if (!first) {
					result << ',';
				}
				result << slot << '=' << FormatShaderResourceViewInfo(view);
				if (view) {
					view->Release();
				}
				first = false;
			}

			return first ? "none" : result.str();
		}

		void TraceLightLimitFixStateContext(ID3D11DeviceContext* a_context, const char* a_stateKind, std::string_view a_stateDetails)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			auto metadata = GetBoundLightLimitFixPixelShader(a_context);
			if (!metadata) {
				return;
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto snapshotKey = std::format("{:08X}:{}:{}", metadata->asmHash, category, a_stateKind);
			std::size_t snapshotCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				snapshotCount = ++llfStateSnapshotCounts[snapshotKey];
			}
			if (snapshotCount > kMaxLLFStateSnapshotsPerShaderKind) {
				if (snapshotCount == kMaxLLFStateSnapshotsPerShaderKind + 1) {
					logger::info(
						"[LightLimitFix] Candidate PS state limit reached asmHash=0x{:08X} uid={} category={} target={} kind={} limit={}",
						metadata->asmHash,
						metadata->uid,
						category,
						IsLightLimitFixPixelCandidate(*metadata),
						a_stateKind,
						kMaxLLFStateSnapshotsPerShaderKind);
				}
				return;
			}

			D3D11_PRIMITIVE_TOPOLOGY topology{};
			a_context->IAGetPrimitiveTopology(&topology);

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			a_context->RSGetViewports(&viewportCount, &viewport);
			const auto viewportDescription = FormatViewport(viewport, viewportCount);

			ID3D11RenderTargetView* renderTargets[2]{};
			a_context->OMGetRenderTargets(2, renderTargets, nullptr);
			const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
			const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
			for (auto* renderTarget : renderTargets) {
				if (renderTarget) {
					renderTarget->Release();
				}
			}

			const auto rt0Description = FormatRenderTargetInfo(rt0);
			const auto rt1Description = FormatRenderTargetInfo(rt1);
			const auto details = std::string{ a_stateDetails };

			logger::info(
				"[LightLimitFix] Candidate PS state kind={} asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} topology={} viewport={} rt0={} rt1={} state={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				a_stateKind,
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				details,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		void TraceLightLimitFixDrawContext(ID3D11DeviceContext* a_context, const char* a_drawKind, std::string_view a_drawCounts)
		{
			if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return;
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return;
			}

			auto metadata = GetTrackedLightLimitFixPixelShader(pixelShader.get());
			bool surveyDraw = false;
			std::string surveyReason;
			if (!metadata) {
				metadata = GetObservedPixelShader(pixelShader.get());
				if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
					return;
				}

				surveyDraw = true;
				surveyReason = ClassifyLightLimitFixSurveyRejection(*metadata);
			}

			D3D11_PRIMITIVE_TOPOLOGY topology{};
			a_context->IAGetPrimitiveTopology(&topology);

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			a_context->RSGetViewports(&viewportCount, &viewport);
			const auto viewportDescription = FormatViewport(viewport, viewportCount);

			ID3D11RenderTargetView* renderTargets[2]{};
			a_context->OMGetRenderTargets(2, renderTargets, nullptr);
			const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
			const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
			for (auto* renderTarget : renderTargets) {
				if (renderTarget) {
					renderTarget->Release();
				}
			}

			const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
			const auto rt0Description = FormatRenderTargetInfo(rt0);
			const auto rt1Description = FormatRenderTargetInfo(rt1);
			const auto key = std::format(
				"{:08X}:{}:{}:{}:{}:{}:{}:{}",
				metadata->asmHash,
				metadata->uid,
				category,
				a_drawKind,
				a_drawCounts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description + ":" + rt1Description);

			if (surveyDraw && IsLightLimitFixPixelImmediateConstantNearTarget(*metadata)) {
				bool shouldLogNearTargetDraw = false;
				bool shouldLogNearTargetDrawLimit = false;
				std::size_t nearTargetDrawUniqueCount = 0;
				{
					std::scoped_lock lock(llfCandidateLock);
					if (loggedLLFBoundPixelShaderNearTargetDraws.size() < kMaxLLFBoundPixelShaderNearTargetDrawLogs) {
						shouldLogNearTargetDraw = loggedLLFBoundPixelShaderNearTargetDraws.insert(key).second;
					} else if (!loggedLLFBoundPixelShaderNearTargetDrawLimit) {
						loggedLLFBoundPixelShaderNearTargetDrawLimit = true;
						shouldLogNearTargetDrawLimit = true;
					}
					nearTargetDrawUniqueCount = loggedLLFBoundPixelShaderNearTargetDraws.size();
				}

				if (shouldLogNearTargetDrawLimit) {
					logger::info(
						"[LightLimitFix] Bound PS near-target draw limit reached unique={} limit={}",
						nearTargetDrawUniqueCount,
						kMaxLLFBoundPixelShaderNearTargetDrawLogs);
				}

				if (shouldLogNearTargetDraw) {
					const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
					const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
					logger::info(
						"[LightLimitFix] Bound PS near-target draw asmHash=0x{:08X} hash=0x{:08X} uid={} reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} boundCBs={} boundSRVs={}",
						metadata->asmHash,
						metadata->hash,
						metadata->uid,
						surveyReason,
						FormatLightLimitFixPixelShape(*metadata),
						a_drawKind,
						a_drawCounts,
						static_cast<std::uint32_t>(topology),
						viewportDescription,
						rt0Description,
						rt1Description,
						FormatBufferSlots(*metadata),
						FormatTextureSlots(*metadata),
						FormatTextureDimensions(*metadata),
						metadata->instructionCount,
						metadata->sampleInstructionCount,
						FormatTextureSampleCounts(*metadata),
						boundConstantBuffers,
						boundShaderResources);
				}
			}

			if (surveyDraw) {
				bool shouldLogSurveyDraw = false;
				bool shouldLogSurveyDrawLimit = false;
				std::size_t surveyDrawUniqueCount = 0;
				{
					std::scoped_lock lock(llfCandidateLock);
					if (loggedLLFBoundPixelShaderSurveyDraws.size() < kMaxLLFBoundPixelShaderSurveyDrawLogs) {
						shouldLogSurveyDraw = loggedLLFBoundPixelShaderSurveyDraws.insert(key).second;
					} else if (!loggedLLFBoundPixelShaderSurveyDrawLimit) {
						loggedLLFBoundPixelShaderSurveyDrawLimit = true;
						shouldLogSurveyDrawLimit = true;
					}
					surveyDrawUniqueCount = loggedLLFBoundPixelShaderSurveyDraws.size();
				}

				if (shouldLogSurveyDrawLimit) {
					logger::info(
						"[LightLimitFix] Bound PS survey draw limit reached unique={} limit={}",
						surveyDrawUniqueCount,
						kMaxLLFBoundPixelShaderSurveyDrawLogs);
				}

				if (!shouldLogSurveyDraw) {
					return;
				}

				logger::info(
					"[LightLimitFix] Bound PS survey draw asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					surveyReason,
					FormatLightLimitFixPixelShape(*metadata),
					a_drawKind,
					a_drawCounts,
					static_cast<std::uint32_t>(topology),
					viewportDescription,
					rt0Description,
					rt1Description,
					FormatBufferSlots(*metadata),
					FormatTextureSlots(*metadata),
					FormatTextureDimensions(*metadata),
					metadata->instructionCount,
					metadata->sampleInstructionCount,
					FormatTextureSampleCounts(*metadata),
					metadata->hasDiscard,
					metadata->hasImmediateConstantBuffer,
					metadata->immediateConstantBufferRows);
				return;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFDrawContexts.insert(key).second) {
					return;
				}
			}

			const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
			const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
			logger::info(
				"[LightLimitFix] Candidate PS draw asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={} boundCBs={} boundSRVs={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				category,
				IsLightLimitFixPixelCandidate(*metadata),
				a_drawKind,
				a_drawCounts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows,
				boundConstantBuffers,
				boundShaderResources);
		}

		DrawContextHooks GetDrawContextHooksForContext(ID3D11DeviceContext* a_context)
		{
			const auto vtable = GetContextVTablePointer(a_context);
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = drawContextHooksByVTable.find(vtable); it != drawContextHooksByVTable.end()) {
				return it->second;
			}

			return fallbackDrawContextHooks;
		}

		void TraceMissingContextOriginal(ID3D11DeviceContext* a_context, const char* a_hookKind)
		{
			const auto key = std::format("{}:{:X}:{:X}", a_hookKind, ToAddress(a_context), GetContextVTablePointer(a_context));
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!loggedLLFMissingOriginals.insert(key).second) {
					return;
				}
			}

			logger::error(
				"[LightLimitFix] PreNG context hook missing original kind={} context=0x{:X} vtable=0x{:X}",
				a_hookKind,
				ToAddress(a_context),
				GetContextVTablePointer(a_context));
		}

		void STDMETHODCALLTYPE PSSetShaderHook(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader, ID3D11ClassInstance* const* a_classInstances, UINT a_classInstancesCount)
		{
			TraceLightLimitFixContextHookHealth(a_context, "PSSetShader");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.psSetShader) {
				hooks.psSetShader(a_context, a_pixelShader, a_classInstances, a_classInstancesCount);
				TraceLightLimitFixBoundPixelShaderInventory(a_context, a_pixelShader);
				TrackLightLimitFixBoundPixelShader(a_context, a_pixelShader);
				TraceLightLimitFixPixelShaderBinding(a_context, a_pixelShader);
				TraceLightLimitFixBoundPixelShaderSurvey(a_context, a_pixelShader);
				TraceLightLimitFixStateContext(a_context, "PSSetShader", std::format("shader=0x{:X}", ToAddress(a_pixelShader)));
			} else {
				TraceMissingContextOriginal(a_context, "PSSetShader");
			}
		}

		void STDMETHODCALLTYPE PSSetShaderResourcesHook(ID3D11DeviceContext* a_context, UINT a_startSlot, UINT a_viewCount, ID3D11ShaderResourceView* const* a_shaderResourceViews)
		{
			TraceLightLimitFixContextHookHealth(a_context, "PSSetShaderResources");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.psSetShaderResources) {
				hooks.psSetShaderResources(a_context, a_startSlot, a_viewCount, a_shaderResourceViews);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"PSSetShaderResources",
						std::format(
							"startSlot={} viewCount={} views={}",
							a_startSlot,
							a_viewCount,
							FormatShaderResourceViews(a_startSlot, a_viewCount, a_shaderResourceViews)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "PSSetShaderResources");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedHook(ID3D11DeviceContext* a_context, UINT a_indexCount, UINT a_startIndexLocation, INT a_baseVertexLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexed");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexed",
				std::format("indexCount={} startIndex={} baseVertex={}", a_indexCount, a_startIndexLocation, a_baseVertexLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexed) {
				hooks.drawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexed");
			}
		}

		void STDMETHODCALLTYPE DrawHook(ID3D11DeviceContext* a_context, UINT a_vertexCount, UINT a_startVertexLocation)
		{
			TraceLightLimitFixDrawHookHealth("Draw");
			TraceLightLimitFixDrawContext(
				a_context,
				"Draw",
				std::format("vertexCount={} startVertex={}", a_vertexCount, a_startVertexLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.draw) {
				hooks.draw(a_context, a_vertexCount, a_startVertexLocation);
			} else {
				TraceMissingContextOriginal(a_context, "Draw");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedInstancedHook(ID3D11DeviceContext* a_context, UINT a_indexCountPerInstance, UINT a_instanceCount, UINT a_startIndexLocation, INT a_baseVertexLocation, UINT a_startInstanceLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexedInstanced");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexedInstanced",
				std::format(
					"indexCountPerInstance={} instanceCount={} startIndex={} baseVertex={} startInstance={}",
					a_indexCountPerInstance,
					a_instanceCount,
					a_startIndexLocation,
					a_baseVertexLocation,
					a_startInstanceLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexedInstanced) {
				hooks.drawIndexedInstanced(a_context, a_indexCountPerInstance, a_instanceCount, a_startIndexLocation, a_baseVertexLocation, a_startInstanceLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexedInstanced");
			}
		}

		void STDMETHODCALLTYPE DrawInstancedHook(ID3D11DeviceContext* a_context, UINT a_vertexCountPerInstance, UINT a_instanceCount, UINT a_startVertexLocation, UINT a_startInstanceLocation)
		{
			TraceLightLimitFixDrawHookHealth("DrawInstanced");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawInstanced",
				std::format(
					"vertexCountPerInstance={} instanceCount={} startVertex={} startInstance={}",
					a_vertexCountPerInstance,
					a_instanceCount,
					a_startVertexLocation,
					a_startInstanceLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawInstanced) {
				hooks.drawInstanced(a_context, a_vertexCountPerInstance, a_instanceCount, a_startVertexLocation, a_startInstanceLocation);
			} else {
				TraceMissingContextOriginal(a_context, "DrawInstanced");
			}
		}

		void STDMETHODCALLTYPE DrawAutoHook(ID3D11DeviceContext* a_context)
		{
			TraceLightLimitFixDrawHookHealth("DrawAuto");
			TraceLightLimitFixDrawContext(a_context, "DrawAuto", "auto=true");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawAuto) {
				hooks.drawAuto(a_context);
			} else {
				TraceMissingContextOriginal(a_context, "DrawAuto");
			}
		}

		void STDMETHODCALLTYPE DrawIndexedInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
		{
			TraceLightLimitFixDrawHookHealth("DrawIndexedInstancedIndirect");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawIndexedInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexedInstancedIndirect) {
				hooks.drawIndexedInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
			} else {
				TraceMissingContextOriginal(a_context, "DrawIndexedInstancedIndirect");
			}
		}

		void STDMETHODCALLTYPE DrawInstancedIndirectHook(ID3D11DeviceContext* a_context, ID3D11Buffer* a_bufferForArgs, UINT a_alignedByteOffsetForArgs)
		{
			TraceLightLimitFixDrawHookHealth("DrawInstancedIndirect");
			TraceLightLimitFixDrawContext(
				a_context,
				"DrawInstancedIndirect",
				std::format("argsBuffer={} alignedByteOffset={}", static_cast<const void*>(a_bufferForArgs), a_alignedByteOffsetForArgs));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawInstancedIndirect) {
				hooks.drawInstancedIndirect(a_context, a_bufferForArgs, a_alignedByteOffsetForArgs);
			} else {
				TraceMissingContextOriginal(a_context, "DrawInstancedIndirect");
			}
		}

		void STDMETHODCALLTYPE IASetPrimitiveTopologyHook(ID3D11DeviceContext* a_context, D3D11_PRIMITIVE_TOPOLOGY a_topology)
		{
			TraceLightLimitFixContextHookHealth(a_context, "IASetPrimitiveTopology");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.iaSetPrimitiveTopology) {
				hooks.iaSetPrimitiveTopology(a_context, a_topology);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"IASetPrimitiveTopology",
						std::format("topology={}", static_cast<std::uint32_t>(a_topology)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "IASetPrimitiveTopology");
			}
		}

		void STDMETHODCALLTYPE OMSetRenderTargetsHook(ID3D11DeviceContext* a_context, UINT a_renderTargetViewCount, ID3D11RenderTargetView* const* a_renderTargetViews, ID3D11DepthStencilView* a_depthStencilView)
		{
			TraceLightLimitFixContextHookHealth(a_context, "OMSetRenderTargets");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.omSetRenderTargets) {
				hooks.omSetRenderTargets(a_context, a_renderTargetViewCount, a_renderTargetViews, a_depthStencilView);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"OMSetRenderTargets",
						std::format(
							"rtvCount={} dsv=0x{:X}",
							a_renderTargetViewCount,
							ToAddress(a_depthStencilView)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "OMSetRenderTargets");
			}
		}

		void STDMETHODCALLTYPE RSSetViewportsHook(ID3D11DeviceContext* a_context, UINT a_viewportCount, const D3D11_VIEWPORT* a_viewports)
		{
			TraceLightLimitFixContextHookHealth(a_context, "RSSetViewports");
			const bool traceState = HasCachedBoundLightLimitFixPixelShader(a_context);
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.rsSetViewports) {
				hooks.rsSetViewports(a_context, a_viewportCount, a_viewports);
				if (traceState) {
					TraceLightLimitFixStateContext(
						a_context,
						"RSSetViewports",
						std::format(
							"viewportCount={} first={}",
							a_viewportCount,
							FormatViewport(a_viewports ? a_viewports[0] : D3D11_VIEWPORT{}, a_viewports && a_viewportCount > 0 ? 1 : 0)));
				}
			} else {
				TraceMissingContextOriginal(a_context, "RSSetViewports");
			}
		}

		void STDMETHODCALLTYPE CopyResourceHook(ID3D11DeviceContext* a_context, ID3D11Resource* a_destinationResource, ID3D11Resource* a_sourceResource)
		{
			TraceLightLimitFixContextHookHealth(a_context, "CopyResource");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.copyResource) {
				hooks.copyResource(a_context, a_destinationResource, a_sourceResource);
			} else {
				TraceMissingContextOriginal(a_context, "CopyResource");
			}
		}

		void STDMETHODCALLTYPE ExecuteCommandListHook(ID3D11DeviceContext* a_context, ID3D11CommandList* a_commandList, BOOL a_restoreContextState)
		{
			TraceLightLimitFixContextHookHealth(a_context, "ExecuteCommandList");
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.executeCommandList) {
				hooks.executeCommandList(a_context, a_commandList, a_restoreContextState);
			} else {
				TraceMissingContextOriginal(a_context, "ExecuteCommandList");
			}
		}

		template <class Fn, class HookFn>
		Fn InstallLightLimitFixDirectDrawDiagnostic(Fn a_original, HookFn a_hook, const char* a_drawKind, std::size_t& a_detourCount)
		{
			if (!a_original || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
				return a_original;
			}

			const auto originalAddress = ToFunctionAddress(a_original);
			if (!originalAddress) {
				return a_original;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = llfDirectDrawTrampolines.find(originalAddress); it != llfDirectDrawTrampolines.end()) {
					return std::bit_cast<Fn>(it->second);
				}
			}

			const auto hookAddress = ToFunctionAddress(a_hook);
			const auto trampoline = Detours::X64::DetourFunction(originalAddress, hookAddress);
			if (!trampoline) {
				logger::warn(
					"[LightLimitFix] PreNG direct draw diagnostic skipped draw={} original=0x{:X}; detour failed",
					a_drawKind,
					originalAddress);
				return a_original;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto [it, inserted] = llfDirectDrawTrampolines.emplace(originalAddress, trampoline); !inserted) {
					return std::bit_cast<Fn>(it->second);
				}
			}

			++a_detourCount;
			logger::info(
				"[LightLimitFix] PreNG direct draw diagnostic detoured draw={} original=0x{:X} trampoline=0x{:X}",
				a_drawKind,
				originalAddress,
				trampoline);
			return std::bit_cast<Fn>(trampoline);
		}

		void InstallLightLimitFixDrawContextDiagnostics(ID3D11DeviceContext* a_context, const char* a_source, const void* a_rendererData, const void* a_rendererDevice)
		{
			if (!ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
				return;
			}

			if (!a_context) {
				return;
			}

			const auto vtable = GetContextVTablePointer(a_context);
			if (!vtable) {
				logger::warn(
					"[LightLimitFix] PreNG draw-time diagnostics skipped source={} context=0x{:X}; missing vtable",
					a_source,
					ToAddress(a_context));
				return;
			}

			bool knownVTable = false;
			{
				std::scoped_lock lock(llfCandidateLock);
				knownVTable = drawContextHooksByVTable.find(vtable) != drawContextHooksByVTable.end();
			}

			if (knownVTable) {
				TraceLightLimitFixContextDiagnostics(a_source, "known-vtable", a_context, a_rendererData, a_rendererDevice);
				return;
			}

			TraceLightLimitFixContextDiagnostics(a_source, "prehook", a_context, a_rendererData, a_rendererDevice);

			DrawContextHooks hooks;
			hooks.psSetShaderResources = std::bit_cast<PSSetShaderResourcesFn>(Detours::X64::DetourClassVTable(vtable, &PSSetShaderResourcesHook, 8));
			hooks.psSetShader = std::bit_cast<PSSetShaderFn>(Detours::X64::DetourClassVTable(vtable, &PSSetShaderHook, 9));
			hooks.drawIndexed = std::bit_cast<DrawIndexedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedHook, 12));
			hooks.draw = std::bit_cast<DrawFn>(Detours::X64::DetourClassVTable(vtable, &DrawHook, 13));
			hooks.drawIndexedInstanced = std::bit_cast<DrawIndexedInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedHook, 20));
			hooks.drawInstanced = std::bit_cast<DrawInstancedFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedHook, 21));
			hooks.iaSetPrimitiveTopology = std::bit_cast<IASetPrimitiveTopologyFn>(Detours::X64::DetourClassVTable(vtable, &IASetPrimitiveTopologyHook, 24));
			hooks.omSetRenderTargets = std::bit_cast<OMSetRenderTargetsFn>(Detours::X64::DetourClassVTable(vtable, &OMSetRenderTargetsHook, 33));
			hooks.drawAuto = std::bit_cast<DrawAutoFn>(Detours::X64::DetourClassVTable(vtable, &DrawAutoHook, 38));
			hooks.drawIndexedInstancedIndirect = std::bit_cast<DrawIndexedInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawIndexedInstancedIndirectHook, 39));
			hooks.drawInstancedIndirect = std::bit_cast<DrawInstancedIndirectFn>(Detours::X64::DetourClassVTable(vtable, &DrawInstancedIndirectHook, 40));
			hooks.rsSetViewports = std::bit_cast<RSSetViewportsFn>(Detours::X64::DetourClassVTable(vtable, &RSSetViewportsHook, 44));
			hooks.copyResource = std::bit_cast<CopyResourceFn>(Detours::X64::DetourClassVTable(vtable, &CopyResourceHook, 47));
			hooks.executeCommandList = std::bit_cast<ExecuteCommandListFn>(Detours::X64::DetourClassVTable(vtable, &ExecuteCommandListHook, 58));

			std::size_t directDrawDetourCount = 0;
			hooks.drawIndexed = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexed, &DrawIndexedHook, "DrawIndexed", directDrawDetourCount);
			hooks.draw = InstallLightLimitFixDirectDrawDiagnostic(hooks.draw, &DrawHook, "Draw", directDrawDetourCount);
			hooks.drawIndexedInstanced = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexedInstanced, &DrawIndexedInstancedHook, "DrawIndexedInstanced", directDrawDetourCount);
			hooks.drawInstanced = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawInstanced, &DrawInstancedHook, "DrawInstanced", directDrawDetourCount);
			hooks.drawAuto = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawAuto, &DrawAutoHook, "DrawAuto", directDrawDetourCount);
			hooks.drawIndexedInstancedIndirect = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawIndexedInstancedIndirect, &DrawIndexedInstancedIndirectHook, "DrawIndexedInstancedIndirect", directDrawDetourCount);
			hooks.drawInstancedIndirect = InstallLightLimitFixDirectDrawDiagnostic(hooks.drawInstancedIndirect, &DrawInstancedIndirectHook, "DrawInstancedIndirect", directDrawDetourCount);

			std::size_t hookVTableCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (!installedContextHooks) {
					fallbackDrawContextHooks = hooks;
				}
				drawContextHooksByVTable[vtable] = hooks;
				installedContextHooks = true;
				hookVTableCount = drawContextHooksByVTable.size();
			}

			TraceLightLimitFixContextDiagnostics(a_source, "posthook", a_context, a_rendererData, a_rendererDevice);
			logger::info(
				"[LightLimitFix] PreNG draw-time candidate diagnostics installed source={} context=0x{:X} vtable=0x{:X} hookVTables={} directDrawDetours={} callThroughs=PSSetShaderResources=0x{:X},PSSetShader=0x{:X},DrawIndexed=0x{:X},Draw=0x{:X},DrawIndexedInstanced=0x{:X},DrawInstanced=0x{:X},IASetPrimitiveTopology=0x{:X},OMSetRenderTargets=0x{:X},DrawAuto=0x{:X},DrawIndexedInstancedIndirect=0x{:X},DrawInstancedIndirect=0x{:X},RSSetViewports=0x{:X},CopyResource=0x{:X},ExecuteCommandList=0x{:X}",
				a_source,
				ToAddress(a_context),
				vtable,
				hookVTableCount,
				directDrawDetourCount,
				ToFunctionAddress(hooks.psSetShaderResources),
				ToFunctionAddress(hooks.psSetShader),
				ToFunctionAddress(hooks.drawIndexed),
				ToFunctionAddress(hooks.draw),
				ToFunctionAddress(hooks.drawIndexedInstanced),
				ToFunctionAddress(hooks.drawInstanced),
				ToFunctionAddress(hooks.iaSetPrimitiveTopology),
				ToFunctionAddress(hooks.omSetRenderTargets),
				ToFunctionAddress(hooks.drawAuto),
				ToFunctionAddress(hooks.drawIndexedInstancedIndirect),
				ToFunctionAddress(hooks.drawInstancedIndirect),
				ToFunctionAddress(hooks.rsSetViewports),
				ToFunctionAddress(hooks.copyResource),
				ToFunctionAddress(hooks.executeCommandList));
		}

		void ProbeLightLimitFixRendererContext()
		{
			auto* rendererData = fo4cs::GetRendererData();
			if (!rendererData) {
				if (!rendererContextUnavailableLogged) {
					logger::warn("[LightLimitFix] PreNG rendererData unavailable during draw diagnostics probe");
					rendererContextUnavailableLogged = true;
				}
				return;
			}

			auto* rendererContext = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
			auto* rendererDevice = reinterpret_cast<ID3D11Device*>(rendererData->device);
			if (!rendererDevice) {
				rendererDevice = observedD3D11Device;
			}

			if (!rendererContext) {
				if (!rendererContextUnavailableLogged) {
					logger::warn(
						"[LightLimitFix] PreNG rendererData context unavailable during draw diagnostics probe rendererData=0x{:X} rendererDevice=0x{:X}",
						ToAddress(rendererData),
						ToAddress(rendererDevice));
					rendererContextUnavailableLogged = true;
				}
				return;
			}

			const bool changed = rendererContext != observedRendererContext;
			const auto vtable = GetContextVTablePointer(rendererContext);
			bool knownVTable = false;
			{
				std::scoped_lock lock(llfCandidateLock);
				knownVTable = vtable && drawContextHooksByVTable.find(vtable) != drawContextHooksByVTable.end();
			}

			if (!changed && knownVTable) {
				return;
			}

			observedRendererContext = rendererContext;
			TraceLightLimitFixContextDiagnostics("rendererData", "observed", rendererContext, rendererData, rendererDevice);
			InstallLightLimitFixDrawContextDiagnostics(rendererContext, "rendererData", rendererData, rendererDevice);
		}
#endif

		HRESULT STDMETHODCALLTYPE CreateVertexShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11VertexShader** a_vertexShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Vertex, a_bytecode, a_bytecodeLength);
			return createVertexShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_vertexShader);
		}

		HRESULT STDMETHODCALLTYPE CreatePixelShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11PixelShader** a_pixelShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Pixel, a_bytecode, a_bytecodeLength);
#if defined(FALLOUT_PRE_NG)
			const auto traceMetadata = GetTracePixelShaderMetadata(a_bytecode, a_bytecodeLength);
#endif
			const auto result = createPixelShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_pixelShader);
#if defined(FALLOUT_PRE_NG)
			if (SUCCEEDED(result) && a_pixelShader && *a_pixelShader && traceMetadata) {
				TrackObservedPixelShader(*a_pixelShader, *traceMetadata);
				if (IsLightLimitFixPixelTrackedCandidate(*traceMetadata)) {
					TrackLightLimitFixPixelShader(*a_pixelShader, *traceMetadata);
					TraceLightLimitFixPixelCandidate(a_device, *a_pixelShader, *traceMetadata);
				}
			}
#endif
			return result;
		}

		HRESULT STDMETHODCALLTYPE CreateComputeShaderHook(ID3D11Device* a_device, const void* a_bytecode, SIZE_T a_bytecodeLength, ID3D11ClassLinkage* a_classLinkage, ID3D11ComputeShader** a_computeShader)
		{
			ShaderCache::GetSingleton()->ObserveShader(ShaderStage::Compute, a_bytecode, a_bytecodeLength);
			return createComputeShader(a_device, a_bytecode, a_bytecodeLength, a_classLinkage, a_computeShader);
		}

#if defined(FALLOUT_PRE_NG)
		HRESULT STDMETHODCALLTYPE CreateDeferredContextHook(ID3D11Device* a_device, UINT a_contextFlags, ID3D11DeviceContext** a_deferredContext)
		{
			const auto result = createDeferredContext(a_device, a_contextFlags, a_deferredContext);
			if (SUCCEEDED(result) && a_deferredContext && *a_deferredContext && ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
				logger::info(
					"[LightLimitFix] PreNG deferred context created flags={} context=0x{:X}",
					a_contextFlags,
					ToAddress(*a_deferredContext));
				TraceLightLimitFixContextDiagnostics("deferred", "observed", *a_deferredContext, nullptr, a_device);
				InstallLightLimitFixDrawContextDiagnostics(*a_deferredContext, "deferred", nullptr, a_device);
			}
			return result;
		}
#endif
	}

	void Install()
	{
		logger::info("[CommunityShaders] D3D11 observation hooks armed");
	}

	void OnD3D11DeviceCreated(ID3D11Device* a_device)
	{
		if (!a_device || installedDeviceHooks) {
			return;
		}

		*(uintptr_t*)&createVertexShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateVertexShaderHook, 12);
		*(uintptr_t*)&createPixelShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreatePixelShaderHook, 15);
		*(uintptr_t*)&createComputeShader = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateComputeShaderHook, 18);
#if defined(FALLOUT_PRE_NG)
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			*(uintptr_t*)&createDeferredContext = Detours::X64::DetourClassVTable(*(uintptr_t*)a_device, &CreateDeferredContextHook, 27);
		}
#endif
		installedDeviceHooks = true;

		logger::info("[CommunityShaders] D3D11 shader observation hooks installed");

#if defined(FALLOUT_PRE_NG)
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			observedD3D11Device = a_device;
			winrt::com_ptr<ID3D11DeviceContext> context;
			a_device->GetImmediateContext(context.put());
			if (context) {
				observedImmediateContext = context.get();
				TraceLightLimitFixContextDiagnostics("immediate", "observed", context.get(), nullptr, a_device);
				InstallLightLimitFixDrawContextDiagnostics(context.get(), "immediate", nullptr, a_device);
			} else {
				logger::warn("[LightLimitFix] PreNG immediate context unavailable during draw diagnostics install");
			}
		} else {
			logger::info("[LightLimitFix] PreNG support-only PS candidate diagnostics held; set FO4CS_TRACE_LLF_PS=1 to enable shader-path evidence gathering");
		}
#endif
	}

	void OnFrame()
	{
#if defined(FALLOUT_PRE_NG)
		if (ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
			if (!llfOnFrameLogged) {
				llfOnFrameLogged = true;
				logger::info("[LightLimitFix] PreNG Hooks::OnFrame reached; probing renderer context");
			}
			ProbeLightLimitFixRendererContext();
		}
#endif
	}
}
