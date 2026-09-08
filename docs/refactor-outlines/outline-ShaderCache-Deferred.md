# Refactor outline — ShaderCache.cpp / Deferred.cpp (worktree fo4CS-cs-refactor)

Line ranges are inclusive; END = line holding the closing brace at the definition's indent.
Note: neither file contains a worker thread, condition variable, or async compile queue. All
concurrency is lock/atomic-based on caller threads (render thread + D3D11 draw hook callers).

---

# 1. src/Core/ShaderCache.cpp — 2013 lines, 77 functions

## 1.1 Function table

### Anonymous namespace (block spans 30–856)

| # | Name | Lines | ~LOC | Responsibility |
|---|---|---|---|---|
|1|`ToREVertexShader` (POST_NG)|72–75|4|reinterpret_cast ID3D11VertexShader → REX::W32|
|2|`ToREPixelShader` (POST_NG)|77–80|4|reinterpret_cast ID3D11PixelShader → REX::W32|
|3|`ToREVertexShader` (else)|82–85|4|identity passthrough|
|4|`ToREPixelShader` (else)|87–90|4|identity passthrough|
|5|`ReadDescriptorEnvironmentSwitch`|93–96|4|bool env switch via DebugSwitches|
|6|`ReadDescriptorEnvironmentUInt`|98–105|8|clamped uint env switch|
|7|`ReadDescriptorCompileSwitch`|107–110|4|master PreNG descriptor-compile switch|
|8|`ShouldEnablePreNGDFLightFullShadowedDescriptorConsumer`|113–126|14|gated+warned unsafe DFLight shadowed consumer|
|9|`ShouldEnablePreNGDFLightFullContractVisibleLLF`|128–132|5|DFLight full-contract visible LLF switch|
|10|`ShouldEnablePreNGDFLightForwardVisibleLLF`|134–138|5|DFLight forward LLF bind switch|
|11|`GetPreNGDFLightFullContractVisibleMaxLights`|140–148|9|clamped max-lights tunable|
|12|`GetPreNGDFLightFullContractVisibleStrictMaxLights`|150–158|9|clamped strict max-lights tunable|
|13|`GetPreNGDFLightFullContractVisibleClusterMaxLights`|160–168|9|clamped cluster max-lights tunable|
|14|`IsPreNGDFCompositeVisibleLLFDescriptor`|170–174|5|descriptor 0x88/0x10088 test|
|15|`ShouldEnablePreNGDFCompositeVisibleLLF`|176–187|12|gated+warned DFComposite visible LLF|
|16|`GetPreNGDFCompositeVisibleLLFScale1024`|189–197|9|clamped LLF scale tunable|
|17|`GetPreNGDFCompositeVisibleLLFMaxLights`|199–207|9|clamped LLF max-lights tunable|
|18|`ToLowerAscii`|210–216|7|ASCII lowercase copy|
|19|`StartsWith`|218–222|5|prefix test|
|20|`IsPreNGDFLightFxpName`|224–245|22|normalize+match DFLighting fxp stem|
|21|`IsPreNGBSLightingFxpName`|247–268|22|normalize+match BSLighting fxp stem|
|22|`IsPreNGDFCompositeFxpName`|270–291|22|normalize+match DFComposite fxp stem|
|23|`IsPreNGDFLightFullShadowedDescriptorShader`|293–311|19|classify DFLight full-shadowed PS|
|24|`IsPreNGBSLightingContractPixelDescriptor`|313–316|4|contract descriptor test|
|25|`IsPreNGBSLightingContractDescriptorShader`|318–336|19|classify BSLighting contract PS|
|26|`ShouldCompilePreNGBSLightingContractShader`|338–359|22|env-gated contract compile|
|27|`ShouldCompilePreNGBSLightingConsumerShader`|361–382|22|env-gated consumer compile|
|28|`ShouldBindPreNGBSLightingLLFVisibleConsumerShader`|384–405|22|env-gated visible LLF bind|
|29|`IsPreNGDFLightFullContractDescriptorShader`|407–425|19|classify DFLight full-contract PS|
|30|`IsPreNGDFCompositeContractDescriptorShader`|427–445|19|classify DFComposite contract PS|
|31|`ShouldCompilePreNGDFCompositeDescriptorShader`|447–468|22|env-gated DFComposite compile|
|32|`ShouldEnablePreNGDFCompositeFogSafeBindShader`|470–478|9|fog-safe-bind switch|
|33|`ShouldUsePreNGDFCompositeVanilla40Shader`|480–501|22|safe-bind route for descriptor 0x40|
|34|`ShouldUsePreNGDFCompositeVanilla88Shader`|503–523|21|safe-bind route for 0x88|
|35|`ShouldUsePreNGDFCompositeVanilla10040Shader`|525–546|22|safe-bind route for 0x10040|
|36|`ShouldUsePreNGDFCompositeVanilla10088Shader`|548–568|21|safe-bind route for 0x10088|
|37|`ShouldUsePreNGDFCompositeVanillaSafeBindShader`|570–596|27|OR of the four safe-bind routes|
|38|`IsPreNGDFLightLLFConsumerDescriptorShader`|598–620|23|classify DFLight LLF consumer PS|
|39|`CanActivelyCompilePreNGLightingDescriptorShader`|622–631|10|shaderType whitelist for active compile|
|40|`ShouldCompilePostNGBSLightingConsumerShader`|633–647|15|PostNG/AE consumer compile gate|
|41|`CanCompileDescriptorShader`|649–690|42|top-level compile permission (OR of all gates)|
|42|`BuildFeatureDefineList`|692–705|14|comma-join macro names for logging|
|—|`struct DescriptorDefineSet`|707–711|5|(type) macro storage + D3D_SHADER_MACRO array|
|43|`BuildDescriptorDefineSet`|713–850|138|build full D3D_SHADER_MACRO define set per key|
|44|`GetDescriptorTarget`|852–855|4|"vs_5_0"/"ps_5_0"|

### `CommunityShaders::ShaderCache` members

| # | Name | Lines | ~LOC | Responsibility |
|---|---|---|---|---|
|45|`GetSingleton`|858–862|5|Meyers singleton|
|46|`NormalizeFxpFilename(string_view)`|864–878|15|lowercase + backslash→slash|
|47|`NormalizeFxpFilename(const char*)`|880–883|4|null-safe overload|
|48|`GetDescriptorCacheState`|885–892|8|state→log string|
|49|`MakeDescriptorShaderKey`|894–905|12|build key from BSShader+descriptor|
|50|`ShouldCompileDescriptorShaders`|907–915|9|PreNG-only master compile flag|
|51|`ResolveDescriptorShaderSource`|917–1061|145|pick .hlsl source path for a descriptor key|
|52|`ObserveDescriptorShader`|1063–1093|31|record vanilla descriptor lookup hit|
|53|`GetDescriptorShaderState(BSShader)`|1095–1101|7|overload forwarding|
|54|`GetDescriptorShaderState(type,desc,fxp)`|1103–1116|14|locked map lookup|
|55|`LogDescriptorBridgeHeld`|1118–1150|33|dedup "held" log line|
|56|`LogDescriptorCompileEvent`|1152–1210|59|dedup compile-event log line|
|57|`GetVertexShader`|1212–1239|28|cache lookup → compile VS|
|58|`GetPixelShader`|1241–1268|28|cache lookup → compile PS|
|59|`MakeAndAddVertexShader`|1270–1364|95|compile+CreateVertexShader+own entry|
|60|`MakeAndAddPixelShader`|1366–1463|98|compile+CreatePixelShader+own entry+observe|
|61|`ObserveShader`|1465–1491|27|dedup by bytecode hash, trace/dump|
|62|`GetAsmHashForBytecode`|1493–1501|9|asmHash accessor|
|63|`GetMetadataForBytecode`|1503–1535|33|disassemble+cache metadata|
|64|`ObserveD3DShaderObject`|1537–1545|9|map d3d ptr→metadata|
|65|`ObserveD3DShaderObjectBytecode`|1547–1563|17|map d3d ptr→metadata+bytecode copy|
|66|`GetMetadataForD3DShaderObject`|1565–1577|13|locked lookup|
|67|`DumpObservedD3DShaderObject`|1579–1662|84|write .bin/.asm/.txt/.capture for one object|
|68|`TraceShaderCreation`|1664–1697|34|stack-backtrace pipeline trace file|
|69|`HashShaderBytecode`|1700–1710|11|FNV-1a 64 over bytecode|
|70|`DumpShader`|1712–1748|37|disassemble+write dump set|
|71|`BuildMetadata`|1750–1860|111|regex-parse disassembly into ShaderMetadata|
|72|`WriteMetadataFiles`|1862–1939|78|write shaderDump .txt block|
|73|`GetDumpDirectory`|1941–1944|4|Data/F4SE/.../ShaderDump/<runtime>|
|74|`GetStageName`|1946–1964|19|stage→"PS"/"VS"/...|
|75|`GetStageTypeName`|1966–1984|19|stage→"ps"/"vs"/...|
|76|`HashText`|1986–1994|9|FNV-1a 32|
|77|`GetResourceDimension`|1996–2012|17|dcl_resource_* → dimension code|

## 1.2 Proposed clusters (target `src/Core/Shaders/`)

| TU | Source ranges | ~LOC | Contents |
|---|---|---|---|
|`ShaderSwitches.cpp/.h`|33–52, 71–207|~200|env-switch constants + `ToRE*` casts + all `ShouldEnable*`/`Get*Tunable` readers (fns 1–17)|
|`ShaderDescriptorGates.cpp/.h`|53–69, 210–690|~500|fxp-name matchers + descriptor classifiers + `CanCompileDescriptorShader` (fns 18–41)|
|`ShaderDescriptorDefines.cpp/.h`|692–855|~165|`DescriptorDefineSet`, `BuildDescriptorDefineSet`, `BuildFeatureDefineList`, `GetDescriptorTarget` (42–44)|
|`ShaderCache.cpp` (keep)|858–1116, 1212–1463|~510|singleton, key/normalize, source resolution, observe, Get/MakeAndAdd (45–54, 57–60)|
|`ShaderCacheLog.cpp`|1118–1210|~95|`LogDescriptorBridgeHeld`, `LogDescriptorCompileEvent` (55–56)|
|`ShaderObservation.cpp`|1465–1710|~250|bytecode observation, D3D-object maps, targeted dump, pipeline trace (61–69)|
|`ShaderMetadata.cpp`|1712–2012|~300|`DumpShader`, `BuildMetadata`, `WriteMetadataFiles`, path/stage/hash helpers (70–77)|

Private internal header `Shaders/ShaderCacheInternal.h` must publish what the anonymous namespace
currently hides: gate predicates, define-set builder, `ToRE*`. Those become `namespace detail`,
not `static` — an intentional linkage change (see §1.8).

## 1.3 Shared mutable state & coupling

- `observedLock` (`std::mutex`) guards `observedHashes`, `bytecodeToAsmHash`, `bytecodeToMetadata`,
  `d3dShaderObjectToMetadata`, `d3dShaderObjectToBytecode`, `dumpedD3DShaderObjectKeys`,
  `traceStackHashes`. Owned by **ShaderObservation** but also taken from `DumpShader` (1730)
  in **ShaderMetadata** → the two TUs share one mutex. Keep both in one TU, or give `DumpShader`
  a small locked setter exported by ShaderObservation.
- `descriptorLock` (`mutable std::mutex`) guards `descriptorShaders`, `descriptorVertexShaders`,
  `descriptorPixelShaders`, `descriptorHeldLogs`, `descriptorCompileLogs`. Taken in
  **ShaderCache.cpp** (1075, 1110, 1217, 1246, 1287, 1349, 1383, 1447) and in
  **ShaderCacheLog.cpp** (1135, 1175). Same-mutex cross-TU coupling; acceptable (member mutex).
- `m_hookVerifyCounter` (`std::atomic<uint32_t>`) — incremented at 1471 (ShaderObservation),
  read by Deferred's `LogHookFire`. Cross-file coupling.
- `dumpAllShaders` / `tracePipeline` — plain `bool`, written from config, read at 1483/1486
  without synchronization. Pre-existing benign race; do not "fix" during extraction.
- No mutex is held across a `ShaderCompiler` call — MakeAndAdd* deliberately compiles outside the
  lock and tolerates a losing racer (`owned-entry-created-by-peer`, 1360/1459). Preserve that shape.

## 1.4 Anonymous-namespace helpers / statics → cluster

All 44 anon-ns functions listed above; constants at 33–69 (`kPreNG*Env`, `kPreNG*Source`,
`kPreNGBSLightingShaderType`, `kPreNGDFLightingShaderType`, `kPreNGDFCompositeShaderType`) go with
**ShaderSwitches** (env names, 37–56) and **ShaderDescriptorGates** (source paths, 57–69).
`namespace F4Runtime = RE::FO4Runtime` alias at line 32 must be repeated per TU.

## 1.5 `#if defined(FALLOUT_*)` regions

`FALLOUT_POST_AE`: 9–13 (include).
`FALLOUT_POST_NG` / `#else`: 71–91.
`FALLOUT_PRE_NG` (block): 34–52; 112–208.
`!FALLOUT_PRE_NG`: 53–56; 919–928.
`FALLOUT_PRE_NG` / `#else` inside functions: 226–244, 249–267, 272–290, 299–310, 324–335,
344–358, 367–381, 390–404, 413–424, 433–444, 453–467, 472–477, 486–500, 509–522, 531–545,
554–567, 604–619, 637–646, 738–842, 909–914.

## 1.6 Raw game-address / game-pointer sites

- 72–80 `reinterpret_cast` D3D11 → `REX::W32::ID3D11*Shader` (POST_NG ABI pun).
- `RE::FO4Runtime::PreNG` descriptor constants & predicates: 172–173, 303, 315, 417, 437, 493,
  515, 538, 560, 608–612, 657, 663, 741, 769, 786, 788, 796, 804, 812, 963.
- Game struct field reads on `RE::BSShader`: 901, 903, 1080, 1100, 1129, 1144, 1165, 1187, 1201.
- Writes into game structs `RE::BSGraphics::VertexShader/PixelShader`: 1341–1344, 1437–1438.
- 1440 `reinterpret_cast<std::uintptr_t>(owned->d3dShader.get())`.
- 1667 `RtlCaptureStackBackTrace`; 1683 `GetModuleHandleA("Fallout4.exe")`;
  1693 `Fallout4.exe+0x{offset}` module-base arithmetic.
- 1297 / 1393 `Runtime::GetSingleton()->GetDevice()` (game-owned ID3D11Device).

## 1.7 Function-local `static` variables

115, 130, 136, 142, 152, 162, 178, 191, 201 (anon-ns gate caches);
345, 368, 391, 454, 473, 487, 510, 532, 555 (per-predicate `static const bool enabled`);
860 (`static ShaderCache singleton`); 910 (`ShouldCompileDescriptorShaders`);
1756 `regexFlags` + 1757–1765 eight `static const std::regex` objects in `BuildMetadata`.

## 1.8 Extraction order & immovables

1. `ShaderMetadata.cpp` (1712–2012) — leaf, only touches `observedLock` at 1730. Lowest risk.
2. `ShaderCacheLog.cpp` (1118–1210) — two functions, one member mutex.
3. `ShaderDescriptorDefines.cpp` (692–855) — pure, but depends on the gate predicates: extract
   after step 4 or forward-declare them.
4. `ShaderSwitches.cpp` + `ShaderDescriptorGates.cpp` (33–690) — biggest win, highest churn.
5. `ShaderObservation.cpp` (1465–1710) — last, because of the `observedLock` sharing in step 1.

Cannot move / must move exactly once:
- Every `static const bool enabled` cache. Duplicating a predicate into two TUs creates two
  independent one-shot initializers; 115–124 and 178–185 also **log a warning on first call**,
  so duplication doubles the warning and can flip behavior if the env var changes mid-process.
- `ShaderCache::GetSingleton` (858–862) — one definition only; anything else changes object identity.
- The eight `static const std::regex` at 1757–1765 must travel with `BuildMetadata`; splitting them
  into a header would re-construct them per TU (measurable cost, no behavior change).
- Anon-ns → `detail` namespace is a deliberate internal→external linkage change. Guard it with a
  `namespace CommunityShaders::detail` and keep the header private to `src/Core/Shaders/`.

---

# 2. src/Core/Deferred.cpp — 1198 lines, 51 functions

## 2.1 Function table

### Anonymous namespace #1 (24–56) and #2 (69–429)

| # | Name | Lines | ~LOC | Responsibility |
|---|---|---|---|---|
|1|`HasAddressLibrary`|30–55|26|probe for `version-*.bin` (USVFS-safe FindFirstFileW + fallback)|
|2|`ToGBufferIndex`|85–88|4|enum→array index|
|3|`GetGBufferBinding`|90–94|5|bounds-checked binding lookup|
|4|`IsDeferredTraceEnabled`|128–132|5|`FO4CS_TRACE_DEFERRED` switch|
|5|`IsGBufferDumpEnabled`|134–138|5|`FO4CS_DUMP_GBUFFER` switch|
|6|`TraceDeferred` (template)|140–163|24|append formatted line to deferred_trace.txt|
|7|`DescribeRTV`|165–197|33|format RTV/resource/format/size|
|8|`DescribeDSV`|199–212|14|format DSV|
|9|`TraceOMState`|214–243|30|dump OM render targets + blend|
|10|`TraceRestoreCheck`|245–277|33|verify OM state restored to saved state|
|11|`DumpGBufferSnapshot`|279–348|70|one-shot staging copy + .bin/.txt of each GBuffer|
|12|`LightingDrawGuard::LightingDrawGuard`|355–358|4|RAII begin MRT scope|
|13|`LightingDrawGuard::~LightingDrawGuard`|360–365|6|RAII end MRT scope|
|14|`DeferredWorldEpochGuard::DeferredWorldEpochGuard`|375–377|3|store Deferred*|
|15|`DeferredWorldEpochGuard::~DeferredWorldEpochGuard`|379–382|4|call `EndDeferred()` on scope exit|
|16|`DrawIndexedHook`|388–392|5|vtable slot 12 detour|
|17|`DrawHook`|394–398|5|slot 13|
|18|`DrawIndexedInstancedHook`|400–404|5|slot 20|
|19|`DrawInstancedHook`|406–410|5|slot 21|
|20|`DrawAutoHook`|412–416|5|slot 38|
|21|`DrawIndexedInstancedIndirectHook`|418–422|5|slot 39|
|22|`DrawInstancedIndirectHook`|424–428|5|slot 40|

### `Deferred` members, file-scope helpers, hooks

| # | Name | Lines | ~LOC | Responsibility |
|---|---|---|---|---|
|23|`GetGBufferTargetBindings`|431–434|4|accessor for `kGBufferTargets`|
|24|`GetDeferredRenderTargetBindings`|436–439|4|accessor for `kDeferredRenderTargets`|
|25|`BuildShaderLookupDescriptorState`|441–474|34|decide deferred descriptor rewrite for a draw|
|26|`GetGBufferDesc`|476–480|5|cached TEXTURE2D_DESC|
|27|`GetGBufferTexture`|482–491|10|renderer RT → ID3D11Texture2D|
|28|`GetGBufferSRV`|493–502|10|renderer RT → SRV|
|29|`GetGBufferRTV`|504–513|10|renderer RT → RTV|
|30|`SetupResources`|515–591|77|create samplers, install draw hooks, validate GBuffer|
|31|`InstallDrawHooks`|593–628|36|detour 7 ID3D11DeviceContext vtable slots|
|32|`RegisterLightingPixelShader`|630–651|22|retain a PS as an MRT-eligible lighting shader|
|33|`IsRegisteredLightingPixelShader`|653–661|9|shared-lock membership test|
|34|`BeginLightingDraw`|663–757|95|save OM state, bind GBuffer MRT + MRT blend|
|35|`EndLightingDraw`|759–791|33|restore saved OM state|
|36|`ReflectionsPrepasses`|793–803|11|unbind RTs, dispatch feature ReflectionsPrepass|
|37|`EarlyPrepasses`|805–815|11|unbind RTs, dispatch feature EarlyPrepass|
|38|`PrepassPasses`|817–833|17|unbind RTs, dispatch feature Prepass|
|39|`StartDeferred`|835–857|23|open the world epoch, run prepasses|
|40|`EndDeferred`|859–876|18|run deferred passes, close epoch, dump|
|41|`DeferredPasses`|878–895|18|composite pass — currently a documented stub|
|42|`GetOrCreateMRTBlendState`|897–938|42|clone blend state with IndependentBlendEnable|
|43|`ClearShaderCache`|940–946|7|drop registered shaders + blend cache|
|44|`LogHookFire`|951–961|11|append hook-fire line (global linkage, not anon-ns)|
|45|`detour_thunk_at<T>`|970–973|4|raw-address detour helper|
|46|`Deferred::Hooks::Install`|975–1144|170|resolve REL IDs from .bin, install pipeline detours|
|47|`Main_RenderShadowMaps::thunk`|1146–1155|10|orig → log → `EarlyPrepasses()`|
|48|`Main_RenderWorld::thunk`|1157–1165|9|log → orig|
|49|`Main_RenderWorld_Start::thunk`|1167–1178|12|`StartDeferred()` + epoch guard → orig|
|50|`Main_RenderWorld_BlendedDecals::thunk`|1180–1188|9|orig → log|
|51|`Renderer_Begin::thunk`|1190–1198|9|log → orig|

Nested lambdas inside `Hooks::Install`: `GetRELOffset` 980–1021, `resolvePreNGRelocation` 1095–1106.

## 2.2 Proposed clusters (`src/Core/Deferred/`, keep `Deferred.cpp` as the façade)

| TU | Source ranges | ~LOC | Contents |
|---|---|---|---|
|`DeferredTrace.cpp/.h`|126–348|~225|trace lock, switches, `TraceDeferred`, `Describe*`, `TraceOMState`, `TraceRestoreCheck`, `DumpGBufferSnapshot` (4–11)|
|`DeferredDrawHooks.cpp/.h`|96–124, 350–429, 593–628, 663–791|~280|draw fn typedefs + originals, install lock/vtable, TLS `LightingDrawState`, both guards, 7 hooks, `InstallDrawHooks`, `Begin/EndLightingDraw` (12–22, 31, 34, 35)|
|`DeferredGBuffer.cpp/.h`|71–94, 431–513, 515–591, 897–938|~230|binding tables, index/lookup helpers, GBuffer accessors, `SetupResources`, `GetOrCreateMRTBlendState` (2, 3, 23, 24, 26–30, 42)|
|`Deferred.cpp` (keep)|441–474, 793–895, 940–946|~165|descriptor-state decision, prepass/deferred pass dispatch, epoch begin/end, `ClearShaderCache` (25, 36–41, 43)|
|`DeferredHooks.cpp`|24–56, 948–1198|~290|`HasAddressLibrary`, `LogHookFire`, `detour_thunk_at`, `Hooks::Install`, the 5 thunks (1, 44–51)|

`RegisterLightingPixelShader` / `IsRegisteredLightingPixelShader` (32, 33) belong with
**DeferredDrawHooks** logically but touch the `Deferred` member map; simplest is to keep them in
the façade `Deferred.cpp` (~+31 lines) and have the hook TU call through the singleton.

## 2.3 Shared mutable state & coupling

- `originalDraw*` (104–110, 7 raw fn pointers), `drawHookInstallLock` (`std::mutex`, 111),
  `installedDrawHookVTable` (112) — **DeferredDrawHooks** owns all of them. The 7 hook thunks read
  the pointers with no synchronization after install; `InstallDrawHooks` is the only writer and it
  is idempotent under the mutex. Splitting the thunks from `InstallDrawHooks` forces these to lose
  internal linkage — do not.
- `thread_local LightingDrawState lightingDrawState` (350) — per-thread OM save slot, read/written
  in `BeginLightingDraw` (692), `EndLightingDraw` (761) and observed by `TraceRestoreCheck`.
  **DeferredDrawHooks** owns it; `TraceRestoreCheck` takes it by const&, so **DeferredTrace** needs
  `LightingDrawState` in a header (move the struct 114–124 to `DeferredDrawHooks.h`).
- `deferredTraceLock` (`std::mutex`, 126) — **DeferredTrace** only.
- `Deferred::deferredPass` (`std::atomic_bool`) — written 841/869 (façade), read 453, 665.
- `Deferred::hasLightingPixelShaders` (`std::atomic_bool`) — written 644/944, read 666.
- `Deferred::lightingShaderLock` (`mutable std::shared_mutex`) — 636, 659, 844, 942.
  Note 942 `std::scoped_lock lock(lightingShaderLock, blendStateLock)` locks both at once; any
  future lock taken in another TU must respect that order.
- `Deferred::blendStateLock` (`std::mutex`) + `blendStateCache` — 901 (`GetOrCreateMRTBlendState`,
  DeferredGBuffer) and 942 (`ClearShaderCache`, façade). Cross-TU shared mutex.
- `gBufferResourcesReady` / `gBufferDescriptions` — plain members written in `SetupResources`
  (519, 572) and read from the draw hook path (459, 665) with no atomic. Pre-existing race.
- Cross-file: `LogHookFire` (953) reads `ShaderCache::GetSingleton()->GetShaderCreationCount()`.

## 2.4 Anonymous-namespace helpers / statics → cluster

Anon-ns #1 (24–56): `HasAddressLibrary` → **DeferredHooks**.
Anon-ns #2 (69–429): `kGBufferTargets` (71–76) and `kDeferredRenderTargets` (78–83) →
**DeferredGBuffer**; draw typedefs/pointers/lock (96–112) and `LightingDrawState` (114–124) →
**DeferredDrawHooks**; `deferredTraceLock` (126) and fns 4–11 → **DeferredTrace**;
`lightingDrawState` (350) and classes `LightingDrawGuard` (352–370) /
`DeferredWorldEpochGuard` (372–386) plus the 7 hooks → **DeferredDrawHooks**.
`LogHookFire` (951–961) is **not** in an anonymous namespace — it has external linkage today.

## 2.5 `#if defined(FALLOUT_*)` regions

`FALLOUT_POST_AE` / `#else`: 12–16 (include); 985–989 (module version source).
`FALLOUT_POST_NG` / `#else`: 990–994 (.bin filename format).
`FALLOUT_POST_NG` / `#else` (PreNG) / `#endif`: 1029–1142 — the entire hook-install body.

## 2.6 Raw game-address / game-pointer sites

- `fo4cs::GetRendererData()` + `reinterpret_cast` of engine handles: 285–291, 490, 501, 512,
  517, 523–524, 795–797, 807–809, 819–821, 848, 921–924.
- 600 `*reinterpret_cast<std::uintptr_t*>(a_context)` — reads the ID3D11DeviceContext vtable ptr.
- 612–618 `Detours::X64::DetourClassVTable(vtable, hook, N)` with slots 12, 13, 20, 21, 38, 39, 40.
- 972 `Detours::X64::DetourFunction(a_address, (uintptr_t)&T::thunk)`; `*(uintptr_t*)&T::func`.
- 980–1021 direct parse of `Data/F4SE/Plugins/version-*.bin` (`mapping_t{id,offset}` + lower_bound).
- 986 `REX::FModule::GetExecutingModule().GetFileVersion()`; 988 `REL::Module::get().version()`.
- 1038 `F4Hooks::DEFERRED_MAIN_RENDER_WORLD_START.id()`; 1045 `stl::detour_thunk<...>`.
- 1056 `RE::FO4Runtime::ModuleBase()`; 1059–1071 `F4Hooks::DEFERRED_PIPELINE` iteration, `base + rva`.
- 1078–1092 raw byte scan: `reinterpret_cast<const uint8_t*>(base + rva + off)`, opcode `0xE8`/`0xE9`,
  `*reinterpret_cast<const int32_t*>(ptr + 1)` relative-target decode.
- 1095–1106 `resolvePreNGRelocation` (`base + rva ± RelocationID::offset`).
- 1109–1112 / 1123–1126 `detour_thunk_at<Main_RenderWorld_Start>` / `<Main_RenderShadowMaps>`.
- Header `Deferred.h` 123, 129, 135, 141, 147: `static inline REL::Relocation<decltype(thunk)> func`.

## 2.7 Function-local `static` variables

130 `static const bool enabled` (trace switch); 136 `static const bool enabled` (dump switch);
281 `static std::atomic_bool dumped` (one-shot GBuffer dump latch);
574 `static bool loggedPending` (SetupResources warn latch);
747 `static std::atomic_bool loggedReady` (first-MRT-bind info latch);
982 `static std::vector<mapping_t> s_id2offset` (address-library cache, inside the `GetRELOffset`
lambda inside `Hooks::Install`).
Also `Deferred::GetSingleton`'s `static Deferred singleton` at `Deferred.h:29`.

## 2.8 Extraction order & immovables

1. `DeferredTrace.cpp` (126–348) — leaf; only needs `LightingDrawState` + `kGBufferTargets`
   declared in headers. Lowest risk.
2. `DeferredGBuffer.cpp` (71–94, 431–513, 515–591, 897–938) — pure accessors + setup; the only
   snag is `SetupResources` calling `InstallDrawHooks` (524) across the new boundary.
3. `DeferredHooks.cpp` (24–56, 948–1198) — self-contained once `LogHookFire` and
   `detour_thunk_at` move together.
4. `DeferredDrawHooks.cpp` last — it carries the TLS and the raw original-function pointers.

Cannot move without behavior change:
- **The 7 hook thunks, `originalDraw*`, `drawHookInstallLock`, `installedDrawHookVTable` and
  `InstallDrawHooks` must stay in one TU.** Splitting them requires giving the pointers external
  linkage, which changes ODR surface and lets a second vtable install race the readers. The
  "refuse second vtable" guard (601–610) is process-global state; duplicating it re-arms detours.
- `thread_local LightingDrawState lightingDrawState` (350) must stay in the same TU as
  `BeginLightingDraw`/`EndLightingDraw`/`LightingDrawGuard`. Exporting a `thread_local` across TUs
  switches MSVC to the dynamic TLS-on-demand path (`__dyn_tls_on_demand_init`) and adds a
  first-touch check on the hottest path in the file (once per draw call).
- `static std::vector<mapping_t> s_id2offset` (982) is scoped to the lambda inside
  `Hooks::Install`. It is loaded once per process today because `Install` runs once; if `Install`
  is ever split across TUs, the cache must be hoisted explicitly or the .bin gets re-read.
- The one-shot latches at 281, 574, 747 must each exist exactly once — duplicating them turns
  "log/dump once" into "log/dump once per TU".
- `LogHookFire` (951–961) has external linkage at namespace scope. Moving it into an anonymous
  namespace is a linkage change; keep it non-static or declare it in a private header.
- `kGBufferTargets` / `kDeferredRenderTargets` (71–83) are namespace-scope objects returned by
  reference from `GetGBufferTargetBindings`/`GetDeferredRenderTargetBindings`. Nothing currently
  reads them during static initialization, but once they live in a different TU from a caller,
  any future static-init consumer becomes a static-initialization-order bug. Prefer making them
  `constexpr`/function-local `static const` when the move happens.
- `Deferred::GetSingleton` stays in `Deferred.h:27–31`; every new TU must include that header
  rather than re-declare the singleton.
