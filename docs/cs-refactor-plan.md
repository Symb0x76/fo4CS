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

## Starting state (2026-09-08)

Single DLL by default: `if(COMMUNITY_SHADERS)` forces `AIO`, `FRAMEGEN`, `REFLEX`,
`UPSCALER`, `OVERLAY` off, so `Core` + `CoreCS` link into `CommunityShaders.dll`.

| File | Lines | Shared with `main`? |
|------|-------|---------------------|
| `src/Features/LightLimitFix.cpp` | 5081 | CS only |
| `src/Core/BSShaderHooks.cpp` | 3508 | CS only |
| `src/Core/ShaderCache.cpp` | 2013 | CS only |
| `src/Upscaler.cpp` | 1668 | yes, +229/-86 vs main |
| `src/UpscalerRenderBackend.cpp` | 1345 | yes, +98/-32 |
| `src/DX12SwapChain.cpp` | 1212 | yes, +1073/-733 (largely rewritten) |
| `src/Core/Deferred.cpp` | 1198 | CS only |
| `src/Streamline.cpp` | 1111 | **identical** to main |
| `src/Core/AdditivePasses.cpp` | 992 | CS only |
| `src/Core/LLFPixelTracker.cpp` | 991 | CS only |
| `src/Overlay/Overlay.cpp` | 658 | CS only (over 600 soft mark) |

`src/Buffer.h` is also identical to main. The branch already has
`fo4cs_apply_msvc_compile_options()` and `fo4cs_add_imgui_sources()` in
`cmake/XSEPlugin.cmake` — part of the CMake dedupe `main` got later, so Phase A
absorbs rather than replaces them.

## Phase A — Infrastructure (port from `codex/main-refactor`)

1. [ ] `cmake/Fo4csTargets.cmake`: `fo4cs_configure_target(<target> [NO_PCH])`,
       `fo4cs_add_object_library`, `fo4cs_configure_plugin_link`,
       `fo4cs_link_runtime`. Absorb the existing `fo4cs_apply_msvc_compile_options`
       and `fo4cs_add_imgui_sources`. Verify generated project files are unchanged.
2. [ ] `Fo4cs.Runtime` INTERFACE target carries `FALLOUT_*`; drop the directory-wide
       `add_compile_definitions` and the dead `FO4CS_BUILD_*` / `GamePath` /
       `FO4CS_RUNTIME_FLAVOR` variables. Print a configure summary.
3. [ ] Replace `Core` + `CoreCS` with focused OBJECT libraries:
       `Fo4cs.Render`, `Fo4cs.Upscaling`, `Fo4cs.ImGui`, `Fo4cs.Overlay`
       (same names as `main`), plus CS-only `Fo4cs.Shaders` (shader cache,
       compiler, DB, hooks), `Fo4cs.Features` (feature framework + features),
       `Fo4cs.Presentation`. Header-only `Fo4cs.Platform`, `Fo4cs.Diagnostics`.
4. [ ] Directory moves (`git mv`) to match `main`: `src/Render/`, `src/Upscaling/`,
       `src/Platform/` (+ `RE/`), `src/Plugins/`. `src/Core/`, `src/Features/`,
       `src/Overlay/`, `src/Presentation/`, `src/Diagnostics/` stay.
5. [ ] `Diagnostics/LogPaths.h`, `LogEvents.h` with `LogEvent()`, and the event
       emission sites. This branch logs to one shared `CommunityShaders.log`, so
       the validator's per-plugin assumption is replaced by a single-log mode.
6. [ ] `tools/check-file-sizes.ps1`, `tools/collect-runtime-log.ps1`,
       `tools/validate-runtime-log.ps1`.

## Phase B — Port the shared-file splits from `main`

Same target files and names as `docs/main-refactor-plan.md` §2a–2d.

1. [ ] `Streamline.cpp` — identical to main, port the split verbatim
       (`StreamlineInternal.{h,cpp}`, `StreamlineDLSS.cpp`, `StreamlineReflex.cpp`,
       `StreamlineFrameGeneration.cpp`).
2. [ ] `Upscaler.cpp` — port, then re-check the CS-only deltas.
3. [ ] `UpscalerRenderBackend.cpp` — port.
4. [ ] `DX12SwapChain.cpp` — largely rewritten here; use main's cluster shape
       (creation / Present / proxy / wrapped resource) but derive the line ranges
       from this branch's file.

## Phase C — Community Shaders hotspots

Driven by per-file outlines produced 2026-09-08. Targets ≤ ~600 lines.

1. [ ] `src/Features/LightLimitFix.cpp` (5081) → `src/Features/LightLimit/`
2. [ ] `src/Core/BSShaderHooks.cpp` (3508) → `src/Core/ShaderHooks/`
3. [ ] `src/Core/ShaderCache.cpp` (2013) → `src/Core/Shaders/`
4. [ ] `src/Core/Deferred.cpp` (1198)
5. [ ] `src/Core/AdditivePasses.cpp` (992), `src/Core/LLFPixelTracker.cpp` (991)
6. [ ] `src/Overlay/Overlay.cpp` (658, soft mark only)

## Phase D — Documentation

- [ ] `docs/architecture.md` for this branch: the extra Core/Feature/Shader layers
      on top of main's, feature lifecycle, shader replacement pipeline.
- [ ] `docs/validation-matrix.md`: CS scenarios (light-heavy interior/exterior for
      LightLimitFix, shader cache cold/warm, feature toggles).
- [ ] `.claude/docs/current-state.md` refreshed.

## Deferred findings

(To be filled as the splits surface them; not fixed during move-only phases.)

## Verification commands

```bash
cmake --preset PreNG
cmake --build build/PreNG --config Release
pwsh -File tools/check-file-sizes.ps1
```
