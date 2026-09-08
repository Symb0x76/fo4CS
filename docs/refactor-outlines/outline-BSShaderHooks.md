# BSShaderHooks.cpp refactor outline

Source: `D:\Projects\CnCpp\fo4CS-cs-refactor\src\Core\BSShaderHooks.cpp` (3508 lines)
Public header: `src/Core/BSShaderHooks.h` (Install / OnFrame / ModifyShaderLookup / ReplacePixelShaders)
Everything lives directly in `namespace CommunityShaders` — **there is no anonymous namespace anywhere in this file**.

## 1. Every definition in the .cpp (exact ranges; END = closing brace line)

Kind: F=function, S=struct, E=enum, G=file-scope global block, D=forward declaration.

| # | Kind | Name | Start | End | Lines | Responsibility |
|---|---|---|---|---|---|---|
| 1 | G | includes + `namespace CommunityShaders` open | 1 | 29 | 29 | includes / namespace open |
| 2 | G | PreNG constants + env-switch name table | 42 | 87 | 46 | `kPreNG*` shader-type ids, diag caps, env var names (PreNG-only) |
| 3 | G | `kStableFrame` | 88 | 88 | 1 | drain gate frame threshold (all builds) |
| 4 | D | fwd `ReplacePixelShaders`, `CompileReplacementPS` | 92 | 94 | 3 | forward declarations |
| 5 | F | `IsReadableMemory` | 96 | 99 | 4 | wrap `F4Runtime::IsReadableAddress` |
| 6 | F | `IsWritableMemory` | 101 | 104 | 4 | wrap `F4Runtime::IsWritableAddress` |
| 7 | F | `WritePreNGValue<T>` | 106 | 110 | 5 | templated guarded write |
| 8 | F | `ReadPreNGEnvironmentSwitch` | 112 | 115 | 4 | DebugSwitches bool read |
| 9 | E | `PreNGEnvironmentValueSource` | 117 | 121 | 5 | env value provenance enum |
| 10 | S | `PreNGEnvironmentUIntState` | 123 | 129 | 7 | env uint read result |
| 11 | F | `PreNGEnvironmentValueSourceName` | 131 | 139 | 9 | enum → string |
| 12 | F | `ToPreNGEnvironmentSource` | 141 | 146 | 6 | DebugSwitches::Source → local enum |
| 13 | F | `ReadPreNGEnvironmentUInt` | 148 | 157 | 10 | DebugSwitches uint read |
| 14 | F | `GetPreNGDFLightFullShadowedCandidateBindBudget` | 159 | 195 | 37 | budget resolve + clamp + one-shot log |
| 15 | F | `ShouldBindPreNGDescriptorShaders` | 197 | 201 | 5 | env gate |
| 16 | F | `ShouldBindPreNGDFLightFullContractDescriptorShader` | 203 | 207 | 5 | env gate |
| 17 | F | `ShouldCompilePreNGBSLightingContractShader` | 209 | 213 | 5 | env gate |
| 18 | F | `ShouldCompilePreNGBSLightingConsumerShader` | 215 | 219 | 5 | env gate |
| 19 | F | `ShouldObservePreNGBSLightingDescriptors` | 221 | 225 | 5 | env gate |
| 20 | F | `ShouldBindPreNGBSLightingDescriptorResources` | 227 | 231 | 5 | env gate |
| 21 | F | `ShouldBindPreNGBSLightingVanillaDescriptorShader` | 233 | 237 | 5 | env gate |
| 22 | F | `ShouldBindPreNGBSLightingLLFConsumerShader` | 239 | 243 | 5 | env gate |
| 23 | F | `ShouldCompilePreNGDFLightFullContractDescriptorShader` | 245 | 249 | 5 | env gate |
| 24 | F | `ShouldObservePreNGDFLightDescriptors` | 251 | 255 | 5 | env gate |
| 25 | F | `ShouldObservePreNGDFCompositeDescriptors` | 257 | 261 | 5 | env gate |
| 26 | F | `ShouldCompilePreNGDFCompositeDescriptorShader` | 263 | 267 | 5 | env gate |
| 27 | F | `ShouldBindPreNGDFCompositeDescriptorResources` | 269 | 273 | 5 | env gate |
| 28 | F | `ShouldBindPreNGDFCompositeVisibleDescriptorResources` | 275 | 283 | 9 | env gate + descriptor whitelist 0x88/0x10088 |
| 29 | F | `ShouldBindPreNGDFCompositeSafeDescriptorShader` | 285 | 289 | 5 | env gate |
| 30 | F | `ShouldBindPreNGDFCompositeFogSafeDescriptorShader` | 291 | 295 | 5 | env gate |
| 31 | F | `ShouldPersistPreNGClusterPrepass` | 297 | 301 | 5 | env gate |
| 32 | F | `ShouldMutatePreNGDescriptorShaders` | 303 | 307 | 5 | env gate |
| 33 | F | `ShouldEnablePreNGShaderLookupDiagnostic` | 309 | 313 | 5 | env gate |
| 34 | F | `ShouldCompilePreNGDescriptorShadersForDiagnostic` | 315 | 319 | 5 | env gate |
| 35 | F | `ShouldBindPreNGDFLightFullShadowedCandidate` | 321 | 325 | 5 | env gate |
| 36 | F | `ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer` | 327 | 340 | 14 | double gate + unsafe-override warn |
| 37 | F | `ShouldDumpPreNGBSLightingVanillaShader` | 342 | 346 | 5 | env gate |
| 38 | F | `ShouldDumpPreNGDFLightVanillaShader` | 348 | 352 | 5 | env gate |
| 39 | F | `ShouldDumpPreNGDFCompositeVanillaShader` | 354 | 358 | 5 | env gate |
| 40 | F | `NormalizePreNGLightingVertexDescriptor` | 360 | 363 | 4 | runtime descriptor normalize |
| 41 | F | `NormalizePreNGLightingPixelDescriptor` | 365 | 368 | 4 | runtime descriptor normalize |
| 42 | F | `ReadPreNGValue<T>` | 370 | 374 | 5 | templated guarded read |
| 43 | F | `ReadPreNGPointer` | 376 | 379 | 4 | guarded pointer read |
| 44 | F | `ReadPreNGShaderEntryD3DObject` | 381 | 384 | 4 | shader-entry → D3D object ptr |
| 45 | F | `ReadPreNGCString` | 386 | 410 | 25 | safe bounded C-string read |
| 46 | F | `NormalizePreNGFxpFilename` | 412 | 420 | 9 | lowercase + slash normalize |
| 47 | F | `IsPreNGFxpBaseName` | 422 | 440 | 19 | basename/extension compare |
| 48 | F | `IsPreNGBSLightingFxpName` | 442 | 445 | 4 | fxp name predicate |
| 49 | F | `IsPreNGDFLightFxpName` | 447 | 450 | 4 | fxp name predicate |
| 50 | F | `IsPreNGDFCompositeFxpName` | 452 | 455 | 4 | fxp name predicate |
| 51 | F | `IsPreNGLightingDescriptorShader(type, fxp)` | 457 | 461 | 5 | type+name predicate |
| 52 | F | `IsPreNGLightingDescriptorShader(BSShader*)` | 463 | 479 | 17 | overload reading fxpFilename |
| 53 | F | `IsPreNGDFCompositeDescriptorShader` | 481 | 489 | 9 | DFComposite predicate |
| 54 | F | `IsPreNGDFCompositeContractDescriptorShader` | 491 | 495 | 5 | + observed-descriptor filter |
| 55 | F | `IsPreNGDFCompositeSafeBindDescriptorShader` | 497 | 505 | 9 | safe/fog-safe descriptor whitelist |
| 56 | F | `IsPreNGDFLightFullShadowedPixelDescriptor` | 508 | 511 | 4 | descriptor predicate |
| 57 | F | `IsPreNGDFLightFullShadowedDescriptorShader` | 513 | 522 | 10 | shader+descriptor predicate |
| 58 | F | `IsPreNGDFLightFullContractDescriptorShader` | 524 | 533 | 10 | shader+descriptor predicate |
| 59 | F | `IsPreNGDFLightLLFConsumerDescriptorShader` | 535 | 546 | 12 | LLF-consumer descriptor predicate |
| 60 | F | `LogPreNGDFLightFullContractDescriptorBindHeld` | 548 | 573 | 26 | rate-limited "bind held" log |
| 61 | F | `IsPreNGDFLightFullShadowedDescriptorConsumerShader` | 575 | 579 | 5 | gated predicate |
| 62 | F | `CanActivelyReplacePreNGLightingDescriptorShader` | 581 | 590 | 10 | replacement eligibility |
| 63 | F | `IsPreNGDFLightFullShadowedCandidateLookup` | 592 | 601 | 10 | candidate lookup predicate |
| 64 | F | `IsPreNGBSLightingVanillaDumpLookup` | 603 | 611 | 9 | dump-target predicate |
| 65 | F | `IsPreNGBSLightingContractPixelDescriptor` | 613 | 616 | 4 | descriptor predicate |
| 66 | F | `IsPreNGBSLightingContractDescriptorShader` | 618 | 622 | 5 | combined predicate |
| 67 | F | `IsPreNGDFLightVanillaDumpLookup` | 624 | 636 | 13 | dump-target predicate |
| 68 | F | `IsPreNGDFCompositeVanillaDumpLookup` | 638 | 641 | 4 | dump-target predicate |
| 69 | F | `GetPreNGDFLightVanillaDumpFamily` | 643 | 655 | 13 | descriptor → family label |
| 70 | S | `PreNGDFLightFullShadowedCandidateState` | 657 | 663 | 7 | owned candidate PS state |
| 71 | S | `PreNGDFLightVanillaDumpKey` | 665 | 669 | 5 | dump dedup key |
| 72 | S | `PreNGBSLightingVanillaBindAlias` | 671 | 677 | 7 | vanilla PS alias entry |
| 73 | G | PreNG candidate / dump / bind mutable state | 679 | 711 | 33 | 29 globals (see §3) |
| 74 | F | `GetPreNGDFLightFullShadowedCandidateState` | 713 | 718 | 6 | 920/922 state selector |
| 75 | F | `ShouldDumpPreNGBSLightingVanillaObject` | 720 | 735 | 16 | dump dedup + cap |
| 76 | F | `ShouldDumpPreNGDFLightVanillaObject` | 737 | 752 | 16 | dump dedup + cap |
| 77 | F | `ShouldDumpPreNGDFCompositeVanillaObject` | 754 | 769 | 16 | dump dedup + cap |
| 78 | F | `TryReservePreNGBSLightingVanillaPendingProbeFrame` | 771 | 786 | 16 | one-probe-per-frame CAS reservation |
| 79 | S | `PreNGShaderLookupDiagnosticKey` | 788 | 796 | 9 | lookup dedup key |
| 80 | F | `SamePreNGShaderLookupKey` | 798 | 806 | 9 | key equality |
| 81 | S | `PreNGShaderLookupFirstSeenKey` | 808 | 817 | 10 | first-seen dedup key |
| 82 | F | `SamePreNGShaderLookupFirstSeenKey` | 819 | 830 | 12 | key equality |
| 83 | G | lookup diagnostic locks + counters | 832 | 843 | 12 | 12 globals (see §3) |
| 84 | F | `ShouldBypassPreNGShaderLookupHeavyDiagnostics` | 845 | 848 | 4 | hot-path bypass flag read |
| 85 | F | `MaybeCompletePreNGShaderLookupHeavyDiagnostics` | 850 | 883 | 34 | budget-based diag shutdown + summary log |
| 86 | F | `IsPreNGPowerOfTwo` | 885 | 888 | 4 | log-rate helper |
| 87 | F | `ShouldLogPreNGShaderLookupEntry` | 890 | 904 | 15 | entry log rate policy |
| 88 | F | `TracePreNGShaderLookupFirstSeen` | 906 | 980 | 75 | first-seen dedup + log |
| 89 | F | `TracePreNGShaderLookupEntry` | 982 | 1043 | 62 | per-call counters + entry log |
| 90 | F | `ShouldLogPreNGShaderLookup` | 1045 | 1060 | 16 | lookup dedup + cap |
| 91 | S | `PreNGDescriptorMutationDiagnosticKey` | 1062 | 1071 | 10 | mutation dedup key |
| 92 | F | `SamePreNGDescriptorMutationKey` | 1073 | 1082 | 10 | key equality |
| 93 | G | mutation diag lock + vector | 1084 | 1085 | 2 | 2 globals |
| 94 | F | `ShouldLogPreNGDescriptorMutation` | 1087 | 1102 | 16 | mutation dedup + cap |
| 95 | F | `LogPreNGDescriptorMutation` | 1104 | 1143 | 40 | descriptor mutation log |
| 96 | S | `PreNGDescriptorBindDiagnosticKey` | 1145 | 1154 | 10 | bind dedup key |
| 97 | F | `SamePreNGDescriptorBindKey` | 1156 | 1165 | 10 | key equality |
| 98 | G | bind diag lock + vector | 1167 | 1168 | 2 | 2 globals |
| 99 | F | `ShouldLogPreNGDescriptorBind` | 1170 | 1185 | 16 | bind dedup + cap |
| 100 | D | fwd `LogPreNGDescriptorBind` | 1187 | 1198 | 12 | needed by #101 (defined at 1467) |
| 101 | F | `TryBindPreNGDFCompositeDescriptorPixelShader` | 1200 | 1293 | 94 | DFComposite owned-PS bind + LLF resources |
| 102 | F | `TracePreNGShaderLookup` | 1295 | 1368 | 74 | lighting lookup trace + ShaderCache observe |
| 103 | F | `TracePreNGDFCompositeDescriptor` | 1370 | 1430 | 61 | DFComposite descriptor observation log |
| 104 | F | `ObservePreNGDFCompositeDescriptorShader` | 1432 | 1465 | 34 | ShaderCache VS/PS observe only |
| 105 | F | `LogPreNGDescriptorBind` | 1467 | 1526 | 60 | descriptor custom-bind log |
| 106 | F | `GetPreNGBSLightingVanillaBindAlias` | 1528 | 1563 | 36 | create/reuse vanilla PS alias entry |
| 107 | F | `TryBindPreNGBSLightingVanillaPixelShader` | 1565 | 1702 | 138 | BSLighting vanilla-equivalent proof bind |
| 108 | F | `TryBindPreNGBSLightingLLFConsumerPixelShader` | 1709 | 1862 | 154 | BSLighting visible LLF consumer bind (production) |
| 109 | F | `DumpPreNGBSLightingVanillaShader` | 1864 | 1935 | 72 | dump vanilla BSLighting PS bytecode |
| 110 | F | `DumpPreNGDFLightVanillaShader` | 1937 | 2005 | 69 | dump vanilla DFLight PS bytecode |
| 111 | F | `DumpPreNGDFCompositeVanillaShader` | 2007 | 2078 | 72 | dump vanilla DFComposite PS bytecode |
| 112 | F | `GetPreNGDFLightFullShadowedCandidatePixelShader` | 2080 | 2170 | 91 | compile+create candidate PS, register metadata |
| 113 | F | `TryBindPreNGDFLightFullShadowedCandidate` | 2172 | 2268 | 97 | budgeted candidate PS proof bind |
| 114 | F | `TryBindPreNGDescriptorShaders` | 2270 | 2336 | 67 | generic owned VS+PS bind |
| 115 | F | `TryBindPreNGDFLightDescriptorPixelShader` | 2338 | 2428 | 91 | DFLight owned-PS bind + LLF resources |
| 116 | F | `ValidatePreNGShaderPath` | 2430 | 2460 | 31 | vtable/address sanity check at Install |
| 117 | F | `TryBindPreNGDeferredLightingPixelShader` | 2462 | 2501 | 40 | **DEAD CODE — zero call sites repo-wide** |
| 118 | S | `PreNGBSShaderLookup` (detour) | 2503 | 2976 | 474 | PreNG BSShader lookup detour: gates, mutation, dispatch to every path |
| 119 | F | `LogPreNGShaderLookupDetourPatch` | 2978 | 3012 | 35 | verify E9 patch/trampoline at lookup addr |
| 120 | G | PostNG constants + counters | 3021 | 3031 | 11 | `F4Runtime` alias, shader type, 2 env names, 2 counters |
| 121 | F | `ShouldObservePostNGBSLightingDescriptors` | 3033 | 3036 | 4 | env gate (no caching) |
| 122 | F | `ShouldBindPostNGBSLightingLLFConsumerShader` | 3038 | 3041 | 4 | env gate (no caching) |
| 123 | F | `ReadPostNGPointer` | 3043 | 3046 | 4 | guarded pointer read |
| 124 | F | `ReadPostNGShaderEntryD3DObject` | 3048 | 3055 | 8 | shader entry → D3D object |
| 125 | F | `TryBindPostNGBSLightingLLFConsumerPixelShader` | 3057 | 3157 | 101 | PostNG/AE visible LLF consumer bind |
| 126 | S | `PostNGShaderLookup` (detour) | 3159 | 3213 | 55 | PostNG BSShader lookup detour |
| 127 | S | `PendingReplace` + `s_pending` | 3218 | 3219 | 2 | deferred queue item + queue global |
| 128 | F | `DrainPending` (static) | 3221 | 3238 | 18 | drain queue when device stable |
| 129 | S | `BSShader_ReloadShaders` (detour) | 3242 | 3260 | 19 | ReloadShaders vfunc detour + enqueue |
| 130 | F | `ReplacePixelShaders` | 3262 | 3293 | 32 | PreNG: held; PostNG: replace each PS entry |
| 131 | F | `CompileReplacementPS` (static) | 3300 | 3331 | 32 | compile Lighting.hlsl with Feature defines |
| 132 | F | `ModifyShaderLookup` | 3335 | 3353 | 19 | public descriptor normalize entry point |
| 133 | F | `BSShaderHooks::Install` | 3357 | 3502 | 146 | validate, detour ReloadShaders + lookup, startup logging |
| 134 | F | `BSShaderHooks::OnFrame` | 3504 | 3507 | 4 | calls `DrainPending` |

## 2. Proposed clusters (target ≤ ~600 lines / TU)

New directory: `src/Core/ShaderHooks/`. CMakeLists.txt lists sources **explicitly** (no GLOB) — every new .cpp must be added around `CMakeLists.txt:133`.

| # | Cluster / files | Source ranges | ~Lines |
|---|---|---|---|
| C0 | `ShaderHooks/PreNGShaderHookConstants.h` (header only) | 42-88 | 47 |
| C1 | `ShaderHooks/PreNGRuntime.{h,cpp}` — memory/string/env/fxp primitives | 96-157, 360-455, 885-888 | ~170 |
| C2 | `ShaderHooks/PreNGSwitches.{h,cpp}` — every `Should*` env gate + bind budget | 159-358 | 200 |
| C3 | `ShaderHooks/PreNGDescriptorPredicates.{h,cpp}` — shader/descriptor classification | 457-546, 575-655 | 171 |
| C4 | `ShaderHooks/PreNGLookupDiagnostics.{h,cpp}` — lookup dedup, first-seen, entry trace, heavy-diag shutdown, detour patch check | 788-1060, 1295-1368, 2978-3012 | ~382 |
| C5 | `ShaderHooks/PreNGDescriptorDiagnostics.{h,cpp}` — mutation/bind dedup + logs, DFComposite observe | 548-573, 1062-1198, 1370-1465, 1467-1526 | ~319 |
| C6 | `ShaderHooks/PreNGVanillaDumps.{h,cpp}` — dump dedup + 3 dumpers | 665-669, 685-688, 703-711, 720-769, 1864-2078 | ~296 |
| C7 | `ShaderHooks/PreNGBSLightingBind.{h,cpp}` — vanilla alias, vanilla proof bind, LLF consumer bind | 671-677, 689-702, 771-786, 1528-1702, 1709-1862 | ~372 |
| C8 | `ShaderHooks/PreNGDescriptorBind.{h,cpp}` — DFComposite/DFLight/generic/candidate binds | 657-663, 679-684, 713-718, 1200-1293, 2080-2428 | ~455 |
| C9 | `ShaderHooks/PreNGShaderLookupHook.{h,cpp}` — the detour struct + path validation | 2430-2460, (delete 2462-2501), 2503-2976 | ~510 |
| C10 | `ShaderHooks/PostNGShaderLookupHook.{h,cpp}` — whole `#if !defined(FALLOUT_PRE_NG)` block | 3014-3214 | 201 |
| C11 | `BSShaderHooks.cpp` (remains) — queue, ReloadShaders thunk, replacement, Install/OnFrame | 1-29, 88-94, 3216-3508 | ~330 |

Deletions: #117 `TryBindPreNGDeferredLightingPixelShader` (2462-2501) is unreachable — remove rather than move.

## 3. Shared mutable state

**No anonymous namespace exists.** All globals below have *external linkage* in `namespace CommunityShaders`. Splitting into TUs without wrapping them in an anonymous namespace or a dedicated sub-namespace risks name collisions with other Core TUs; the safest mechanical move is a per-cluster `namespace CommunityShaders::PreNGHooks { namespace { ... } }` or exposing accessor functions.

Per-cluster ownership:

- **C6 (dumps)** — 685 `s_preNGBSLightingVanillaDumpLock`, 686 `…DumpKeys`, 687 `…DumpAttempts`, 688 `…DumpLimitLogged`; 703-706 DFLight dump lock/keys/attempts/limit; 707-710 DFComposite dump lock/keys/attempts/limit.
- **C7 (BSLighting bind)** — 689 `s_preNGBSLightingVanillaBindLock`, 690 `…BindAliases`, 691 `…BindAttempts`, 692 `…BoundCount`, 693 `…BindProofCompleteLogged`, 694 `…PendingProbeNextFrame`, 695-697 LLF-consumer attempts/bound/proof, 702 `s_preNGBSLightingLLFConsumerResourceBoundFrame`.
- **C8 (descriptor bind)** — 679-680 candidate 920/922 state, 681 candidate lock, 682-684 attempts/bound/limit-logged, 711 `s_preNGDFCompositeDescriptorObservations` (written in C5's tracer — see coupling).
- **C4 (lookup diagnostics)** — 832-835 diagnostic/first-seen locks + vectors, 836-840 five call counters, 841-843 three completion flags.
- **C5 (descriptor diagnostics)** — 1084-1085 mutation lock/vector, 1167-1168 bind lock/vector.
- **C10 (PostNG)** — 3030-3031 attempt/bound counters.
- **C11 (core)** — 3219 `static std::vector<PendingReplace> s_pending` (already internal linkage; **not thread-safe**, mutated from the ReloadShaders thunk and read from OnFrame).

Cross-cluster coupling risks:

1. `s_preNGDFCompositeDescriptorObservations` (711) is declared with the C8 candidate state but only used by `TracePreNGDFCompositeDescriptor` (C5, line 1401). Move it to C5.
2. C4's counters (836-840) and completion flags (841-843) are read by the detour thunk (C9) via `ShouldBypassPreNGShaderLookupHeavyDiagnostics` / `MaybeCompletePreNGShaderLookupHeavyDiagnostics` — keep those two as the only public entry points.
3. `LogPreNGDescriptorBind` (C5, 1467) is called from C8's four bind functions and needs a forward declaration today (1187-1198); after the split it becomes a normal header include, and the forward declaration block should be deleted.
4. `globals::features::lightLimitFix` is touched from C5, C7, C8, C9, C10 — the widest coupling in the file (Notify*/Has*/Bind*/Trace* on LightLimitFix). Do not try to abstract it during the split.
5. `ShaderCache::GetSingleton()` is used by C4, C5, C6, C7, C8, C9, C10, C11 — an unavoidable shared dependency.
6. C1's `ReadPreNGCString` + `kPreNGMaxFxpFilenameLength` are used by nearly every cluster; C0+C1 must be extracted first.

## 4. Anonymous-namespace helpers and detour trampolines

- Anonymous-namespace helpers: **none** (the file uses `static` only for the constants at 44-88, `s_pending` 3219, `DrainPending` 3221, `CompileReplacementPS` 3300 and the 93-94 forward decl).
- There is **no** `static inline REL::Relocation<decltype(thunk)> func;` idiom in this file. Trampolines are raw function pointers assigned from `Detours::X64::DetourFunction`:

| Trampoline | Decl line | Assigned at | Thunk | Cluster |
|---|---|---|---|---|
| `PreNGBSShaderLookup::func` | 2975 | 3425-3428 | 2505-2973 | C9 (assignment stays in C11 Install) |
| `PostNGShaderLookup::func` | 3212 | 3485-3488 | 3161-3210 | C10 (assignment stays in C11 Install) |
| `BSShader_ReloadShaders::func` | 3259 | 3375-3378 | 3244-3258 | C11 |

Because `Install()` (C11) writes all three `func` pointers, the detour structs must be exposed by their cluster headers (or provide `InstallX()` functions taking the resolved address).

## 5. Build-configuration conditional regions (exact ranges)

| Directive | Lines | Contents |
|---|---|---|
| `#if defined(FALLOUT_PRE_NG)` … `#endif` | 42 … 87 | PreNG constant + env-name block (#2) |
| `#if defined(FALLOUT_PRE_NG)` … `#endif` | 95 … 3013 | The entire PreNG implementation (#5 … #119) |
| `#if !defined(FALLOUT_PRE_NG)` … `#endif` | 3014 … 3214 | PostNG/AE block (#120 … #126) |
| `#if defined(FALLOUT_PRE_NG)` / `#else` / `#endif` | 3264 / 3272 / 3292 | inside `ReplacePixelShaders`: PreNG held vs PostNG replace loop |
| `#if defined(FALLOUT_PRE_NG)` / `#else` / `#endif` | 3339 / 3348 / 3352 | inside `ModifyShaderLookup`: PreNG normalize vs no-op |
| `#if defined(FALLOUT_PRE_NG)` / `#else` / `#endif` | 3361 / 3364 / 3368 | inside `Install`: PreNG vtable address vs `REL::Relocation` |
| `#if defined(FALLOUT_PRE_NG)` / `#else` / `#endif` | 3383 / 3481 / 3501 | inside `Install`: PreNG lookup detour + startup log vs PostNG detour |

No `FALLOUT_POST_NG` or `FALLOUT_POST_AE` macro is used in this file — the PostNG path is expressed purely as `!defined(FALLOUT_PRE_NG)`.

## 6. Raw game-address sites, grouped by hook

**A. `PreNGBSShaderLookup` detour + its bind helpers (C8/C9)** — every one goes through `F4Runtime::PreNG::<X>.address()`:
- `CURRENT_VERTEX_SHADER_ENTRY`: 1209, 1320, 1382, 1442, 1624, 1791, 2231, 2317, 2346, 2470
- `CURRENT_PIXEL_SHADER_ENTRY`: 1210, 1252, 1323, 1385, 1443, 1627, 1666, 1801, 1906, 1979, 2049, 2240, 2318, 2400, 2474, 2739, 2843
- `CURRENT_HULL_SHADER_ENTRY`: 1211, 1321, 1383, 1625, 1792, 2232, 2279, 2347, 2471
- `CURRENT_DOMAIN_SHADER_ENTRY`: 1212, 1322, 1384, 1626, 1793, 2233, 2280, 2348, 2472
- `BIND_SHADERS`: 1251, 1665, 1800, 2239, 2311, 2399, 2434, 2473
- `RENDERER_STATE` (passed as arg 1 of the bind helper): 1266, 1679, 1814, 2254, 2333, 2414, 2487
- `reinterpret_cast<PreNGBindShadersFn>(bindAddr)` indirect calls: 1265-1266, 1678-1679, 1813-1814, 2253-2254, 2332-2333, 2413-2414, 2486-2487
- Raw entry→object casts: 1215 and 2350 `reinterpret_cast<RE::BSGraphics::VertexShader*>(vertexEntry)`; 1548 `reinterpret_cast<ID3D11PixelShader*>(a_vanillaPixelD3D)`; 2465 `reinterpret_cast<ID3D11PixelShader*>(pixelShader->shader)`

**B. `ValidatePreNGShaderPath` (C9)**
- 2432 `BS_LIGHTING_SHADER_SETUP_TECHNIQUE.address()`, 2433 `BS_SHADER_LOOKUP.address()`, 2434 `BIND_SHADERS.address()`
- **vtable offset**: 2435 and 2441 `a_vtableAddr + (0x02 * sizeof(std::uintptr_t))` — vfunc index 0x02

**C. `LogPreNGShaderLookupDetourPatch` (C9)**
- 2982-2985 raw byte/rel32 read at the lookup address, 2989-2991 E9 rel32 branch-target arithmetic, 2994-2995 `reinterpret_cast<std::uintptr_t>` of thunk/func

**D. `PostNGShaderLookup` detour + consumer bind (C10)**
- 3054 `PostNG::SHADER_ENTRY_D3D_OBJECT.address(a_entry)` (**offset applied to an instance pointer**)
- 3106, 3131, 3200 `PostNG::CURRENT_PIXEL_SHADER_ENTRY.address()`
- 3120 `CURRENT_VERTEX_SHADER_ENTRY`, 3121 `CURRENT_HULL_SHADER_ENTRY`, 3122 `CURRENT_DOMAIN_SHADER_ENTRY`
- 3130 `PostNG::BIND_SHADERS.address()`, 3145 `PostNG::RENDERER_STATE.address()`
- 3143-3145 `reinterpret_cast<PostNGBindShadersFn>` indirect call

**E. `BSShaderHooks::Install` (C11)**
- 3359 `RE::FO4Runtime::ModuleBase()`
- 3362 `PreNG::BS_LIGHTING_SHADER_VTABLE.address()` (PreNG)
- 3365-3367 `REL::Relocation<std::uintptr_t>(RE::VTABLE::BSLightingShader[0])` (**the only `REL::Relocation` in the file**; PostNG only)
- 3369 `reinterpret_cast<std::uintptr_t*>(vtableAddr)`; **3370 `vtable[0x0B]` — ReloadShaders vfunc index 0x0B**
- 3375-3378 `Detours::X64::DetourFunction` for ReloadShaders
- 3424-3428 `PreNG::BS_SHADER_LOOKUP.address()` + `DetourFunction`
- 3484-3488 `PostNG::BS_SHADER_LOOKUP.address()` + `DetourFunction`

There is **no literal `REL::ID(...)`** anywhere in this file; all PreNG/PostNG addresses come from `RE::FO4Runtime::{PreNG,PostNG}` relocation values.

## 7. Function-local `static` variables

| Line | Variable | Enclosing function | Note |
|---|---|---|---|
| 161 | `static const std::uint32_t budget` | `GetPreNGDFLightFullShadowedCandidateBindBudget` | lambda-initialized, logs once |
| 199, 205, 211, 217, 223, 229, 235, 241, 247, 253, 259, 265, 271, 277, 287, 293, 299, 305, 311, 317, 323, 344, 350, 356 | `static const bool enabled` | the 24 simple `Should*` gates | env read cached for process lifetime |
| 329 | `static const bool enabled` | `ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer` | lambda, warns once |
| 554 | `static std::atomic_uint32_t heldCount` | `LogPreNGDFLightFullContractDescriptorBindHeld` | log rate limiter |
| 1274 | `static std::atomic_bool loggedPersistentResourceHold` | `TryBindPreNGDFCompositeDescriptorPixelShader` | one-shot warn |
| 2490 | `static std::atomic_uint32_t boundCount` | `TryBindPreNGDeferredLightingPixelShader` | dead with its function |
| 2734 | `static std::atomic_bool bsLightingResourceProofComplete` | `PreNGBSShaderLookup::thunk` | one-shot proof latch |
| 2735 | `static std::atomic_bool bsLightingResourceProofCompleteLogged` | `PreNGBSShaderLookup::thunk` | one-shot log latch |
| 2750 | `static std::atomic_uint32_t bsLightingResourceAuditCount` | `PreNGBSShaderLookup::thunk` | audit rate limiter |
| 2768 | `static std::atomic_uint32_t bsLightingResourcePendingCount` | `PreNGBSShaderLookup::thunk` | pending rate limiter |
| 2781 | `static std::atomic_bool bsLightingVanillaBindProofComplete` | `PreNGBSShaderLookup::thunk` | one-shot proof latch |
| 2837 | `static std::atomic_bool dfCompositeResourceProofComplete` | `PreNGBSShaderLookup::thunk` | one-shot proof latch |
| 2838 | `static std::atomic_bool dfCompositeResourceProofCompleteLogged` | `PreNGBSShaderLookup::thunk` | one-shot log latch |
| 2859 | `static std::atomic_uint32_t dfCompositeResourceAuditCount` | `PreNGBSShaderLookup::thunk` | audit rate limiter |
| 3266 | `static bool loggedPreNGHold` | `ReplacePixelShaders` | non-atomic one-shot log (pre-existing race) |

The eight latches at 2734-2859 live **inside** `PreNGBSShaderLookup::thunk` and are load-bearing for behaviour (they stop repeated proof binds). They must travel with the thunk into C9 unchanged.

## 8. Recommended extraction order

1. **C0 `PreNGShaderHookConstants.h`** — pure header move of 42-88. Zero behavioural risk; unblocks everything else.
2. **C1 `PreNGRuntime`** — leaf helpers, no state. `ReadPreNGCString`, `IsPreNGPowerOfTwo`, memory guards, fxp-name helpers.
3. **C2 `PreNGSwitches`** — leaf; only depends on C0. All 25 cached gates in one TU keeps the `static const bool` semantics intact.
4. **C3 `PreNGDescriptorPredicates`** — depends on C0+C1+C2 only.
5. **C6 `PreNGVanillaDumps`** — self-contained state + three near-identical dumpers; only entry points are the three `DumpPreNG*VanillaShader` calls from the thunk.
6. **C5 `PreNGDescriptorDiagnostics`** — logging only; delete the 1187-1198 forward declaration once `LogPreNGDescriptorBind` lives in a header.
7. **C4 `PreNGLookupDiagnostics`** — logging + counters; expose only `TracePreNGShaderLookupEntry`, `TracePreNGShaderLookup`, `ShouldBypassPreNGShaderLookupHeavyDiagnostics`, `MaybeCompletePreNGShaderLookupHeavyDiagnostics`, `LogPreNGShaderLookupDetourPatch`.
8. **C7 `PreNGBSLightingBind`** and **C8 `PreNGDescriptorBind`** — real GPU-state binds; move only after all their logging dependencies are stable.
9. **C10 `PostNGShaderLookupHook`** — independent of every PreNG cluster; can be done at any point, but it is the cheapest smoke test of the new directory on the non-PreNG build.
10. **C9 `PreNGShaderLookupHook`** — last. The 474-line thunk is the highest-risk artifact in the file.
11. Delete dead `TryBindPreNGDeferredLightingPixelShader` (2462-2501) as a **separate commit** from any move.

### Cannot move without changing behaviour

- **The eight function-local latches inside `PreNGBSShaderLookup::thunk` (2734-2859).** Hoisting them to file scope changes nothing semantically today, but splitting them across TUs would; keep them inline in the thunk.
- **The gate-evaluation order and short-circuit structure at 2515-2610.** The two early-out blocks are the hot-path performance guard (documented perf regression in project memory); reordering or extracting them into helper functions that evaluate all predicates eagerly will re-introduce per-draw cost.
- **`static const bool enabled` caching in the `Should*` gates.** Converting them to plain functions or `constexpr` reads would make env switches re-read per draw.
- **The Install-time ordering at 3357-3502**: `ValidatePreNGShaderPath` must run before the lookup detour, and `LogPreNGShaderLookupDetourPatch` after — it reads the patched bytes.
- **`vtable[0x0B]` (3370) and vfunc index `0x02` (2435/2441)** are hard-coded engine ABI; do not "clean up" into named constants across the `#if` boundary without checking both build configs.
- **`ModifyShaderLookup` and `ReplacePixelShaders`** are declared in the public `BSShaderHooks.h`; they must keep external linkage in `namespace CommunityShaders` and should stay in `BSShaderHooks.cpp`.
- **`s_pending` + `DrainPending` + `BSShader_ReloadShaders::thunk`** form the deferred queue; the thunk pushes and `OnFrame` drains with no lock. Splitting them into different TUs makes the (already unsynchronised) access harder to audit — keep them together in C11.
