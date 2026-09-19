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

## Status at 2026-09-19

Phases A and B are complete and build-verified. Phase C is done except for
`AdditivePasses.cpp` and `LLFPixelTracker.cpp`, neither of which has an outline
yet, and the C9 thunk, which is still blocked on the fork decision.

| File | Before | Now |
|------|--------|-----|
| `src/Features/LightLimitFix.cpp` | 5081 | **72** (+ 14 units under `src/Features/LightLimit/`, largest 685) |
| `src/Core/BSShaderHooks.cpp` | 3508 | 895 (+ 9 units under `src/Core/ShaderHooks/`) — C9 thunk left |
| `src/Core/ShaderCache.cpp` | 2013 | 548 (+ 5 units under `src/Core/Shaders/`) |
| `src/Upscaling/Upscaler.cpp` | 1668 | 407+ (+ 8 units) |
| `src/Upscaling/UpscalerRenderBackend.cpp` | 1345 | 662 (+ 5 units) |
| `src/Render/DX12SwapChain.cpp` | 1212 | 323 (+ 4 units) |
| `src/Core/Deferred.cpp` | 1198 | 763 (+ 2 units under `src/Core/Deferred/`) |
| `src/Upscaling/Streamline.cpp` | 1111 | 550 (+ 5 units) |
| `src/Core/AdditivePasses.cpp` | 992 | **992 — not started** |
| `src/Core/LLFPixelTracker.cpp` | 991 | **991 — not started** |
| `src/Overlay/Overlay.cpp` | 658 | 691 (soft mark only) |

The largest file in the repository is now `src/Core/AdditivePasses.cpp` at 992
lines. No unit exceeds the 1000-line mark and none of the fourteen LightLimitFix
units exceeds 685.

### Gates now open (2026-09-16)

Two items below were parked on preconditions. Both were satisfied incidentally by the
performance and frame-generation work of 2026-09-15/16, so neither is still blocking:

1. **"Awaiting a game run"** (Phase C.2, the BSShaderHooks clusters). `codex/cs-refactor`
   had never been run in game when that was written. It now has been, repeatedly and
   under measurement — Tracy captures, FSR-FG and DLSS-G frame-generation validation,
   several hours of gameplay. The shader-lookup hot path those four clusters sit on has
   been exercised throughout.
2. **"wants a log-based before/after baseline first"** (Phase C.4, LightLimitFix). That
   baseline now exists and is unusually good: paired Tracy captures either side of the
   `VirtualQuery` fix, with per-zone self times, call counts and frame-interval
   histograms. See `llf-prepass-perf-handoff.md` in the main checkout. The active
   performance debugging that motivated the deferral is finished — 37-40 fps to 128 fps.

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

C.1, C.3 and C.4 are done. C.2 has only the C9 thunk left and that is
blocked on the fork decision. C.5 and C.6 have no outlines yet.

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
2. [~] `src/Core/BSShaderHooks.cpp` (3508 → 895) → `src/Core/ShaderHooks/`.
       Outline: `docs/refactor-outlines/outline-BSShaderHooks.md`
       (134 definitions, 12 clusters). **Nine units now exist** — the four listed
       below plus `PreNGVanillaDumps` (C6), `PreNGDescriptorDiagnostics` (C5),
       `PreNGLookupDiagnostics` (C4), `PreNGDescriptorBind` and
       `PreNGBSLightingBind` (C7/C8), and `PostNGShaderLookupHook` (C10). Only the
       C9 thunk remains in the façade. The first four, in the order the outline
       recommends:

       - `PreNGShaderHookConstants.h` (67) — the `RE::FO4Runtime` alias and 40
         constants. Left spelled `static constexpr`, not modernised to `inline
         constexpr`, so each including TU keeps its own identical copy exactly as
         before; nothing takes their addresses or compares the `const char*`
         values.
       - `PreNGRuntime.{h,cpp}` (65/151) — guarded memory access, the Debug.ini
         readers, descriptor normalisation, fxp-name matching. The two templates
         stay in the header because `ReadPreNGCString` instantiates
         `ReadPreNGValue<char>`.
       - `PreNGSwitches.{h,cpp}` (45/211) — all 26 gates, deliberately in one TU:
         each caches its lookup in a function-local `static const`, so one TU
         means one latch each and no Debug.ini read returns to the draw path.
       - `PreNGDescriptorPredicates.{h,cpp}` (42/189) — shader-family and
         descriptor classification. Two ranges rather than one, because
         `LogPreNGDFLightFullContractDescriptorBindHeld` sits between them and
         belongs to the diagnostics cluster.

       **Structural fact that shapes the rest:** lines 95–3013 of the original
       file were a single `#if defined(FALLOUT_PRE_NG)` region — 2919 of 3508
       lines. On PostNG/PostAE this file is under 600 lines of active code, so
       each new PreNG cluster wraps its body in that guard and compiles to
       nothing on the other two variants. Since 2026-09-09 all three variants
       build, so that emptiness is proven rather than assumed.

       Remaining: **C9 only** — the 474-line thunk, with eight function-local
       latches and a hot-path gate ordering that must not be disturbed. Plus a dead
       `TryBindPreNGDeferredLightingPixelShader` to delete as its own commit.

       **C9 is blocked on the CommonLibF4 fork decision — do not start it.** This is a
       separate gate from the game-run one cleared below, and it is still closed. The
       three `extern/CommonLibF4*` submodules are three *pins of the same repo*
       (`Symb0x76/CommonLibFO4`: PreNG `02f4668`, PostNG `278a609`, PostAE `ad6bc8d`),
       and that fork's `REL::ID` holds a single `std::uint64_t` — one address per ID, so
       the runtime must be chosen at build time. If the fork is swapped for one with
       `REL::ID({OG,NG,AE})`, `PreNGBSShaderLookup` and `PostNGShaderLookup` collapse
       into one `ShaderLookup_Hook` and C10 stops being a separate cluster. Extracting
       C9 as a macro-gated unit first restructures the file's highest-risk code twice.
       See `fcs-convergence-runtime-dispatch.md` §8.
       (C6 vanilla dumps, C5 descriptor diagnostics, C4 lookup diagnostics, C7/C8
       the GPU-state binds and C10 the PostNG hook block have all landed since this
       list was written.)

       ~~**Awaiting a game run.**~~ **Cleared 2026-09-16.** The concern was that
       compile-identical is not runtime-identical for the `static const` latch
       semantics, and that a regression riding in on unvalidated moves would be
       hard to attribute once later clusters landed on top. The branch has since
       been run in game extensively under measurement, with the shader-lookup hot
       path exercised throughout and no latch-behaviour anomalies observed.
3. [x] `src/Core/Deferred.cpp` (1198 → 763, under the 800 fail threshold).
       `DeferredTrace.cpp` (242) and `DeferredGBuffer.cpp` (232) extracted, sharing
       `DeferredInternal.h` (80). `LightingDrawState` reaches the trace cluster
       through that header as the outline required.

       Two of the outline's cautions turned out not to bind. `SetupResources`
       calling `InstallDrawHooks` across the new boundary needed nothing, because
       both are `Deferred` members declared in `Deferred.h`; the same goes for
       `GetOrCreateMRTBlendState`. And the binding tables did not need external
       linkage — every out-of-unit reader goes through the public static accessors
       `GetGBufferTargetBindings()` / `GetDeferredRenderTargetBindings()`, which
       return those very objects by const reference.

       Still in the façade and genuinely interleaved: the draw-hook cluster (TLS,
       the seven original function pointers, the install lock and vtable latch,
       which the outline is right to insist stay in one TU) and the hook-install
       cluster. `ClearShaderCache` still locks `lightingShaderLock` and
       `blendStateLock` together, so any lock taken in a new Deferred unit must
       respect that order.

       If the hook-install cluster (`HasAddressLibrary`, `LogHookFire`,
       `detour_thunk_at`, `Hooks::Install`, the five thunks; ~290 lines) is
       extracted later, note that it holds every `#if defined(FALLOUT_*)` region in
       the file plus the raw byte-scan relocation decode — and PostNG/PostAE cannot
       be built on this branch to verify it (see the pre-existing guard defects
       below), so PreNG would be the only witness.
4. [x] `src/Features/LightLimitFix.cpp` (5104 → **72**) → `src/Features/LightLimit/`,
       fourteen units plus `LLFInternal.h`. Outline:
       `outline-LightLimitFix.md`. Cluster assignments followed as written; the
       line numbers in it were stale as predicted and were re-derived per cut.

       | Unit | Functions (#) | Lines |
       |---|---|---|
       | `LLFClusterPrepass.cpp` | 97-99 | 685 |
       | `LLFLightCollect.cpp` | 114-116, 139-143 | 644 |
       | `LLFConfig.cpp` | 28-38, 44, 46-71, 73-76 | 556 |
       | `LLFInternal.h` | shared state, types, declarations | 502 |
       | `LLFPreviewMenu.cpp` | 39-43, 105, 106, 122-127 | 498 |
       | `LLFResources.cpp` | 2-4, 91, 94-96, 100, 101, 113 | 473 |
       | `LLFClusterBind.cpp` | 102-104, 107-112, 117-121, 132-137 | 399 |
       | `LLFDFLightForward.cpp` | 128-131 | 397 |
       | `LLFShaderMetadata.cpp` | 15-24, 138 | 384 |
       | `LLFHooks.cpp` | 93, 144-153 | 334 |
       | `LLFDiagnostics.cpp` | 45, 72, 78-80 | 272 |
       | `LLFLightDecode.cpp` | 5-14, 25, 26 | 243 |
       | `LLFPointLightHook.cpp` | 27, 81-85 | 206 |
       | `LLFRuntimeAddresses.cpp` | 1, 77 | 139 |
       | `LLFSettings.cpp` | 86-90, 92 | 116 |
       | `LightLimitFix.cpp` (facade) | cross-cluster state definitions | 72 |

       **The enabling step was naming the anonymous namespace.** The 1650-line
       anonymous namespace became `CommunityShaders::lightlimit`, which let the
       clusters be carved out one at a time: a helper still in the facade could
       be declared in `LLFInternal.h` and called from a cluster that had already
       moved. Same deliberate linkage change the `ShaderCache` split made. It
       does not touch function-local statics — those are one object per function
       under either linkage.

       **Verification that the move was actually a move.** Normalising both
       sides to significant lines (dropping blanks, comments, includes,
       preprocessor lines, namespace braces and using-directives) and comparing
       as multisets: **0 lines lost, 93 lines added, and all 93 are function
       declarations in `LLFInternal.h`.** No statement, branch, `REL::ID`,
       vtable index or offset differs. The seven DFLight forward consumer entry
       points (`write_vfunc<0x7>` x3, `BS_LIGHTING_BATCH_SETUP`,
       `DF_LIGHT_FORWARD_PIXEL_DESCRIPTOR_8004`,
       `IsDFLightForwardPixelDescriptor`, the `BSDFLightShader` thunk) are
       present and unchanged.

       **Latch discipline held.** No gate predicate is `inline` or
       header-defined; `LLFInternal.h` carries declarations only and says so.
       All gates live in one TU (`LLFConfig.cpp`) so each keeps one
       `static const` latch and one resolution log line.
       `ShouldTimePreNGClusterPrepassGpu` stays uncached.
       `s_preNGDFLightCameraCB` keeps its single definition in the facade.

       Three cautions in the outline did **not** survive contact:

       - §5's note that the members at about 3274-3653 and 3723-3918 are
         unguarded is **stale**. `e422d13` added the `FALLOUT_PRE_NG` guard when
         it made PostNG/PostAE compile; the split preserves it per half.
       - §8's note that `GetCurrentLightsSRV` still self-recurses on non-PreNG
         is **stale for this branch** — `e422d13` fixed it. It returns
         `lightsSRV.get()`. (On `community-shaders` the same fix exists only in
         the uncommitted working tree.)
       - `CapturePreNGDFLightCameraCBOnce` is listed as DFLightForward-only.
         The *state* is, but the *function* is called from the batch-setup thunk
         in the Hooks cluster. Being file-static hid that until the boundary
         existed. It is now declared in `LLFInternal.h`; the capture state stays
         private. The three preview-reason helpers cross the same way.

       Also removed: the empty `namespace RE::VTABLE {}` block, dead.

       Built PreNG, PostNG and PostAE, Release, `/W4 /WX`, zero warnings.
       **Runtime validation is still owed** — PostNG/PostAE have no game install
       here, and PreNG needs a run to confirm the LLF consumer path behaves as
       it did. Compile-clean is not runtime-identical.
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
- `LightLimitFix`: ~~`GetCurrentLightsSRV` has an infinite self-recursion in its
  non-PreNG branch~~ — **fixed on this branch by `e422d13`**; it returns
  `lightsSRV.get()`. Still live on `community-shaders`, where the same fix exists
  only in the uncommitted working tree. The two plain (non-atomic) frame-change
  statics in `TryReservePreNGSetupGeometryFrameSample` remain a genuine race if
  that function is ever called from two threads; they moved verbatim into
  `LLFConfig.cpp` and were deliberately not "fixed" during the move.
- `Render`/`Upscaling` and `Framework`/`Features` include cycles.

## Verification commands

```bash
cmake --preset PreNG
cmake --build build/PreNG --config Release
pwsh -File tools/check-file-sizes.ps1
```
