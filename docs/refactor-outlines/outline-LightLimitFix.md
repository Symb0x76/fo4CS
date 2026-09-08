# LightLimitFix.cpp refactor outline (worktree `D:\Projects\CnCpp\fo4CS-cs-refactor\`)

Source: `src/Features/LightLimitFix.cpp` (5081 lines), `src/Features/LightLimitFix.h` (449), base `src/Core/Feature.h` (100).
Structure: includes 1-62; anonymous namespace 64-1823 (PRE_NG-only sub-region 262-1822); `LightLimitFix::` members 1825-5081.
END = line of the closing `}` at the function's own indent.

## 1. Function inventory (153 definitions)

### 1a. Anonymous-namespace / file-static free functions (all inside `namespace { }` 64-1823)

| # | Function | Range | ~L | Responsibility |
|---|---|---|---|---|
|1|`GetPreNGDFLightRendererStateBase`|219-235|17|Walk TEB->TLS slot -> renderer-state base (raw addresses)|
|2|`GetShaderPath`|239-242|4|Return `"LightLimitFix\\"` shader dir|
|3|`LogResourceFailure`|244-248|5|Log an HRESULT resource failure, return false|
|4|`IsFiniteMatrix`|250-261|12|Validate all 16 floats of a 4x4|
|5|`PreNGClusterBuildInputsMatch`|263-284|22|Compare cached cluster-build inputs vs current CB|
|6|`HashPreNGAppendBytes`|289-297|9|FNV-1a byte append|
|7|`HashPreNGBytes`|299-304|6|FNV-1a of a buffer|
|8|`MakePreNGClusterPayloadCacheState`|306-324|19|Build payload-cache key from lights+view+grid|
|9|`SamePreNGShadowSceneFastReuseKey`|337-347|11|Strict fast-reuse key equality (incl. hashes)|
|10|`SamePreNGShadowSceneFastReuseStructure`|358-367|10|Structural (hash-free) fast-reuse key equality|
|11|`ReadPreNGRaw<T>` (template)|374-379|6|Raw memcpy read of game memory|
|12|`ReadPreNGRawField<T>` (template)|381-385|5|Raw read via `RuntimeField` offset|
|13|`ReadPreNGShadowSceneBucketHash`|387-400|14|Hash one shadow-scene light bucket|
|14|`MakePreNGShadowSceneFastReuseKey`|402-427|26|Build the full fast-reuse key from node+buckets|
|15|`ReadPreNGCurrentPixelShaderEntryState`|431-434|4|Read current PS entry state from runtime|
|16|`HasPreNGConstantBufferSlot`|436-439|4|Metadata: does PS declare cb slot N|
|17|`HasPreNGTextureSlot`|441-445|5|Metadata: does PS declare texture slot N|
|18|`GetPreNGTextureSampleCount`|447-451|5|Metadata: sample count for texture slot N|
|19|`FormatPreNGShaderBufferSlots`|453-472|20|Format CB slot list for logs|
|20|`FormatPreNGShaderTextureSlots`|474-486|13|Format texture slot list for logs|
|21|`FormatPreNGShaderTextureSampleCounts`|488-507|20|Format texture sample counts for logs|
|22|`GetPreNGShaderSlotEvidence`|521-539|19|Collapse metadata into cb3/t35-t37 evidence struct|
|23|`HasPreNGFullShadowedDFLightVanillaContract`|541-550|10|Recognise vanilla full-shadowed DFLight PS|
|24|`FormatPreNGShaderMetadata`|552-563|12|One-line shader metadata for logs|
|25|`GetPreNGWorldShadowSceneNode`|567-570|4|Thin wrapper on F4Runtime shadow-scene node lookup|
|26|`DecodePreNGBSLightWrapper`|580-655|76|Decode BSLight wrapper + NiLight -> `LightData`|
|27|`ValidatePreNGPointLightCallsite`|657-685|29|Verify point-light vfunc/call-site signature|
|28|`EnvironmentSwitchSourceName`|707-716|10|Enum -> "debug-ini"/"none"|
|29|`ToEnvironmentSwitchSource`|718-722|5|DebugSwitches::Source -> local enum|
|30|`ReadEnvironmentSwitch`|724-728|5|Read a bool debug switch|
|31|`ReadEnvironmentUInt`|730-734|5|Read a uint debug switch|
|32|`GetPreNGDFLightLLFAdditiveRefreshInterval`|736-758|23|Resolve+clamp additive refresh interval (latched)|
|33|`GetPreNGSetupGeometryCallBudget`|760-782|23|Resolve optional SetupGeometry call budget (latched)|
|34|`GetPreNGSetupGeometryFrameBudget`|784-809|26|Resolve SetupGeometry per-frame budget (latched)|
|35|`TryReservePreNGSetupGeometryCall`|811-834|24|Consume one call from the call budget|
|36|`TryReservePreNGSetupGeometryFrameSample`|836-868|33|Consume one sample from the per-frame budget|
|37|`TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame`|870-884|15|One no-light probe per frame (CAS)|
|38|`ExtendPreNGBSLightingSetupGeometryBypassWindow`|886-905|20|Push out SetupGeometry bypass-until frame|
|39|`DetectPreNGBSLightingResourceProofMenuBlock`|909-928|20|Which blocking preview menu is open|
|40|`ProbePreNGShadowSceneBucketTotal`|935-952|18|Cheap O(1) shadow-scene bucket-count probe|
|41|`GetPreNGBSLightingLastPreviewMenuReason`|954-962|9|Name of last preview menu reason|
|42|`ShouldDeferPreNGBSLightingResourceProofForMenu`|964-1025|62|Menu + overload + settle resume gate|
|43|`ExtendPreNGBSLightingResourceProofDescriptorSettle`|1027-1054|28|Extend descriptor-burst settle window|
|44|`IsTruthyEnvironmentSwitch`|1056-1059|4|Bool switch shorthand (fwd-declared at 907)|
|45|`LogPreNGDiagnosticEnvironmentSnapshot`|1061-1176|116|One-shot dump of every PreNG debug switch|
|46|`ShouldInstallPreNGInternalPointLightHook`|1178-1186|9|Gate: install internal point-light hook|
|47|`ShouldUpdatePreNGStrictLightCB`|1188-1192|5|Gate (latched)|
|48|`ShouldBindPreNGStrictLightCB`|1194-1198|5|Gate (latched)|
|49|`ShouldBindPreNGSetupGeometryStrictLightCB`|1200-1205|6|Gate (latched)|
|50|`ShouldPersistPreNGSetupGeometryStrictLightCB`|1207-1211|5|Gate (latched)|
|51|`ShouldBindPreNGClusterSRVs`|1213-1217|5|Gate (latched)|
|52|`ShouldBindPreNGPrepassResources`|1219-1229|11|Gate, default-on, logs resolution (latched)|
|53|`ShouldBindPreNGDFLightDrawStateStrictLightCB`|1231-1235|5|Gate (latched)|
|54|`ShouldBindPreNGDFLightDrawStateClusterSRVs`|1237-1241|5|Gate (latched)|
|55|`ShouldUsePreNGSetupGeometryStrictLightCBProof`|1243-1248|6|Composite gate (latched)|
|56|`ShouldReusePreNGShadowSceneFastReuse`|1250-1260|11|Gate, default-on (latched)|
|57|`GetPreNGShadowSceneFastReuseRefreshInterval`|1262-1291|30|Resolve+clamp fast-reuse interval (latched)|
|58|`ShouldRunPreNGDFLightResourceNoOpPass`|1293-1296|4|Gate (uncached)|
|59|`ShouldRunPreNGDFLightFullContractNoOpPass`|1298-1301|4|Gate (uncached)|
|60|`ShouldRunPreNGDFLightLLFAdditivePass`|1303-1323|21|Legacy additive proof gate + warnings (latched)|
|61|`ShouldRunPreNGDFLightFullContractVisibleLLF`|1325-1329|5|Gate (latched)|
|62|`ShouldRunPreNGDFCompositeVisibleLLF`|1331-1336|6|Gate (latched)|
|63|`ShouldUsePreNGDFLightDescriptorDemandResources`|1338-1346|9|Gate + unsafe override (latched)|
|64|`ShouldUsePreNGDFCompositeDescriptorDemandResources`|1348-1353|6|Gate (latched)|
|65|`ShouldUsePreNGBSLightingDescriptorDemandResources`|1355-1359|5|Gate (latched)|
|66|`ShouldBindPreNGBSLightingLLFVisibleConsumer`|1367-1371|5|Gate (latched)|
|67|`ShouldBindPreNGDFLightForwardVisibleLLF`|1373-1377|5|Gate (latched)|
|68|`ShouldAllowPreNGBSLightingConsumerBindInMenu`|1379-1383|5|Gate (latched)|
|69|`ShouldBindPreNGBSLightingSetupGeometryResources`|1385-1389|5|Gate (latched)|
|70|`ShouldTimePreNGClusterPrepassGpu`|1391-1397|7|GPU-timing gate, deliberately NOT cached|
|71|`ShouldSubmitPreNGClusterPrepassEarly`|1399-1415|17|Prepass vs EarlyPrepass site selector (latched)|
|72|`LogPreNGHookReachabilityWatchdog`|1417-1451|35|Every-600-frame hook-reachability log|
|73|`ShouldHoldPreNGDFLightPreparedState`|1453-1457|5|Composite gate|
|74|`ShouldRunPreNGClusterPrepassProof`|1459-1522|64|Master gate for running the clustered prepass|
|75|`ShouldCompilePreNGDFLightContractProbe`|1524-1527|4|Gate (uncached)|
|76|`ShouldCompilePreNGDFLightFullShadowedCandidate`|1529-1532|4|Gate (uncached)|
|77|`TryBindPreNGBSLightingDeferredDescriptorResources`|1534-1577|44|Post-prepass deferred descriptor resource bind|
|78|`RunPreNGDFLightCompileOnlyDiagnostic`|1579-1644|66|Compile a probe PS and log its slot evidence|
|79|`RunPreNGDFLightContractProbeCompileDiagnostic`|1646-1653|8|One-shot wrapper of #78|
|80|`RunPreNGDFLightFullShadowedCandidateCompileDiagnostic`|1655-1662|8|One-shot wrapper of #78|
|81|`PreNGPointLightSetupCall::thunk`|1666-1727|62|Point-light call detour: collect + bind + log (struct 1664-1730)|
|82|`VerifyPreNGPointLightHookPatch`|1732-1759|28|Verify the E8 call patch points at our thunk|
|83|`PreNGPointLightHookStateName`|1769-1783|15|Enum -> name (enum 1761-1767)|
|84|`CanInstallPreNGSetupGeometryHooks`|1785-1789|5|Allow SetupGeometry install only if callsite trusted|
|85|`PreparePreNGPointLightHook`|1791-1821|31|Validate + optionally write the point-light thunk|

### 1b. `LightLimitFix::` members and later free functions

| # | Function | Range | ~L | Responsibility |
|---|---|---|---|---|
|86|`LoadSettings`|1825-1844|20|Read viz settings from ini|
|87|`SaveSettings`|1846-1863|18|Write viz settings to ini|
|88|`RestoreDefaultSettings`|1865-1868|4|Reset settings struct|
|89|`DrawSettings`|1870-1893|24|ImGui settings panel|
|90|`GetCommonBufferData`|1895-1906|12|Fill shared `PerFrame` CB struct|
|91|`SetupResources`|1908-2170|263|Compile CS + create all buffers/SRVs/UAVs|
|92|`DataLoaded`|2172-2182|11|PostAE: unlock `iMagicLightMaxCount`|
|93|`PostPostLoad`|2184-2260|77|Install hooks per runtime + gate logging|
|94|`BeginPreNGClusterGpuTimer`|2263-2303|41|Lazily create + begin GPU timestamp queries|
|95|`EndPreNGClusterGpuTimer`|2305-2318|14|End queries, rotate slot|
|96|`ResolvePreNGClusterGpuTimer`|2320-2360|41|Read prior slot, log GPU ms|
|97|`Prepass`|2368-2377|10|Dispatcher: run cluster prepass unless early-hook|
|98|`EarlyPrepass`|2379-2388|10|Dispatcher: early-hook variant|
|99|`RunClusterPrepass`|2390-2960|571|**The core**: gates, camera, collect, upload, build+cull dispatch, SRV bind|
|100|`HasResources`|2962-2980|19|All GPU objects present?|
|101|`GetCurrentLightsSRV`|2982-2989|8|Current rotating lights SRV (non-PRE_NG branch self-recurses)|
|102|`HasPreNGDFLightDescriptorConsumerData`|2992-2995|4|Payload readiness predicate|
|103|`HasPreNGDFCompositeDescriptorConsumerData`|2997-3000|4|Payload readiness predicate|
|104|`HasPreNGBSLightingDescriptorConsumerData`|3002-3005|4|Payload readiness predicate|
|105|`ShouldSuppressPreNGBSLightingVisibleConsumerForMenu`|3007-3037|31|Per-draw menu suppression + arm resume|
|106|`NotifyPreNGBSLightingVisibleConsumerResumeComplete`|3039-3050|12|Emit post-menu recovery marker|
|107|`NotifyPreNGDFLightLLFConsumerDescriptorObserved`|3052-3066|15|Latch DFLight descriptor observation|
|108|`HasPreNGDFLightLLFConsumerDescriptorObserved`|3068-3071|4|Read that latch|
|109|`NotifyPreNGDFCompositeLLFConsumerDescriptorObserved`|3073-3089|17|Latch DFComposite descriptor observation|
|110|`HasPreNGDFCompositeLLFConsumerDescriptorObserved`|3091-3094|4|Read that latch|
|111|`NotifyPreNGBSLightingLLFConsumerDescriptorObserved`|3096-3117|22|Latch BSLighting descriptor + settle window|
|112|`HasPreNGBSLightingLLFConsumerDescriptorObserved`|3119-3122|4|Read that latch|
|113|`Reset`|3125-3139|15|Null out PS t35-t37|
|114|`CollectLightsFromPass`|3141-3158|18|Collect BSLight* from a render pass' light list|
|115|`CollectLightsFromPreNGSceneLights`|3161-3271|111|Decode `BSRenderPass::sceneLights` -> frameLights|
|116|`CollectLightsFromPreNGShadowScene`|3274-3539|266|Decode the world ShadowSceneNode buckets + fast-reuse cache|
|117|`BindPreNGClusterSRVsToPixelShader`|3541-3614|74|Bind t35-t37 for a pass, with failure logging|
|118|`BindPreNGDescriptorResourcesToPixelShader`|3616-3637|22|Shared descriptor-resource bind body|
|119|`BindPreNGDFLightDescriptorResourcesToPixelShader`|3639-3642|4|Wrapper of #118|
|120|`BindPreNGDFCompositeDescriptorResourcesToPixelShader`|3644-3647|4|Wrapper of #118|
|121|`BindPreNGBSLightingDescriptorResourcesToPixelShader`|3649-3652|4|Wrapper of #118|
|122|`GetPreNGBSLightingSetupGeometryPreviewReasonName` (free)|3655-3668|14|Reason code -> menu name|
|123|`DetectPreNGBSLightingSetupGeometryPreviewReason` (free)|3670-3699|30|Detect preview menu / Workshop 3D preview|
|124|`GetCachedPreNGBSLightingSetupGeometryPreviewReason` (free)|3701-3720|20|Per-frame cache over #123|
|125|`ShouldProcessPreNGBSLightingSetupGeometryProof`|3723-3757|35|SetupGeometry proof bypass window|
|126|`BindPreNGBSLightingSetupGeometryResources`|3759-3845|87|One-shot SetupGeometry resource proof bind|
|127|`TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry`|3850-3917|68|Swap PS to the LLF consumer + re-assert t35-t37|
|128|`GetPreNGDFLightForwardZeroPixelShader` (free)|3932-3976|45|Lazy-create zero-output PS (anon state 3920-3930)|
|129|`UpdatePreNGDFLightForwardCameraCB`|3978-4034|57|Create/update the DFLight forward cb3 (96 B)|
|130|`CapturePreNGDFLightCameraCBOnce` (anon)|4049-4107|59|One-shot capture of vanilla cb12 (anon ns 4037-4109)|
|131|`HandlePreNGDFLightForwardBatchPostCall`|4113-4265|153|Per-draw DFLight forward consumer / zero-pass replacement|
|132|`BindPreNGDFLightDrawStateStrictLightCB`|4267-4283|17|Legacy no-op strict-CB entry point|
|133|`BindPreNGDFLightDrawStateClusterSRVs`|4285-4357|73|Draw-state t35-t37 bind + proof logs|
|134|`BindPreNGDFLightNoOpPassResources`|4359-4417|59|Shared no-op pass resource bind body|
|135|`BindPreNGDFLightResourceNoOpPass`|4419-4423|5|Wrapper of #134|
|136|`BindPreNGDFLightFullContractNoOpPass`|4425-4429|5|Wrapper of #134|
|137|`BindPreNGDFLightLLFAdditivePass`|4431-4435|5|Wrapper of #134|
|138|`TracePreNGActiveLightingBindings`|4437-4664|228|Query live PS/SRV state, audit-log LLF completeness|
|139|`CollectLightCB`|4667-4747|81|Read vanilla PS cb2 staging copy -> frameLights|
|140|`CollectLightsFromBSLight`|4749-4769|21|`seenLights` -> frameLights|
|141|`CollectLightsFromScene`|4771-4814|44|Walk `TESObjectREFR` forms for LIGH refs|
|142|`SetupGeometryBefore`|4816-4819|4|Clear `seenThisPass`|
|143|`SetupGeometryAfter`|4821-4858|38|PreNG budgeted scene-light collection / else pass collect|
|144|`HasPostNGBSLightingDescriptorConsumerData`|4873-4879|7|PostNG payload readiness (anon statics 4865-4871)|
|145|`BindPostNGBSLightingClusterResourcesToPixelShader`|4881-4896|16|PostNG t35-t37 re-assert|
|146|`NotifyPostNGBSLightingLLFConsumerDescriptorObserved`|4898-4913|16|PostNG descriptor latch|
|147|`HasPostNGBSLightingLLFConsumerDescriptorObserved`|4915-4918|4|Read that latch|
|148|`InstallPreNGBSLightingBatchHook`|4922-4944|23|Detour `BS_LIGHTING_BATCH_SETUP`|
|149|`Hooks::Install`|4947-4987|41|`write_vfunc<0x7>` on 2-3 shader vtables|
|150|`Hooks::BSLightingShader_SetupGeometry::thunk`|4989-5039|51|Before/after collect + resource + consumer bind|
|151|`Hooks::BSDFLightShader_SetupGeometry::thunk`|5042-5050|9|Post-call DFLight forward handling|
|152|`Hooks::PreNGBSLightingBatchSetup::thunk`|5054-5072|19|cb12 capture + consumer bind after batch setup|
|153|`Hooks::BSEffectShader_SetupGeometry::thunk`|5075-5081|7|Effect-shader before/after collect|

Non-function file items: `namespace RE::VTABLE {}` empty block 4860-4862.

## 2. Proposed clusters (each <= ~600 lines), under `src/Features/LightLimit/`

Plus one shared non-TU header `src/Features/LightLimit/LLFInternal.h` holding the `k*` constants (66-140, 159-204, 286-336), the `s_preNG*` atomics (141-212), the gate-predicate declarations, `ReadPreNGRaw*` templates and the small enums/structs (`PreNGShaderSlotEvidence` 509-519, `PreNGLightDecodeResult` 572-578, `EnvironmentSwitch*` 687-705, `PreNGPointLightHookState` 1761-1767).

| Cluster / new file | Functions (#) | Lines |
|---|---|---|
|`LLFConfig.cpp` — debug-switch plumbing, budgets, all `Should*`/`Get*` gates|28-38, 44, 46-71, 73-76|~541|
|`LLFDiagnostics.cpp` — env snapshot, hook watchdog, compile-only probes|45, 72, 78-80|~245|
|`LLFPreviewMenu.cpp` — preview-menu detection/suppression + BSLighting consumer bind|39-43, 105, 106, 122-127|~450|
|`LLFShaderMetadata.cpp` — shader-metadata formatting + live binding audit|15-24, 138|~365|
|`LLFLightDecode.cpp` — hashing, fast-reuse keys, wrapper decode|5-14, 25, 26|~235|
|`LLFPointLightHook.cpp` — point-light callsite validation, thunk, install|27, 81-85|~190|
|`LLFSettings.cpp` — ini load/save, ImGui, common buffer, DataLoaded|86-90, 92|~95|
|`LLFResources.cpp` — shader path/log helpers, SetupResources, HasResources, SRV accessor, Reset, GPU timers|2-4, 91, 94-96, 100, 101, 113|~430|
|`LLFClusterPrepass.cpp` — Prepass/EarlyPrepass/RunClusterPrepass|97-99|~598|
|`LLFLightCollect.cpp` — every light-collection path + SetupGeometry before/after|114-116, 139-143|~584|
|`LLFClusterBind.cpp` — cluster/descriptor binds, consumer-data predicates, descriptor latches, DFLight draw-state + no-op binds|102-104, 107-112, 117-121, 132-137|~415|
|`LLFDFLightForward.cpp` — zero PS, forward cb3, cb12 capture, per-draw forward replacement|128-131|~350|
|`LLFHooks.cpp` — PostPostLoad, PostNG consumer, batch hook install, all vfunc thunks|93, 144-153|~300|
|`LLFRuntimeAddresses.cpp` (tiny) — TLS/renderer-state base + deferred descriptor bind|1, 77|~65|

## 3. Shared mutable state and coupling risk

**`LightLimitFix` members** (declared in the header, so every cluster can reach them — the real coupling):

| Member | Written by (cluster) | Read by (cluster) |
|---|---|---|
|`frameLights`|Prepass, LightCollect, BSLighting-setup (3782)|Prepass, LightCollect, PreviewMenu|
|`currentLightCount`|Prepass (2639, 2518), BSLighting-setup (3782)|**7 clusters** — ClusterBind, DFLightForward, ShaderMetadata audit, Settings UI, PreviewMenu|
|`seenLights`, `seenThisPass`, `seenCBHashes`|Prepass, LightCollect|Prepass, LightCollect|
|`clusterBuildCache(+Valid)`|Resources (1917-1918), Prepass (2766-2813)|Prepass|
|`clusterPayloadCache(+Valid)`|Resources, Prepass (2519, 2921)|Prepass|
|`shadowSceneFastReuse(+Valid)`|Resources, Prepass (2521), LightCollect (3286, 3372, 3511-3535)|LightCollect|
|`preNGDFLightLastSnapshotCameraPos/ViewRows/ViewValid`|Prepass (2700-2702)|Prepass (2864-2869) only|
|`currentLightsBufferIndex`, `lightsBuffers[]`, `lightsSRVs[]`|Resources, Prepass (2722)|ClusterBind, DFLightForward, audit|
|all other `winrt::com_ptr` GPU objects|Resources|Prepass, ClusterBind, DFLightForward, audit|
|`diagFrameCounter`|Prepass|Prepass|
|`dflightForwardCB`|DFLightForward|DFLightForward|
|`preNGClusterGpuTimers*`|Resources cluster (timers)|Prepass|
|`settings`, `CameraNear/Far`, `clusterSize[3]`|Settings, Resources|Settings, Prepass, ClusterBind, audit|

**File-scope statics (anon namespace, lines 141-215)** — all `std::atomic`; these are the cross-cluster globals and must move to `LLFInternal.h` as `extern` (or accessor functions), NOT be duplicated:

- 141-142 `s_preNGDFLightLLFConsumerDescriptorObserved/Observations` — written ClusterBind (3056-3058), read Config gate `ShouldRunPreNGClusterPrepassProof` (1468).
- 143-144 `s_preNGDFCompositeLLFConsumerDescriptorObserved/Observations` — ClusterBind (3078-3080) / Config (1470).
- 145-151 `s_preNGBSLightingLLFConsumerDescriptorObserved/Observations/LastVertexDescriptor/LastPixelDescriptor/LastFound/LastVanillaPixelShader`, `s_preNGBSLightingDeferredResourceProofComplete` — written ClusterBind (3100-3104), read Config (1472), RuntimeAddresses (1537-1552, 1571).
- 175-180 `s_preNGBSLightingResourceProofBypassUntilFrame/BypassLogs`, `VisibleConsumerMenuSuppressLogs`, `PreviewMenuLastReason`, `PreviewMenuResumePending`, `PreviewMenuConsumerResumePending` — PreviewMenu cluster + Prepass (2487-2489) + ClusterBind (3024, 3041).
- 189 `s_preNGShadowSceneLastBucketTotal` — PreviewMenu only.
- 199-204 `s_preNGBSLightingSetupGeometryNoLightNextProbeFrame/BypassUntilFrame/BypassLogs/PreviewCacheFrame/PreviewCacheReason` — Config (886-905), PreviewMenu (3701-3720, 3743-3746).
- 205-212 `s_preNGPointLightHookInstalled/PatchVerified/CallCount`, `s_preNGBSLightingSetupGeometryHookInstalled/HookCallCount/BypassCallCount`, `s_preNGBSLightingBatchSetupHookInstalled/HookCallCount` — written PointLightHook + Hooks clusters, read Diagnostics watchdog (1424-1450).
- 214-215 (PRE_NG) `s_preNGDFLightCameraCB` (`winrt::com_ptr<ID3D11Buffer>`), `s_preNGDFLightCameraCBCaptured` — written DFLightForward (4101-4103), read **Prepass** (2773, 2797-2800) and DFLightForward (4139). This is the single hardest cross-cluster dependency: a COM pointer shared between the capture hook and the compute dispatch.
- 4869-4870 (non-PRE_NG) `s_postNGBSLightingLLFConsumerDescriptorObserved/Observations` — Hooks cluster only.
- 3928-3929 `s_preNGDFLightForwardZeroLock` + `s_preNGDFLightForwardZeroState`; 4044-4045 `s_preNGDFLightCB12CaptureLock` + `s_preNGDFLightCB12CaptureState` — both DFLightForward-only, safe to move whole.

## 4. Anonymous-namespace helpers and file-static data -> cluster

| Item | Lines | Cluster |
|---|---|---|
|`kClusterMaxLights`, `kMaxLights`|66-67|`LLFInternal.h`|
|All `kPreNG*` env-name / budget / threshold constants|69-140, 159-204(const parts), 286-287, 328-335|`LLFInternal.h`|
|All `s_preNG*` atomics|141-212|`LLFInternal.h` (extern)|
|`s_preNGDFLightCameraCB` + captured flag|214-215|`LLFInternal.h` (extern) — read by Prepass|
|`namespace F4Runtime = RE::FO4Runtime`|326|`LLFInternal.h`|
|`ReadPreNGRaw`/`ReadPreNGRawField` templates|374-385|`LLFInternal.h` (must stay header-inline)|
|`using PreNGPixelShaderEntryState`|429|`LLFInternal.h`|
|`struct PreNGShaderSlotEvidence`|509-519|ShaderMetadata (decl in `LLFInternal.h`)|
|`using PreNGShadowSceneNodeRef`|565|`LLFInternal.h`|
|`enum class PreNGLightDecodeResult`|572-578|LightDecode (decl in `LLFInternal.h`)|
|`enum EnvironmentSwitchSource` / `EnvironmentSwitchState` / `EnvironmentUIntState`|687-705|Config (decl in `LLFInternal.h`)|
|fwd decl `IsTruthyEnvironmentSwitch`|907|remove; declare in `LLFInternal.h`|
|`struct PreNGPointLightSetupCall`|1664-1730|PointLightHook|
|`enum class PreNGPointLightHookState`|1761-1767|PointLightHook (decl in `LLFInternal.h`)|
|`PreNGDFLightForwardZeroShaderState` + lock/state|3920-3930|DFLightForward|
|`PreNGDFLightCB12CaptureState` + lock/state|4037-4109|DFLightForward|
|empty `namespace RE::VTABLE {}`|4860-4862|delete (dead)|
|PostNG env names + observed atomics|4865-4871|Hooks|

## 5. Preprocessor conditional regions (exact ranges)

`FALLOUT_POST_AE` include selects: 28-32, 33-37, 38-46, 47-51 (all `#if POST_AE ... #else ... #endif`).
`FALLOUT_POST_AE` code: 2174-2181 (DataLoaded magic-light unlock); 2252-2256 is the `#elif POST_AE` arm of 2186/2259.

`FALLOUT_PRE_NG` regions (`#if`-line .. `#endif`-line):
18-21 · 68-237 (nested 213-236) · 262-1822 (the whole PreNG anon-namespace body) · 1916-1923 · 1928-1931 · 2007-2046 (`#else` at 2027) · 2186-2259 (`#elif POST_AE` 2252, `#else` 2257) · 2262-2361 · 2370-2375 · 2381-2387 · 2394-2413 · 2462-2526 · 2563-2590 · 2592-2600 · 2602-2637 (`#else` 2628) · 2640-2648 · 2659-2661 · 2664-2667 · 2721-2737 (`#else` 2730) · 2740-2752 · 2765-2778 · 2796-2802 · 2806-2824 · 2827-2840 · 2915-2923 · 2927-2959 (`#else` 2947) · 2964-2972 · 2984-2988 (`#else` 2986) · 2991-3123 · 3160-3272 · 3654-3721 · 3919-4665 (nested 4036-4110, 4135-4143) · 4823-4857 (`#else` 4855) · 4921-4945 · 4957-4959 · 4961-4963 · 4992-5031 · 5035-5038 · 5041-5051 · 5053-5073 (nested 5063-5065).

`#if !defined(FALLOUT_PRE_NG)` regions: 2974-2976 · 4864-4919.

Note: 3274-3653 and 3723-3918 define PreNG-named members with **no** `#if` guard even though the header declares them inside `#if defined(FALLOUT_PRE_NG)`. Preserve this as-is when cutting (or the PostNG build breaks differently than today).

## 6. Raw game-address / unsafe-cast sites

- **Hardcoded absolute addresses**: 222 `RuntimeAddressValue{0x1467347B4}` (TlsIndex), 223 `RuntimeAddressValue{0x1461DDC68}` (renderer fallback).
- **TEB/TLS walk**: 225 `__readgsqword(0x30)`, 226 `*(uintptr_t*)(teb + 0x58)`, 227 `tlsIndex < 0x400`, 228 `*(uintptr_t*)(tlsArray + tlsIndex*8)`, 229 `*(uintptr_t*)(slot + 2848)`.
- **Renderer-state struct offsets**: 2682-2686 and 2874-2879 — `rendererBase + 7024 + 114*16` (view rows, 4x16 B) and `rendererBase + 8736` (camera pos, 3 floats), both raw `memcpy` from `reinterpret_cast<const void*>`.
- **`F4Runtime::PreNG::` relocations / field offsets**: 329-331 (`BS_RENDER_PASS_SCENE_LIGHT_FIRST_INDEX`, `INVALID_SHADOW_LIGHT_MASK_INDEX`, `MAX_SHADOW_LIGHT_MASK_BITS`); 592-593, 601-608, 636 (BSLight wrapper + NiLight field offsets, incl. manual `+ sizeof(float)` / `+ 2*sizeof(float)` component reads); 1566, 3831 (`BS_LIGHTING_SHADER_TYPE`); 1737 `POINT_LIGHT_TARGET.address()`; 1801 `POINT_LIGHT_CALL.address()`; 3869 `IsBSLightingContractPixelDescriptor`; 3883-3885 and 4225-4227 (`CURRENT_VERTEX/HULL/DOMAIN_SHADER_ENTRY.address()`); 3891-3892 / 4228-4229 (`BIND_SHADERS.address()`, `CURRENT_PIXEL_SHADER_ENTRY.address()`); 3904, 4243 (`RENDERER_STATE.address()`); 3971 `DF_LIGHT_FORWARD_PIXEL_DESCRIPTOR_8004`; 4119 `DF_LIGHTING_SHADER_TYPE`; 4130 `IsDFLightForwardPixelDescriptor`; 4930 `BS_LIGHTING_BATCH_SETUP.address()`.
- **Hook writes / relocations**: 1729 `REL::Relocation<decltype(thunk)> func` (point-light); 1804 `stl::write_thunk_call<PreNGPointLightSetupCall>`; 4937-4940 `Detours::X64::DetourFunction`; **vtable index 7**: 4956 `write_vfunc<0x7, BSLightingShader_SetupGeometry>(RE::VTABLE::BSLightingShader[0])`, 4958 `...BSDFLightShader[0]`, 4969 `...BSEffectShader[0]`. Header also holds `REL::Relocation` members at 348, 354, 360.
- **Raw function-pointer calls**: 3902-3904 and 4240-4247 — `reinterpret_cast<PreNGBindShadersFn>(bindAddr)` then call with 5 raw `uintptr_t`.
- **Game-pointer `reinterpret_cast`**: 377 (generic raw read), 3239 / 3455 / 4796 (`niLightAddress` -> `RE::BSLight*`), 4755 / 4792 (`RE::NiLight*`), 2534 / 2550 / 2695 / 2885 (`camViewData.viewMat|projMat` -> `XMFLOAT4X4`), 2571 (`__m128*` -> `float*`), 2715 (`positionWS[1].data` -> `XMFLOAT3*`), 1736 (`&thunk` -> uintptr), 3876-3877, 4235.
- **D3D handle casts from `fo4cs::GetRendererData()`**: 2433, 2665, 3133, 3580, 4005, 4063-4064, 4252, 4596, 4672-4673, 4885.

## 7. Function-local `static` variables (must travel with their function; never duplicate)

- Latched config values (`static const`, one-shot resolve+log): 738, 762, 786, 1007 (`disableOverloadGate` inside #42), 1190, 1196, 1202, 1209, 1215, 1221, 1233, 1239, 1245, 1252, 1264, 1305, 1327, 1333, 1340, 1350, 1357, 1369, 1375, 1381, 1387, 1407.
- One-shot log latches (`static bool`): 1063 (`logged`), 1495 (`loggedHeld`), 1649/1658 (`loggedHeld` — **two distinct copies**, #79 and #80), 2287 (`loggedFailure`).
- One-shot `std::atomic_bool` latches: 820, 1648, 1657 (`attempted`, two copies), 3776 (`setupGeometryResourceProofComplete`), 4442-4446 (audit-complete/skip-logged pair x2), 4924 (`installed`), 4949-4950 (`lightingInstalled`, `effectInstalled`).
- Throttled-log counters (`static std::atomic_uint32_t`, all independent): 819, 860, 1556, 2476, 2507, 2615, 2743, 2815, 2830, 2935, 3252, 3288, 3350, 3382, 3473, 3519, 3551, 3590, 3602, 3628, 3793, 3812, 3826, 3908, 4186, 4201, 4301, 4336, 4346, 4370, 4406, 4444, 4447, 4483-4491 (nine counters inside the `logAudit` lambda), 4836, 4847, 5022.
- Frame-change detection (**non-atomic, plain statics**): 846-847 (`sampledFrame`, `sampledThisFrame` in #36) — a real data race if that function is ever called from two threads; keep as-is unless deliberately fixed.
- Per-frame dedup for DFLight forward: 4151-4152 (`s_lastDFLightConsumerBoundFrame`, `s_lastDFLightConsumerAttemptFrame`).
- Static members inside local structs: 1729 `PreNGPointLightSetupCall::func`.

Duplicate *names* across different functions that must NOT be merged: `failureCount` (3288, 3551, 4301, 4370), `bindCount` (3590, 3908, 4336, 4406), `nonZeroBindCount` (3602, 4346), `decodeDiagCount` (3252, 3473), `attempted`/`loggedHeld` (1648-1649, 1657-1658), `enabled` (26 sites).

## 8. Extraction order (least risk first) and immovables

1. `LLFInternal.h` first — move constants, atomics, templates, enums. No behavior change, but it is a prerequisite for everything else.
2. `LLFSettings.cpp` (#86-90, 92) — no PreNG state, no statics.
3. `LLFResources.cpp` (#2-4, 91, 94-96, 100, 101, 113) — touches only `this` GPU members plus one `loggedFailure` static.
4. `LLFShaderMetadata.cpp` (#15-24) then add #138 — pure metadata formatting; #138 carries 15 statics, move them verbatim.
5. `LLFLightDecode.cpp` (#5-14, 25, 26) — pure functions, no statics.
6. `LLFConfig.cpp` (#28-38, 44, 46-71, 73-76) — bulk move; the risk is only that a `static const` latch must not end up duplicated across TUs, so **no gate predicate may be inlined into a header**.
7. `LLFDiagnostics.cpp` (#45, 72, 78-80).
8. `LLFPointLightHook.cpp` (#27, 81-85).
9. `LLFLightCollect.cpp` (#114-116, 139-143).
10. `LLFClusterBind.cpp` (#102-104, 107-112, 117-121, 132-137).
11. `LLFPreviewMenu.cpp` (#39-43, 105, 106, 122-127).
12. `LLFDFLightForward.cpp` (#128-131).
13. `LLFHooks.cpp` (#93, 144-153).
14. `LLFClusterPrepass.cpp` (#97-99) — **last**; it depends on nearly every other cluster.

**Cannot move without changing behavior:**

- Every `static`/`static const`/`static std::atomic_*` listed in §7 is a per-*function* singleton. If a function is duplicated, or a gate predicate is turned into an inline/header function, the latch splits and log/latch behavior changes. Keep each gate predicate in exactly one TU.
- `s_preNGDFLightCameraCB` (214) must remain a single object visible to both `RunClusterPrepass` (2773, 2797-2800) and `CapturePreNGDFLightCameraCBOnce` (4101-4103). Extern in `LLFInternal.h`, defined in exactly one TU (suggest `LLFDFLightForward.cpp`).
- `ShouldTimePreNGClusterPrepassGpu` (1391-1397) is deliberately **not** cached (comment at 1393-1395). Do not "optimise" it into a latched static during the move.
- `#79`/`#80` (1646-1662) each own their own `attempted`/`loggedHeld` pair; they must stay two separate functions.
- `GetCurrentLightsSRV` (2982-2989) has an infinite self-recursion in its non-PRE_NG branch (2987). It is a pre-existing bug — moving it must not silently "fix" or hide it; flag it separately.
- The unguarded PreNG member definitions at 3274-3653 and 3723-3918 (see §5 note) are compiled unconditionally today; wrapping them in `#if defined(FALLOUT_PRE_NG)` during extraction *is* a behavior change for the PostNG build.
- Ordering dependency: `IsTruthyEnvironmentSwitch` is forward-declared at 907 and defined at 1056 because #42 uses it at 1007. Splitting Config from PreviewMenu removes the need for that forward declaration — the declaration must land in `LLFInternal.h`.
- `Hooks::Install` (4947) and `InstallPreNGBSLightingBatchHook` (4922) are one-shot via their own atomics; they must not be called from more than one TU's static initialiser.
