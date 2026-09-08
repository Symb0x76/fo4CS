# fo4CS `community-shaders` refactor plan (branch `codex/cs-refactor`)

Companion to `docs/main-refactor-plan.md` on `codex/main-refactor`. Source of
requirements: `.codex/docs/thermo-nuclear-code-quality-review.md` — the P0/P1 items
that only exist on this branch (`LightLimitFix`, `BSShaderHooks`, `ShaderCache`,
`Deferred`) plus the infrastructure the review asked for on both branches.

## Ground rules

Same as the `main` refactor:

- Every step must build `PreNG` Release with `/W4 /WX` and keep producing
  `CommunityShaders.dll` with unchanged exports.
- Phases A and B are move-only. No behaviour change, no renamed public symbols.
  Anything found broken goes in `## Deferred findings`, fixed separately.
- Function-local `static` latches (`logged*`, `previous*`, caches) move with their
  owning function and stay a single instance. Never make them `inline` in a header.
- `REL::ID(...)`, vtable indices, `+0x..` offsets and every `FALLOUT_*` conditional
  are transcribed verbatim.
- Runtime validation is manual and owned by BOSS.

Extra rule for this branch: **converge with `main`.** Where a file exists on both
branches, use the same target names, directory and split as `codex/main-refactor`,
so a later merge sees both sides moving a file to the same place instead of a
one-sided rename.

## Status at 2026-09-08

Phases A and B are complete and build-verified. Phase C is partially done.

| File | Before | Now |
|------|--------|-----|
| `src/Features/LightLimitFix.cpp` | 5081 | **5081 — not started** |
| `src/Core/BSShaderHooks.cpp` | 3508 | **3508 — not started** |
| `src/Core/ShaderCache.cpp` | 2013 | 548 (+ 5 units under `src/Core/Shaders/`) |
| `src/Upscaling/Upscaler.cpp` | 1668 | 407 (+ 8 units) |
| `src/Upscaling/UpscalerRenderBackend.cpp` | 1345 | 663 (+ 5 units) |
| `src/Render/DX12SwapChain.cpp` | 1212 | 323 (+ 4 units) |
| `src/Core/Deferred.cpp` | 1198 | **1198 — not started** |
| `src/Upscaling/Streamline.cpp` | 1111 | 467 (+ 5 units) |
| `src/Core/AdditivePasses.cpp` | 992 | **992 — not started** |
| `src/Core/LLFPixelTracker.cpp` | 991 | **991 — not started** |
| `src/Overlay/Overlay.cpp` | 658 | 658 (soft mark only) |

## Phase A — Infrastructure (ported from `codex/main-refactor`) — DONE

1. [x] `cmake/Fo4csTargets.cmake` with `fo4cs_configure_target(<target> [NO_PCH])`,
       `fo4cs_add_object_library`, `fo4cs_configure_plugin_link`,
       `fo4cs_link_runtime`, `fo4cs_mark_third_party_sources`. It absorbed this
       branch's `fo4cs_apply_msvc_compile_options`; `fo4cs_add_imgui_sources` stays
       as a shim. Verified against a snapshot of the generated project files:
       `Core` and `CoreCS` byte-identical, the plugin only gaining
       `FO4CS_ENABLE_DEBUG_SETTINGS=1` in Debug/RelWithDebInfo (read only by
       `Upscaler.cpp`) and `Core` the generated include dir.
2. [x] `Fo4cs.Runtime` INTERFACE target carries the `FALLOUT_*` macros; the
       directory-wide `add_compile_definitions` is gone, so CommonLibF4 no longer
       compiles with them. Dead `FO4CS_BUILD_*`, `GamePath` and
       `FO4CS_RUNTIME_FLAVOR` removed. Configure prints one summary line.
3. [x] `Core` → `Fo4cs.Render` + `Fo4cs.Upscaling` + `Fo4cs.ImGui`;
       `CoreCS` → `Fo4cs.Framework` + `Fo4cs.Shaders` + `Fo4cs.Passes` +
       `Fo4cs.Features` + `Fo4cs.Overlay`. Header-only `Fo4cs.Platform` and
       `Fo4cs.Diagnostics`.
4. [x] Directory moves to match `main`: `src/Render/`, `src/Upscaling/`,
       `src/Platform/` (+ `RE/`), `src/Plugins/`. 65 includes rewritten from an
       explicit header table so CommonLibF4's `RE/Fallout.h` was untouched.
5. [x] `Diagnostics/LogPaths.h` replaces the duplicated log-directory helper;
       `HangTrace` moved to `fo4cs::diagnostics`; `LogEvents.h` gained `LogEvent()`
       and BOOT / DEVICE_READY / HOOK_INSTALL / FEATURE_STATE / RESOURCE_CREATE /
       ERROR are emitted.
6. [x] `tools/check-file-sizes.ps1`, `tools/collect-runtime-log.ps1`,
       `tools/validate-runtime-log.ps1`, retargeted at this branch's single log.
7. [x] `/docs` un-ignored so this plan, the architecture doc and the validation
       matrix are tracked, matching `main`. Revert that `.gitignore` line if the
       exclusion was deliberate.

## Phase B — Shared-file splits ported from `main` — DONE

1. [x] `Streamline.cpp` — byte-identical to main's pre-split version, so the split
       landed verbatim: `StreamlineInternal.{h,cpp}`, `StreamlineDLSS.cpp`,
       `StreamlineReflex.cpp`, `StreamlineFrameGeneration.cpp`.
       `Platform/ModulePaths.h` came along with it.
2. [x] `Upscaler.cpp` — same cluster shape as main, line ranges derived here:
       `UpscalingHooks`, `FramePacing`, `FrameGenResources`, `FrameGenCapture`,
       `FrameGenComposite`, `UpscalingRenderTargetIDs.h`, `UpscalingShaderCompile`,
       `UpscalingInternal`. This branch's `NormalizeRealFrameRateLimit` /
       `FormatRealFrameRate` became shared internals because both the settings code
       and the frame-limiter policy call them.
3. [x] `UpscalerRenderBackend.cpp` — `UpscalerRenderTargets`, `UpscalerDepthBuffer`,
       `UpscalerSamplerStates`, `UpscalerRenderBackendShaders`,
       `UpscalerRenderBackendHooks`. No API deleted (unlike main: the render-target
       and depth override entry points have callers in the feature layer here).
4. [x] `DX12SwapChain.cpp` — main's clusters plus this branch's HDR pass:
       `DX12SwapChainPresent`, `DX12SwapChainColorSpace`, `DXGISwapChainProxy`,
       `WrappedResource`, `DX12SwapChainInternal.h`.

## Phase C — Community Shaders hotspots — PARTIALLY DONE

Per-file outlines produced 2026-09-08 are checked in under
`docs/refactor-outlines/`. They carry exact function line ranges, cluster
proposals, shared-state maps, `FALLOUT_*` region lists, raw game-address
inventories and the "cannot move without changing behaviour" list for each file.
**Read the matching outline before touching any of these files** — but re-verify
the line numbers first, they drift and several were off by one.

1. [x] `src/Core/ShaderCache.cpp` (2013 → 548) → `src/Core/Shaders/`
       - `ShaderObservation.cpp`, `ShaderMetadata.cpp`, `ShaderCacheLog.cpp` —
         leaf clusters, no linkage change.
       - `ShaderSwitches.cpp`, `ShaderDescriptorGates.cpp` +
         `ShaderCacheInternal.h` — the one deliberate linkage change in the whole
         refactor: an anonymous namespace became
         `CommunityShaders::shadercache`. Every predicate keeps exactly one
         definition because several own a one-shot cache that logs on first call.
2. [ ] `src/Core/BSShaderHooks.cpp` (3508) → `src/Core/ShaderHooks/`.
       Outline: `docs/refactor-outlines/outline-BSShaderHooks.md`
       (134 definitions, 12 clusters).
3. [ ] `src/Core/Deferred.cpp` (1198). Outline: the second half of
       `outline-ShaderCache-Deferred.md`. Note the clusters interleave (the draw-hook
       cluster is four non-contiguous ranges), `LightingDrawState` has to reach the
       trace cluster through a header, and `ClearShaderCache` locks
       `lightingShaderLock` and `blendStateLock` together.
4. [ ] `src/Features/LightLimitFix.cpp` (5081) → `src/Features/LightLimit/`.
       Outline: `outline-LightLimitFix.md` (153 functions, 14 clusters).
       **Highest risk in the repository** and deliberately left last:
       - roughly 100 function-local statics, many of them one-shot log latches
         whose duplication would change logging behaviour;
       - two enormous `#if defined(FALLOUT_PRE_NG)` regions (about 262–1822 and
         3919–4665) so every extracted chunk needs its own guard;
       - `s_preNGDFLightCameraCB` is a COM pointer shared between the capture hook
         and the compute dispatch and must stay one object;
       - the member definitions at about 3274–3653 and 3723–3918 are PreNG-named but
         compiled unconditionally today — wrapping them in a guard *is* a behaviour
         change for the PostNG build;
       - this is the feature under active performance debugging, so it wants a
         log-based before/after baseline first.
5. [ ] `src/Core/AdditivePasses.cpp` (992), `src/Core/LLFPixelTracker.cpp` (991) —
       no outline produced yet.
6. [ ] `src/Overlay/Overlay.cpp` (658) — soft mark only.

## Phase D — Documentation

- [x] `docs/architecture.md` — targets, dependency direction (two cycles recorded
      as debt), the feature framework and shader replacement pipeline, hook
      ownership, lifecycle with events, threads, variants, log contract.
- [x] `docs/validation-matrix.md` — automated checks plus the manual in-game matrix
      including the light-heavy scenarios that matter for LightLimitFix.
- [x] `docs/refactor-outlines/` — the three per-file analyses.
- [x] `.claude/docs/current-state.md` refreshed (in the main checkout, gitignored).

## Deferred findings

Not fixed, by design — these are pre-existing and the move-only phases preserved
them verbatim.

- `ShaderCache`: `dumpAllShaders` / `tracePipeline` are plain `bool`s written from
  config and read from the render thread without synchronisation.
- `Deferred`: `gBufferResourcesReady` / `gBufferDescriptions` are written in
  `SetupResources` and read from the draw-hook path without atomics.
- `LightLimitFix`: `GetCurrentLightsSRV` has an infinite self-recursion in its
  non-PreNG branch; two plain (non-atomic) frame-change statics are a genuine race
  if that function is ever called from two threads.
- `Render`/`Upscaling` and `Framework`/`Features` include cycles.

## Verification commands

```bash
cmake --preset PreNG
cmake --build build/PreNG --config Release
pwsh -File tools/check-file-sizes.ps1
```
