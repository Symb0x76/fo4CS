#include "Core/Hooks.h"

#include "Core/DebugSwitches.h"
#include "Core/DiagnosticsFormatter.h"
#include "Core/Globals.h"
#include "Core/HooksInternal.h"
#include "Core/PreNGEnvironment.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"
#include "Features/LightLimitFix.h"

#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
	namespace
	{

#if defined(FALLOUT_PRE_NG)
		using PSSetShaderResourcesFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
		using PSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
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
		ID3D11DeviceContext* observedRendererContext = nullptr;
		bool installedContextHooks = false;
		bool rendererContextUnavailableLogged = false;
		bool llfOnFrameLogged = false;
		std::unordered_set<std::string> loggedLLFMissingOriginals;
		std::unordered_map<std::uintptr_t, std::uintptr_t> llfDirectDrawTrampolines;
		template <class T>
		std::uintptr_t ToFunctionAddress(T a_function)
		{
			static_assert(sizeof(T) == sizeof(std::uintptr_t));
			return std::bit_cast<std::uintptr_t>(a_function);
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
				TrackPreNGDFLightDrawStateBoundPixelShader(a_context, a_pixelShader);
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
			TracePreNGDFLightDrawStateContext(
				a_context,
				"DrawIndexed",
				std::format("indexCount={} startIndex={} baseVertex={}", a_indexCount, a_startIndexLocation, a_baseVertexLocation));
			const auto hooks = GetDrawContextHooksForContext(a_context);
			if (hooks.drawIndexed) {
				hooks.drawIndexed(a_context, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightLLFAdditivePass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightFullContractNoOpPass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightResourceNoOpPass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
				RunPreNGDFLightZeroAdditivePass(a_context, hooks.drawIndexed, a_indexCount, a_startIndexLocation, a_baseVertexLocation);
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
			TracePreNGDFLightDrawStateContext(
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
			TracePreNGDFLightDrawStateContext(
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
			TracePreNGDFLightDrawStateContext(
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
			TracePreNGDFLightDrawStateContext(a_context, "DrawAuto", "auto=true");
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
			TracePreNGDFLightDrawStateContext(
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
			TracePreNGDFLightDrawStateContext(
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
			if (!a_original || !ShouldEnableLightLimitFixPixelCandidateDiagnostics()) {
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

	}

#if defined(FALLOUT_PRE_NG)
	// D3D11DeviceHooks domain (Promotion Step 1): definitions promoted out of the
	// anonymous namespace (declared in Core/HooksInternal.h).
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
#endif

	void Install()
	{
		logger::info("[CommunityShaders] D3D11 observation hooks armed");
	}

	void OnFrame()
	{
#if defined(FALLOUT_PRE_NG)
		AdvancePreNGDFLightLLFAdditivePassFrame();
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