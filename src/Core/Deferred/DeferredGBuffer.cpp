#include "Core/Deferred/DeferredInternal.h"

#include <d3d11.h>
#include <d3d11_1.h>

#include <RE/FO4Runtime.h>

#include <optional>
#include <vector>

// The GBuffer half of Deferred: the binding tables, the accessors over them, the
// one-time resource setup and the MRT blend-state cache.
//
// kGBufferTargets and kDeferredRenderTargets keep internal linkage here. Every
// reader outside this translation unit goes through the public static accessors
// Deferred::GetGBufferTargetBindings() / GetDeferredRenderTargetBindings(), which
// return these same objects by const reference, so the split introduces no
// external-linkage namespace-scope objects and therefore no
// static-initialization-order hazard.

// SetupResources traces the render-target bindings it establishes; the same
// directive as Deferred.cpp keeps those call sites spelled as they were.
using namespace CommunityShaders::deferred;

// FO4 GBuffer layout (Creation Engine shared architecture):
//   RT20 = kGbufferNormal     — Normal + roughness
//   RT22 = kGbufferAlbedo     — Albedo (diffuse)
//   RT23 = kGbufferEmissive   — Emissive
//   RT24 = kGbufferMaterial   — Glossiness, Specular, SSS, Backlighting
//
// These slots replicate the Skyrim CS deferred pattern:
//   ALBEDO     → FO4 RT22
//   SPECULAR   → FO4 RT24 (specular channel)
//   MASKS      → FO4 RT20 (normal channel doubles as mask carrier)

namespace
{
	const Deferred::GBufferTargetBindings kGBufferTargets{ {
		{ Deferred::GBufferTarget::kNormal, RE::FO4Runtime::RenderTargetIndex::kGBufferNormal, DXGI_FORMAT_R10G10B10A2_UNORM, "NormalRoughness" },
		{ Deferred::GBufferTarget::kAlbedo, RE::FO4Runtime::RenderTargetIndex::kGBufferAlbedo, DXGI_FORMAT_R10G10B10A2_UNORM, "Albedo" },
		{ Deferred::GBufferTarget::kEmissive, RE::FO4Runtime::RenderTargetIndex::kGBufferEmissive, DXGI_FORMAT_R11G11B10_FLOAT, "Emissive" },
		{ Deferred::GBufferTarget::kMaterial, RE::FO4Runtime::RenderTargetIndex::kGBufferMaterial, DXGI_FORMAT_R11G11B10_FLOAT, "Material" },
	} };

	const Deferred::DeferredRenderTargetBindings kDeferredRenderTargets{ {
		{ 2, Deferred::GBufferTarget::kNormal, "NormalRoughness" },
		{ 3, Deferred::GBufferTarget::kAlbedo, "Albedo" },
		{ 4, Deferred::GBufferTarget::kEmissive, "Emissive" },
		{ 5, Deferred::GBufferTarget::kMaterial, "Material" },
	} };

	[[nodiscard]] std::size_t ToGBufferIndex(Deferred::GBufferTarget a_target) noexcept
	{
		return static_cast<std::size_t>(a_target);
	}

	[[nodiscard]] const Deferred::GBufferTargetBinding* GetGBufferBinding(Deferred::GBufferTarget a_target) noexcept
	{
		const auto index = ToGBufferIndex(a_target);
		return index < kGBufferTargets.size() ? &kGBufferTargets[index] : nullptr;
	}
}

const Deferred::GBufferTargetBindings& Deferred::GetGBufferTargetBindings() noexcept
{
	return kGBufferTargets;
}

const Deferred::DeferredRenderTargetBindings& Deferred::GetDeferredRenderTargetBindings() noexcept
{
	return kDeferredRenderTargets;
}

D3D11_TEXTURE2D_DESC Deferred::GetGBufferDesc(GBufferTarget a_target) const noexcept
{
	const auto index = ToGBufferIndex(a_target);
	return index < gBufferDescriptions.size() ? gBufferDescriptions[index] : D3D11_TEXTURE2D_DESC{};
}

ID3D11Texture2D* Deferred::GetGBufferTexture(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11Texture2D*>(rendererData->renderTargets[binding->rendererTargetIndex].texture);
}

ID3D11ShaderResourceView* Deferred::GetGBufferSRV(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11ShaderResourceView*>(rendererData->renderTargets[binding->rendererTargetIndex].srView);
}

ID3D11RenderTargetView* Deferred::GetGBufferRTV(GBufferTarget a_target) const noexcept
{
	const auto* binding = GetGBufferBinding(a_target);
	auto* rendererData = fo4cs::GetRendererData();
	if (!binding || !rendererData) {
		return nullptr;
	}

	return reinterpret_cast<ID3D11RenderTargetView*>(rendererData->renderTargets[binding->rendererTargetIndex].rtView);
}

void Deferred::SetupResources()
{
	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData || !rendererData->device) {
		gBufferResourcesReady = false;
		return;
	}

	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	InstallDrawHooks(reinterpret_cast<ID3D11DeviceContext*>(rendererData->context));
	auto createSampler = [&](D3D11_FILTER a_filter, const char* a_name, winrt::com_ptr<ID3D11SamplerState>& a_sampler) {
		if (a_sampler) {
			return true;
		}

		D3D11_SAMPLER_DESC desc{};
		desc.Filter = a_filter;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.MaxAnisotropy = 1;
		desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		desc.MinLOD = 0.0f;
		desc.MaxLOD = D3D11_FLOAT32_MAX;

		const auto hr = device->CreateSamplerState(&desc, a_sampler.put());
		if (FAILED(hr)) {
			logger::warn("[Deferred] CreateSamplerState({}) failed hr=0x{:08X}", a_name, static_cast<std::uint32_t>(hr));
			return false;
		}

		return true;
	};

	bool ready = createSampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, "linear", linearSampler) &&
	             createSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, "point", pointSampler);

	for (const auto& binding : kGBufferTargets) {
		auto* texture = GetGBufferTexture(binding.target);
		auto* srv = GetGBufferSRV(binding.target);
		auto* rtv = GetGBufferRTV(binding.target);
		if (!texture || !srv || !rtv) {
			ready = false;
			continue;
		}

			texture->GetDesc(&gBufferDescriptions[ToGBufferIndex(binding.target)]);
			if (IsDeferredTraceEnabled()) {
				const auto& desc = gBufferDescriptions[ToGBufferIndex(binding.target)];
				TraceDeferred("gbuffer target={} rendererIndex={} expectedFmt={} actualFmt={} size={}x{} texture=0x{:X} srv=0x{:X} rtv={}",
					binding.name, binding.rendererTargetIndex, static_cast<std::uint32_t>(binding.expectedFormat),
					static_cast<std::uint32_t>(desc.Format), desc.Width, desc.Height,
					reinterpret_cast<std::uintptr_t>(texture), reinterpret_cast<std::uintptr_t>(srv), DescribeRTV(rtv));
			}
		}

	const bool wasReady = gBufferResourcesReady;
	gBufferResourcesReady = ready;

	static bool loggedPending = false;
	if (gBufferResourcesReady) {
		loggedPending = false;
		if (!wasReady) {
			const auto normalDesc = GetGBufferDesc(GBufferTarget::kNormal);
			logger::info(
				"[Deferred] GBuffer resources ready targets={} size={}x{} linearSampler={} pointSampler={}",
				kGBufferTargets.size(),
				normalDesc.Width,
				normalDesc.Height,
				linearSampler != nullptr,
				pointSampler != nullptr);
		}
	} else if (!loggedPending) {
		logger::warn("[Deferred] GBuffer resources pending; deferred consumers remain disabled until renderer targets are available");
		loggedPending = true;
	}
}

ID3D11BlendState* Deferred::GetOrCreateMRTBlendState(ID3D11BlendState* a_original)
{
	if (!a_original) return nullptr;

	std::scoped_lock lock(blendStateLock);

	auto it = blendStateCache.find(a_original);
	if (it != blendStateCache.end())
		return it->second ? it->second.get() : a_original;

	D3D11_BLEND_DESC desc;
	a_original->GetDesc(&desc);

	if (desc.IndependentBlendEnable) {
		blendStateCache[a_original].attach(nullptr);  // mark as already MRT
		return a_original;
	}

	// Extend: copy RT[0] blend settings to RTs [1..7]
	desc.IndependentBlendEnable = TRUE;
	for (int i = 1; i < 8; i++) {
		desc.RenderTarget[i] = desc.RenderTarget[0];
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData || !rendererData->device) return a_original;

	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!device) return a_original;

	winrt::com_ptr<ID3D11BlendState> extended;
	if (FAILED(device->CreateBlendState(&desc, extended.put()))) {
		logger::warn("[Deferred] Failed to create MRT-extended blend state");
		return a_original;
	}

	blendStateCache[a_original] = extended;
	logger::info("[Deferred] Created MRT blend state for 0x{:X} → 0x{:X}",
	             reinterpret_cast<std::uintptr_t>(a_original),
	             reinterpret_cast<std::uintptr_t>(extended.get()));
	return extended.get();
}
