#include "Core/Deferred.h"

#include <d3d11.h>

#include "Core/CommunityShaders.h"
#include "Core/Feature.h"
#include "Core/ShaderCache.h"
#include "Core/State.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <Windows.h>

namespace
{
	// Check if the F4SE Address Library version file exists.
	// Without it, REL::ID resolution crashes via REX::FAIL -> std::terminate.
	// MO2 USVFS does not hook GetModuleFileNameW or directory_iterator,
	// so we first search via Win32 FindFirstFile (USVFS-compatible) in Data/F4SE/Plugins.
	[[nodiscard]] bool HasAddressLibrary()
	{
		// Method 1: Win32 API search in game Data dir (MO2 USVFS compatible)
		wchar_t exePath[MAX_PATH];
		if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
			auto searchPath = std::filesystem::path(exePath).parent_path() / "Data" / "F4SE" / "Plugins" / "version-*.bin";
			WIN32_FIND_DATAW findData;
			HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
			if (hFind != INVALID_HANDLE_VALUE) {
				FindClose(hFind);
				return true;
			}
		}

		// Method 2: fallback - check DLL directory (non-MO2 installs)
		wchar_t modulePath[MAX_PATH];
		if (GetModuleFileNameW(GetModuleHandleW(L"NuclearGFX.dll"), modulePath, MAX_PATH)) {
			std::filesystem::path pluginDir = std::filesystem::path(modulePath).parent_path();
			for (auto& entry : std::filesystem::directory_iterator(pluginDir)) {
				auto name = entry.path().filename().string();
				if (name.starts_with("version-") && name.ends_with(".bin"))
					return true;
			}
		}
		return false;
	}
}

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
	constexpr auto kGBufferAlbedo   = 22;
	constexpr auto kGBufferNormal   = 20;
	constexpr auto kGBufferEmissive = 23;
	constexpr auto kGBufferMaterial = 24;
}

void Deferred::SetupResources()
{
	// GBuffer render target creation will go here.
	// The existing Upscaling code already handles render target resizing —
	// Deferred adds extra GBuffer slots beyond the engine's forward-only layout.
	//
	// Typical setup (matches Skyrim CS pattern):
	//   1. Clone main RT descriptor
	//   2. Create Albedo RT  (R10G10B10A2_UNORM)
	//   3. Create Normal RT  (R10G10B10A2_UNORM)
	//   4. Create Emissive RT (R11G11B10_FLOAT or R10G10B10A2_UNORM)
	//   5. Create Material RT (R11G11B10_FLOAT or R16G16B16A16_FLOAT)
	//   6. Create samplers (linear + point, clamp addressing)
	//   7. Create composite blend state + depth stencil state + rasterizer
	logger::info("[Deferred] GBuffer setup deferred (render targets not yet allocated)");
}

void Deferred::ReflectionsPrepasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("ReflectionsPrepass", [](Feature* a_feature) {
		try { a_feature->ReflectionsPrepass(); } catch (...) {}
	});
}

void Deferred::EarlyPrepasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("EarlyPrepass", [](Feature* a_feature) {
		try { a_feature->EarlyPrepass(); } catch (...) {}
	});
}

void Deferred::PrepassPasses()
{
	auto* rd = fo4cs::GetRendererData();
	if (!rd || !rd->context) return;
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rd->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	Feature::ForEachLoadedFeature("Prepass", [](Feature* a_feature) {
		try {
			a_feature->Prepass();
		} catch (const std::exception& e) {
			logger::error("[Deferred] Feature '{}' Prepass exception: {}", a_feature->GetName(), e.what());
		} catch (...) {
			logger::error("[Deferred] Feature '{}' Prepass unknown exception", a_feature->GetName());
		}
	});
}

void Deferred::StartDeferred()
{
	deferredPass = true;
	try {
		PrepassPasses();
	} catch (const std::exception& e) {
		logger::error("[Deferred] StartDeferred exception: {}", e.what());
	} catch (...) {
		logger::error("[Deferred] StartDeferred unknown exception");
	}
}

void Deferred::EndDeferred()
{
	DeferredPasses();
	ResetBlendStates();
	deferredPass = false;

	auto rendererData = fo4cs::GetRendererData();
	auto context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	context->OMSetRenderTargets(0, nullptr, nullptr);
}

void Deferred::DeferredPasses()
{
	// Deferred composite pass — dispatches to Features.
	// Full implementation will:
	//   1. Copy main + normal-roughness to backup RTs
	//   2. Bind GBuffer SRVs (t0-t5: main, spec, normal, depth, albedo, masks)
	//   3. Bind lighting/reflection SRVs from loaded Features
	//   4. Composite PS with interior/exterior variant
	//   5. Draw fullscreen triangle (3 verts, no input layout)
	//
	// Feature dispatch:
	//   SSGI → screenSpaceGI.DrawSSGI()
	//   SSS  → subsurfaceScattering.DrawSSS()
	//   Cubemaps → dynamicCubemaps.UpdateCubemap()
	//
	// After composite:
	//   dynamicCubemaps.PostDeferred()
}

	ID3D11BlendState* Deferred::GetOrCreateMRTBlendState(ID3D11BlendState* a_original)
	{
		if (!a_original) return nullptr;

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

		auto* device = reinterpret_cast<ID3D11Device*>(fo4cs::GetRendererData()->device);
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

	// --- OMSetBlendState hook — extends single-RT blend states to MRT during deferred pass ---
	namespace
	{
		using OMSetBlendStateFn = void(STDMETHODCALLTYPE*)(
			ID3D11DeviceContext*, ID3D11BlendState*, const FLOAT[4], UINT);
		OMSetBlendStateFn originalOMSetBlendState = nullptr;
		bool blendHookInstalled = false;

		void STDMETHODCALLTYPE OMSetBlendStateHook(
			ID3D11DeviceContext* a_context,
			ID3D11BlendState* a_blendState,
			const FLOAT a_blendFactor[4],
			UINT a_sampleMask)
		{
			auto* deferred = Deferred::GetSingleton();
			if (deferred->IsBlendOverridden() && a_blendState) {
				a_blendState = deferred->GetOrCreateMRTBlendState(a_blendState);
			}
			originalOMSetBlendState(a_context, a_blendState, a_blendFactor, a_sampleMask);
		}
	}

	void Deferred::OverrideBlendStates()
	{
		blendStatesOverridden = true;

		// Lazy-install OMSetBlendState vtable hook (ID3D11DeviceContext vfunc 26)
		if (!blendHookInstalled) {
			auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(fo4cs::GetRendererData()->context);
			if (ctx) {
				*(uintptr_t*)&originalOMSetBlendState =
					Detours::X64::DetourClassVTable(*(uintptr_t*)ctx, &OMSetBlendStateHook, 26);
				blendHookInstalled = true;
				logger::info("[Deferred] OMSetBlendState hook installed (vfunc 26)");
			}
		}
	}

	void Deferred::ResetBlendStates()
	{
		blendStatesOverridden = false;
	}

void Deferred::ClearShaderCache()
{
	// Release composite PS/VS shaders. Compilation is on-demand (lazy),
	// so clearing forces recompilation on the next frame.
}

	// --- Hook 触发验证日志 ---
	// 将 hook 名称和当前 shader 计数写入 hook_fire_log.txt，
	// 由 Python 验证脚本与 pipeline_trace.txt 交叉对比。
	void LogHookFire(const char* a_hookName)
	{
		auto shaderCount = CommunityShaders::ShaderCache::GetSingleton()->GetShaderCreationCount();
		auto traceDir = std::filesystem::path{ "Data" } / "F4SE" / "Plugins" / "CommunityShaders" / "ShaderDump" / "PipelineTrace" / CommunityShaders::State::GetSingleton()->GetRuntimeName();
		std::error_code ec;
		std::filesystem::create_directories(traceDir, ec);
		auto logFile = traceDir / "hook_fire_log.txt";
		std::ofstream out(logFile, std::ios::app);
		auto frameCount = CommunityShaders::Runtime::GetSingleton()->GetFrameCount();
		out << std::format("[HOOK] {} at frame={} shader_index={}\n", a_hookName, frameCount, shaderCount);
	}

	// --- Hooks into FO4 Creation Engine render pipeline ---
	// REL::IDs resolved via cross-reference with F4SE Address Library + decompiled export.
	// PostAE (1.11.191.0) analysis: 2026-05-07.
	// See .claude/docs/rel-id-analysis-results.md for full analysis.

// Detour a function at a raw absolute address (bypasses REL::ID resolution).
// Template type T provides T::thunk and T::func (REL::Relocation stored as uintptr_t).
template <class T>
static void detour_thunk_at(uintptr_t a_address) {
	*(uintptr_t*)&T::func = Detours::X64::DetourFunction(a_address, (uintptr_t)&T::thunk);
}

void Deferred::Hooks::Install()
{
	// Load the version-*.bin Address Library directly.
	// Returns the RVA offset for a given ID, or std::nullopt if not found.
	// Direct .bin access avoids REL::IDDB::id2offset() which calls TerminateProcess on miss.
	[[nodiscard]] static auto GetRELOffset = [](std::uint64_t a_id) -> std::optional<std::uint64_t> {
		struct mapping_t { std::uint64_t id; std::uint64_t offset; };
		static std::vector<mapping_t> s_id2offset;

		if (s_id2offset.empty()) {
#if defined(FALLOUT_POST_AE)
				const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
#else
				const auto version = REL::Module::get().version();
#endif
#if defined(FALLOUT_POST_NG)
			const auto path = std::format("Data/F4SE/Plugins/version-{}.bin", version.string("-"sv));
#else
			const auto path = std::format("Data/F4SE/Plugins/version-{}.bin", version.string());
#endif
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				logger::warn("[Deferred] Failed to open {}", path);
				return std::nullopt;
			}
			std::uint64_t count = 0;
			file.read(reinterpret_cast<char*>(&count), sizeof(count));
			if (!file || count == 0 || count > 5000000) {
				logger::warn("[Deferred] Invalid .bin count: {}", count);
				return std::nullopt;
			}
			s_id2offset.resize(count);
			file.read(reinterpret_cast<char*>(s_id2offset.data()), count * sizeof(mapping_t));
			if (!file) {
				logger::warn("[Deferred] Failed to read .bin entries");
				return std::nullopt;
			}
		}

		const mapping_t elem{ a_id, 0 };
		const auto it = std::lower_bound(
			s_id2offset.begin(), s_id2offset.end(), elem,
			[](auto& a_lhs, auto& a_rhs) { return a_lhs.id < a_rhs.id; });
		if (it != s_id2offset.end() && it->id == a_id)
			return it->offset;
		return std::nullopt;
	};



	if (!HasAddressLibrary()) {
		logger::info("[Deferred] Address Library not found — skipping pipeline hooks");
		return;
	}
#if defined(FALLOUT_POST_NG)
	// PostNG / PostAE REL::ID namespace (~2.27M - 2.32M range).
	// Each REL::ID is pre-validated via HasRELID() before the detour call.
	// Missing IDs are logged and skipped rather than crashing the process.

	int installed = 0, skipped = 0;
	for (auto [name, id] : {
		std::pair{"Main_RenderWorld", 2318315ull},
		std::pair{"Main_RenderShadowMaps", 2318298ull},
		std::pair{"Main_RenderWorld_Start", 2318312ull},
		std::pair{"Main_RenderWorld_BlendedDecals", 2318306ull},
		std::pair{"Renderer_ResetState", 2276833ull},
	}) {
		if (!GetRELOffset(id).has_value()) {
			logger::warn("[Deferred] REL::ID({}) not in address library — skipping {} hook", id, name);
			skipped++;
			continue;
		}
		logger::info("[Deferred] Found REL::ID({}) for {} hook", id, name);
		installed++;
	}

	if (installed > 0) {
		stl::detour_thunk<Main_RenderWorld>(REL::ID(2318315));
		stl::detour_thunk<Main_RenderShadowMaps>(REL::ID(2318298));
		stl::detour_thunk<Main_RenderWorld_Start>(REL::ID(2318312));
		stl::detour_thunk<Main_RenderWorld_BlendedDecals>(REL::ID(2318306));
		stl::detour_thunk<Renderer_ResetState>(REL::ID(2276833));
	} else {
		logger::warn("[Deferred] No pipeline hooks installed ({} IDs missing from address library)", skipped);
	}

#else
		// PreNG (1.10.163) — REL::ID offsets resolved from version-1-10-163-0.bin.
		// Uses GetRELOffset() + detour_thunk_at() to bypass F4SE runtime ID resolution,
		// which returns incorrect addresses for PreNG (different ID→RVA mapping internally).
		//
		// Verified .bin offsets (2026-05-09):
		//   Main_RenderShadowMaps:        ID 620025  → base+0x2850B1B
		//   Main_RenderWorld_Start:       ID 1108521 → base+0x28529B0
		//   Main_RenderWorld_BlendedDecals: ID 465756  → base+0x2851BBC
		//   Renderer_ResetState:          ID 153957  → base+0x1EB7BA0
		//   Main_RenderWorld:             ID 656535  → base+0x2856B70 (disabled: conflicts with PreUI_Forward)
		//
		auto base = REL::Module::get().base();
		int installed = 0, skipped = 0;

		struct HookEntry { const char* name; std::uint64_t id; };
		for (auto [name, id] : {
			HookEntry{"Main_RenderShadowMaps", 620025},
			HookEntry{"Main_RenderWorld_Start", 1108521},
			HookEntry{"Main_RenderWorld_BlendedDecals", 465756},
			HookEntry{"Renderer_ResetState", 153957},
		}) {
			auto rva = GetRELOffset(id);
			if (!rva) {
				logger::warn("[Deferred] PreNG REL::ID({}) not in .bin — skipping {}", id, name);
				skipped++;
				continue;
			}
			auto addr = base + *rva;
			logger::info("[Deferred] PreNG {}: ID({}) -> base+0x{:X} -> 0x{:X}", name, id, *rva, addr);
			installed++;
		}

			if (installed > 0) {
				// --- Runtime CALL instruction scanner ---
				// Scans +/-128 bytes around each target RVA for 0xE8 (CALL) opcodes.
				// Logs exact CALL addresses for future write_thunk_call hooks.
				logger::info("[Deferred] --- CALL scanner (target area) ---");
				for (auto [name, rva_val] : {
					std::pair{"ShadowMaps", 0x2850B1Bull},
					std::pair{"BlendedDecals", 0x2851BBCull},
					std::pair{"World_Start", 0x28529B0ull},
					std::pair{"ResetState", 0x1EB7BA0ull},
				}) {
					logger::info("[Deferred]   {} (@+0x{:X}):", name, rva_val);
					for (int off = -512; off <= 512; off++) {
						auto ptr = reinterpret_cast<const uint8_t*>(base + rva_val + off);
						if (*ptr == 0xE8 || *ptr == 0xE9) {
							int32_t rel = *reinterpret_cast<const int32_t*>(ptr + 1);
							auto target = base + rva_val + off + 5 + rel;
							const char* kind = (*ptr == 0xE8) ? "CALL" : "JMP ";
							logger::info("[Deferred]       +{: 4d} | 0x{:X} -> {} 0x{:X}",
								off, base + rva_val + off, kind, target);
						}
					}
				}
				logger::info("[Deferred] --- end scanner ---");
				
				// World_Start: detour at function entry (verified working)

				// Helper: decode CALL (0xE8) at address, return target
				auto decodeCallTarget = [](uintptr_t a_callAddr) -> uintptr_t {
					auto ptr = reinterpret_cast<const uint8_t*>(a_callAddr);
					if (*ptr != 0xE8) return 0;
					int32_t rel = *reinterpret_cast<const int32_t*>(ptr + 1);
					return a_callAddr + 5 + rel;
				};

				// World_Start: detour at function entry
				detour_thunk_at<Main_RenderWorld_Start>(base + *GetRELOffset(1108521));

				// ShadowMaps: write_thunk_call at CALL -50 (verified)
				stl::write_thunk_call<Main_RenderShadowMaps>(base + *GetRELOffset(620025) - 50);

				// BlendedDecals: write_thunk_call at CALL 0x2852755 (sub_142852590 -> sub_142851BD0)
				// IDA xref: sub_140D3CBE0 -> sub_142857480 -> sub_142852590 -> sub_142851BD0
				stl::write_thunk_call<Main_RenderWorld_BlendedDecals>(base + 0x2852755);
				// ResetState: no standalone function in PreNG (inline D3D11 state changes)
				logger::info("[Deferred] PreNG: World_Start + ShadowMaps + BlendedDecals installed");		} else {
			logger::warn("[Deferred] PreNG: no hooks installed ({} IDs missing from .bin)", skipped);
		}

	#endif
	logger::info("[Deferred] Pipeline hooks installed");
}

void Deferred::Hooks::Main_RenderShadowMaps::thunk()
{
	try {
		func();  // call original first (preserves regs for write_thunk_call)
		LogHookFire("Main_RenderShadowMaps");
		GetSingleton()->EarlyPrepasses();
	} catch (...) {
		logger::error("[Deferred] Main_RenderShadowMaps exception");
	}
}

void Deferred::Hooks::Main_RenderWorld::thunk()
{
	try {
		LogHookFire("Main_RenderWorld");
		func();
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld exception");
	}
}

void Deferred::Hooks::Main_RenderWorld_Start::thunk()
{
	try {
		LogHookFire("Main_RenderWorld_Start");
		GetSingleton()->StartDeferred();
		func();
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld_Start exception");
	}
}

void Deferred::Hooks::Main_RenderWorld_BlendedDecals::thunk()
{
	try {
		func();  // call original first (preserves regs for write_thunk_call)
		LogHookFire("Main_RenderWorld_BlendedDecals");
	} catch (...) {
		logger::error("[Deferred] Main_RenderWorld_BlendedDecals exception");
	}
}

void Deferred::Hooks::Renderer_ResetState::thunk(void* a_this)
{
	try {
		LogHookFire("Renderer_ResetState");
		func(a_this);
	} catch (...) {
		logger::error("[Deferred] Renderer_ResetState exception");
	}
}
