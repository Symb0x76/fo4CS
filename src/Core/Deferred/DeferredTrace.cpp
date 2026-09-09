#include "Core/Deferred/DeferredInternal.h"

#include <d3d11.h>
#include <d3d11_1.h>

#include "Core/CommunityShaders.h"
#include "Core/DebugSwitches.h"
#include "Core/State.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace CommunityShaders::deferred
{
	namespace
	{
		std::mutex deferredTraceLock;
	}

	bool IsDeferredTraceEnabled() noexcept
	{
		static const bool enabled = CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_TRACE_DEFERRED");
		return enabled;
	}

	bool IsGBufferDumpEnabled() noexcept
	{
		static const bool enabled = CommunityShaders::DebugSwitches::ReadSwitchEnabled("FO4CS_DUMP_GBUFFER");
		return enabled;
	}

	void TraceDeferredLine(std::string_view a_line)
	{
		try {
			auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
			std::error_code ec;
			std::filesystem::create_directories(traceDir, ec);
			if (ec) {
				return;
			}
			auto* runtime = CommunityShaders::Runtime::GetSingleton();
			const auto frame = runtime ? runtime->GetFrameCount() : 0;
			std::scoped_lock lock(deferredTraceLock);
			std::ofstream out(traceDir / "deferred_trace.txt", std::ios::app);
			if (out) {
				out << std::format("[frame={}] {}\n", frame, a_line);
			}
		} catch (...) {
			// Diagnostic output must never terminate a render hook.
		}
	}

	std::string DescribeRTV(ID3D11RenderTargetView* a_rtv)
	{
		if (!a_rtv) {
			return "null";
		}
		D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
		a_rtv->GetDesc(&viewDesc);
		ID3D11Resource* resource = nullptr;
		a_rtv->GetResource(&resource);
		const auto resourceAddress = reinterpret_cast<std::uintptr_t>(resource);
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT textureFormat = DXGI_FORMAT_UNKNOWN;
		if (resource) {
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) && texture) {
				D3D11_TEXTURE2D_DESC textureDesc{};
				texture->GetDesc(&textureDesc);
				width = textureDesc.Width;
				height = textureDesc.Height;
				textureFormat = textureDesc.Format;
				texture->Release();
			}
			resource->Release();
		}
		try {
			return std::format("view=0x{:X},resource=0x{:X},viewFmt={},texFmt={},size={}x{}",
				reinterpret_cast<std::uintptr_t>(a_rtv), resourceAddress,
				static_cast<std::uint32_t>(viewDesc.Format), static_cast<std::uint32_t>(textureFormat), width, height);
		} catch (...) {
			return "<rtv-format-error>";
		}
	}

	std::string DescribeDSV(ID3D11DepthStencilView* a_dsv)
	{
		if (!a_dsv) {
			return "null";
		}
		D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
		a_dsv->GetDesc(&viewDesc);
		try {
			return std::format("view=0x{:X},fmt={},dimension={}", reinterpret_cast<std::uintptr_t>(a_dsv),
				static_cast<std::uint32_t>(viewDesc.Format), static_cast<std::uint32_t>(viewDesc.ViewDimension));
		} catch (...) {
			return "<dsv-format-error>";
		}
	}

	void TraceOMState(ID3D11DeviceContext* a_context, std::string_view a_phase)
	{
		if (!IsDeferredTraceEnabled() || !a_context) {
			return;
		}
		std::array<ID3D11RenderTargetView*, Deferred::kMaxBoundRenderTargetCount> rtvs{};
		ID3D11DepthStencilView* dsv = nullptr;
		a_context->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
		ID3D11BlendState* blend = nullptr;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		a_context->OMGetBlendState(&blend, blendFactor.data(), &sampleMask);
		TraceDeferred("om phase={} dsv={} blend=0x{:X} sampleMask=0x{:X} blendFactor=[{:.3f},{:.3f},{:.3f},{:.3f}]",
			a_phase, DescribeDSV(dsv), reinterpret_cast<std::uintptr_t>(blend), sampleMask,
			blendFactor[0], blendFactor[1], blendFactor[2], blendFactor[3]);
		for (std::size_t i = 0; i < rtvs.size(); ++i) {
			TraceDeferred("om phase={} rtv[{}] {}", a_phase, i, DescribeRTV(rtvs[i]));
		}
		for (auto* rtv : rtvs) {
			if (rtv) {
				rtv->Release();
			}
		}
		if (dsv) {
			dsv->Release();
		}
		if (blend) {
			blend->Release();
		}
	}

	void TraceRestoreCheck(ID3D11DeviceContext* a_context, const LightingDrawState& a_state)
	{
		if (!IsDeferredTraceEnabled() || !a_context) {
			return;
		}
		std::array<ID3D11RenderTargetView*, Deferred::kMaxBoundRenderTargetCount> rtvs{};
		ID3D11DepthStencilView* dsv = nullptr;
		a_context->OMGetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), &dsv);
		ID3D11BlendState* blend = nullptr;
		std::array<FLOAT, 4> blendFactor{};
		UINT sampleMask = D3D11_DEFAULT_SAMPLE_MASK;
		a_context->OMGetBlendState(&blend, blendFactor.data(), &sampleMask);
		bool match = dsv == a_state.depthStencil.get() && blend == a_state.blendState.get() && sampleMask == a_state.sampleMask;
		for (std::size_t i = 0; i < rtvs.size(); ++i) {
			match = match && rtvs[i] == a_state.renderTargets[i].get();
		}
		for (std::size_t i = 0; i < blendFactor.size(); ++i) {
			match = match && blendFactor[i] == a_state.blendFactor[i];
		}
		TraceDeferred("restore check match={} expectedRTCount={} actualDSV=0x{:X} actualBlend=0x{:X}", match,
			a_state.renderTargetCount, reinterpret_cast<std::uintptr_t>(dsv), reinterpret_cast<std::uintptr_t>(blend));
		for (auto* rtv : rtvs) {
			if (rtv) {
				rtv->Release();
			}
		}
		if (dsv) {
			dsv->Release();
		}
		if (blend) {
			blend->Release();
		}
	}

	void DumpGBufferSnapshot()
	{
		static std::atomic_bool dumped = false;
		if (!IsGBufferDumpEnabled() || !Deferred::GetSingleton()->AreGBufferResourcesReady() || dumped.exchange(true)) {
			return;
		}
		auto* rendererData = fo4cs::GetRendererData();
		if (!rendererData || !rendererData->device || !rendererData->context) {
			TraceDeferred("gbuffer dump skipped missing renderer device/context");
			return;
		}
		auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
		auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);
		if (ec) {
			TraceDeferred("gbuffer dump skipped create_directories error={}", ec.message());
			return;
		}
		auto* runtime = CommunityShaders::Runtime::GetSingleton();
		const auto frame = runtime ? runtime->GetFrameCount() : 0;
		// Via the public accessor rather than the kGBufferTargets table itself: the
		// table stays an anonymous-namespace object in Deferred.cpp, so this split
		// does not give it external linkage or a static-init-order dependency.
		for (const auto& binding : Deferred::GetGBufferTargetBindings()) {
			auto* texture = Deferred::GetSingleton()->GetGBufferTexture(binding.target);
			if (!texture) {
				TraceDeferred("gbuffer dump target={} skipped missing texture", binding.name);
				continue;
			}
			D3D11_TEXTURE2D_DESC sourceDesc{};
			texture->GetDesc(&sourceDesc);
			if (sourceDesc.SampleDesc.Count != 1) {
				TraceDeferred("gbuffer dump target={} skipped multisample count={}", binding.name, sourceDesc.SampleDesc.Count);
				continue;
			}
			D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
			stagingDesc.Usage = D3D11_USAGE_STAGING;
			stagingDesc.BindFlags = 0;
			stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			stagingDesc.MiscFlags = 0;
			winrt::com_ptr<ID3D11Texture2D> staging;
			const auto createResult = device->CreateTexture2D(&stagingDesc, nullptr, staging.put());
			if (FAILED(createResult)) {
				TraceDeferred("gbuffer dump target={} CreateTexture2D failed hr=0x{:08X}", binding.name, static_cast<std::uint32_t>(createResult));
				continue;
			}
			context->CopyResource(staging.get(), texture);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto mapResult = context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
			if (FAILED(mapResult)) {
				TraceDeferred("gbuffer dump target={} Map failed hr=0x{:08X}", binding.name, static_cast<std::uint32_t>(mapResult));
				continue;
			}
			const auto stem = std::format("gbuffer_{}_frame_{}", binding.name, frame);
			std::ofstream out(traceDir / (stem + ".bin"), std::ios::binary);
			if (out) {
				for (UINT row = 0; row < sourceDesc.Height; ++row) {
					out.write(static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, mapped.RowPitch);
				}
			}
			context->Unmap(staging.get(), 0);
			std::ofstream metadata(traceDir / (stem + ".txt"));
			if (metadata) {
				metadata << std::format("target={} rendererIndex={} format={} width={} height={} rowPitch={} frame={}\n",
					binding.name, binding.rendererTargetIndex, static_cast<std::uint32_t>(sourceDesc.Format), sourceDesc.Width,
					sourceDesc.Height, mapped.RowPitch, frame);
			}
			TraceDeferred("gbuffer dump target={} path={} format={} size={}x{} rowPitch={}", binding.name,
				(traceDir / (stem + ".bin")).string(), static_cast<std::uint32_t>(sourceDesc.Format), sourceDesc.Width, sourceDesc.Height, mapped.RowPitch);
		}
	}
}
