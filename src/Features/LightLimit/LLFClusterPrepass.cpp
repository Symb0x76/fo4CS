// LightLimitFix -- the clustered light prepass.
//
// Prepass / EarlyPrepass dispatch and RunClusterPrepass itself: gates, camera
// derivation, light collection, constant-buffer upload, the build + cull
// compute dispatches and the SRV bind.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #97-99). The
// outline puts this cut last on purpose -- it reaches into nearly every other
// cluster, so every cross-cluster declaration had to exist first.
//
// The FO4CS_LLF_ZONE markers here are the ed927cb Tracy instrumentation. They
// move verbatim, names unchanged, so captures taken before the split stay
// comparable with captures taken after it.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif

#include <DirectXMath.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>

// The members below are LightLimitFix:: members at global scope, and they name
// the cluster helpers and shared state unqualified exactly as they did in the
// parent translation unit.
using namespace CommunityShaders::lightlimit;
#include <cstring>
#include <memory>
#include <vector>

// Dispatcher: the clustered compute can be submitted either from the default
// Main_RenderWorld_Start site (Prepass) or, when FO4CS_LLF_PRENG_PREPASS_EARLY_HOOK
// is set, from the Main_RenderShadowMaps phase (EarlyPrepass) so the dispatch
// overlaps the engine's shadow GPU batch and avoids the frame-start idle pocket
// that triggers the FrameGen-interop downclock. Exactly one site runs per frame.
void LightLimitFix::Prepass()
{
#if defined(FALLOUT_PRE_NG)
    if (ShouldSubmitPreNGClusterPrepassEarly())
    {
        return; // handled in EarlyPrepass() this run
    }
#endif
    RunClusterPrepass();
}

void LightLimitFix::EarlyPrepass()
{
#if defined(FALLOUT_PRE_NG)
    if (!ShouldSubmitPreNGClusterPrepassEarly())
    {
        return; // handled in Prepass() this run
    }
    RunClusterPrepass();
#endif
}

void LightLimitFix::RunClusterPrepass()
{
    const auto frameNumber = ++diagFrameCounter;

#if defined(FALLOUT_PRE_NG)
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: runtime unavailable");
        }
        return;
    }
    if (runtime->GetFrameCount() < kPreNGStableFrame)
    {
        if (frameNumber == 1)
        {
            logger::info("[LightLimitFix] PreNG Prepass waiting for stable frame gate ({})", kPreNGStableFrame);
        }
        return;
    }
    LogPreNGHookReachabilityWatchdog(runtime->GetFrameCount());
#endif

    if (!HasResources())
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: GPU resources are incomplete");
        }
        return;
    }

    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: renderer data unavailable");
        }
        return;
    }
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    if (!context)
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: D3D11 context unavailable");
        }
        return;
    }

    auto clearComputeBindings = [&] {
        ID3D11ShaderResourceView *nullSRVs[2]{};
        context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView *nullUAVs[3]{};
        context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
        ID3D11Buffer *nullCB = nullptr;
        context->CSSetConstantBuffers(0, 1, &nullCB);
        context->CSSetShader(nullptr, nullptr, 0);
    };
    auto clearPixelClusterSRVs = [&] {
        ID3D11ShaderResourceView *nullSRVs[3]{};
        context->PSSetShaderResources(35, ARRAYSIZE(nullSRVs), nullSRVs);
    };
    auto clearPixelLLFBindings = [&] {
        clearPixelClusterSRVs();
        ID3D11Buffer *nullCB = nullptr;
        context->PSSetConstantBuffers(3, 1, &nullCB);
    };

#if defined(FALLOUT_PRE_NG)
    // The consumer fallback alone is insufficient for 3D preview menus: the
    // clustered compute and b3/t35-t37 bindings otherwise continue to run every
    // frame behind vanilla BSLighting. Stop the whole LLF workload while the menu
    // is live, then resume only after the existing post-menu scene-state gate.
    if (ShouldDeferPreNGBSLightingResourceProofForMenu())
    {
        clearComputeBindings();
        clearPixelLLFBindings();
        seenLights.clear();
        seenThisPass.clear();
        seenCBHashes.clear();
        frameLights.clear();

        static std::atomic_uint32_t previewMenuPrepassHoldCount = 0;
        const auto holdIndex = ++previewMenuPrepassHoldCount;
        if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
        {
            logger::info("[LightLimitFix] PreNG clustered Prepass held for UI preview holds={} frame={} reason={}; "
                         "compute and b3/t35-t37 cleared",
                         holdIndex, runtime->GetFrameCount(), GetPreNGBSLightingLastPreviewMenuReason());
        }
        return;
    }

    if (s_preNGBSLightingPreviewMenuResumePending.exchange(false, std::memory_order_relaxed))
    {
        s_preNGBSLightingPreviewMenuConsumerResumePending.store(true, std::memory_order_relaxed);
        logger::info("[LightLimitFix] PreNG UI preview closed; clustered Prepass resumed frame={} lastMenu={}; "
                     "awaiting llfConsumerComplete=true world bind",
                     runtime->GetFrameCount(), GetPreNGBSLightingLastPreviewMenuReason());
    }

    // Skyrim-parity step 1 (BOSS): no persistent/throttle distinction — the prepass
    // dispatches build+cull and binds t35-t37/b3 every frame via the plain path
    // below. If the proof gate says "don't run", clear and bail.
    if (!ShouldRunPreNGClusterPrepassProof())
    {
        clearPixelLLFBindings();
        seenLights.clear();
        seenThisPass.clear();
        seenCBHashes.clear();
        frameLights.clear();
        if (ShouldHoldPreNGDFLightPreparedState() && currentLightCount > 0)
        {
            static std::atomic_uint32_t holdCount = 0;
            const auto holdIndex = ++holdCount;
            if (holdIndex <= 8 || holdIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass prepared state retained for DFLight proof pass "
                             "holds={} lights={}",
                             holdIndex, currentLightCount);
            }
        }
        else
        {
            currentLightCount = 0;
            clusterPayloadCacheValid = false;
            clusterPayloadCache = {};
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
        return;
    }
#endif

    const auto &gState = RE::BSGraphics::State::GetSingleton();
    const auto &camView = gState.cameraState.camViewData;

    DirectX::XMFLOAT4X4 projInvTransposed;
    {
        DirectX::XMMATRIX proj =
            DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4 *>(camView.projMat));
        DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
        DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
    }
    if (!IsFiniteMatrix(projInvTransposed))
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: camera projection matrix is not invertible");
        }
        return;
    }

    DirectX::XMFLOAT4X4 viewTransposed;
    DirectX::XMFLOAT4X4 viewMatrix;
    {
        viewMatrix = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(camView.viewMat);
        DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewMatrix);
        DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
    }
    if (!IsFiniteMatrix(viewTransposed))
    {
        if (frameNumber == 1 || frameNumber % 300 == 0)
        {
            logger::warn("[LightLimitFix] Prepass skipped: camera view matrix is invalid");
        }
        return;
    }

#if defined(FALLOUT_PRE_NG)
    // TEMP RE DUMP: log every camera matrix row once so the cluster-building
    // projection convention can be compared against vanilla DFLight's dual
    // inverse-projection rows (cb12[20..27]).
    if (frameNumber == 1 || frameNumber % 600 == 0)
    {
        const auto *viewData = std::addressof(camView);
        auto dumpMatrix = [&](const char *a_name, const __m128 *a_rows) {
            const auto *floats = reinterpret_cast<const float *>(a_rows);
            logger::info("[LightLimitFix] PreNG camera matrix dump frame={} name={} "
                         "m00={:.6f} m01={:.6f} m02={:.6f} m03={:.6f} m10={:.6f} m11={:.6f} m12={:.6f} m13={:.6f} "
                         "m20={:.6f} m21={:.6f} m22={:.6f} m23={:.6f} m30={:.6f} m31={:.6f} m32={:.6f} m33={:.6f}",
                         frameNumber, a_name,
                         floats[0], floats[1], floats[2], floats[3],
                         floats[4], floats[5], floats[6], floats[7],
                         floats[8], floats[9], floats[10], floats[11],
                         floats[12], floats[13], floats[14], floats[15]);
        };
        dumpMatrix("viewMat", viewData->viewMat);
        dumpMatrix("projMat", viewData->projMat);
        dumpMatrix("viewProjMat", viewData->viewProjMat);
        dumpMatrix("viewProjUnjittered", viewData->viewProjUnjittered);
        dumpMatrix("currentViewProjUnjittered", viewData->currentViewProjUnjittered);
        dumpMatrix("inv1stPersonProjMat", viewData->inv1stPersonProjMat);
        logger::info("[LightLimitFix] PreNG camera matrix dump frame={} near={} far={}",
                     frameNumber, CameraNear, CameraFar);
    }
#endif

#if defined(FALLOUT_PRE_NG)
    if (ShouldBindPreNGDFLightForwardVisibleLLF())
    {
        // Keeps the DFLight forward consumer cb3 (inverse-view + cluster z
        // domain) fresh for the per-frame clustered pass replacement. The
        // cluster z domain now matches vanilla cb12: z in [1, 333333].
        UpdatePreNGDFLightForwardCameraCB(viewMatrix, 1.0f, 333333.0f);
    }
#endif

#if defined(FALLOUT_PRE_NG)
    {
        FO4CS_LLF_ZONE("LLF/CollectLights");
        std::vector<LightData> preNGSceneLightFallback;
        preNGSceneLightFallback.swap(frameLights);

        seenLights.clear();
        seenThisPass.clear();

        if (CollectLightsFromPreNGShadowScene() == 0)
        {
            if (!preNGSceneLightFallback.empty())
            {
                frameLights.swap(preNGSceneLightFallback);

                static std::atomic_uint32_t fallbackUseCount = 0;
                const auto fallbackIndex = ++fallbackUseCount;
                if (fallbackIndex <= 8 || fallbackIndex % 512 == 0)
                {
                    logger::info("[LightLimitFix] PreNG scene-light fallback feeds clustered prepass uses={} lights={}",
                                 fallbackIndex, static_cast<std::uint32_t>(frameLights.size()));
                }
            }
            else
            {
                CollectLightsFromScene();
            }
        }
    }
#else
    if (!seenLights.empty())
    {
        CollectLightsFromBSLight();
    }
    else
    {
        CollectLightsFromScene();
    }
#endif

    currentLightCount = static_cast<std::uint32_t>(frameLights.size());
#if defined(FALLOUT_PRE_NG)
    const auto preNGClusterPayloadCurrent =
        MakePreNGClusterPayloadCacheState(frameLights, currentLightCount, viewTransposed, CameraNear, CameraFar,
                                          clusterSize);
    // Skyrim-parity step 1 (BOSS): never reuse the cached payload — dispatch the
    // cluster build+cull compute EVERY frame like Skyrim CS. The ViewHash/inputs
    // reuse was the mechanism that, combined with periodic resubmit, produced the
    // GPU power-state event; Skyrim avoids it by simply always dispatching.
#endif

    if (frameNumber % 300 == 0)
    {
        logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}", frameNumber,
                     currentLightCount, clusterSize[0], clusterSize[1], clusterSize[2], CameraNear, CameraFar);
    }

    seenLights.clear();
    seenCBHashes.clear();

#if defined(FALLOUT_PRE_NG)
    // Skyrim-parity: always run the compute submission block (no payload-reuse skip).
#endif
    {
        // Covers the compute-submission block. Its SELF time excludes the nested
        // Transform/Upload/ClusterBuild/ClusterCull zones, so whatever it reports is
        // cost in this block that none of those four account for -- the renderer-state
        // probes, the GPU timer, and the payload cache work.
        FO4CS_LLF_ZONE("LLF/ComputeBlock");

#if defined(FALLOUT_PRE_NG)
        auto *timingDevice = reinterpret_cast<ID3D11Device *>(rendererData->device);
        const auto gpuTimerSlot = BeginPreNGClusterGpuTimer(context, timingDevice);
#endif

        if (currentLightCount > 0)
        {
            // Self time here is the first renderer-state probe and the snapshot
            // bookkeeping; TransformLights and UploadLights are nested children.
            FO4CS_LLF_ZONE("LLF/LightPrep");
            DirectX::XMFLOAT4X4 viewRows{};
            bool viewRowsValid = false;
            DirectX::XMFLOAT3 camPos{};
            bool camPosValid = false;
#if defined(FALLOUT_PRE_NG)
            // Replicate the EXACT transform vanilla sub_1428C37A0 uses for
            // cb2[1]: read the renderer-base camera position (+8736) and the
            // view rows (base + 7024 + 114..117 * 16), then
            // viewPos = (lightWorld - camWorld) * viewRows (row-vector, with
            // perspective divide). This is the only source guaranteed to match
            // the space of vanilla cb2[1].
            //
            // PreNG only: those are byte offsets into the 1.10.163 renderer state,
            // recovered from that build's disassembly. There is no reason for them
            // to hold on 1.10.984 or the Anniversary build, so the other runtimes
            // fall through to the CommonLibF4 camera state below -- the same path
            // PreNG itself takes whenever the raw read is not readable.
            const auto rendererBase = GetPreNGDFLightRendererStateBase();
            if (IsPreNGDFLightRendererStateReadable(rendererBase))
            {
                std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(float) * 3);
                viewRowsValid = true;
                camPosValid = true;
            }
#endif
            if (!viewRowsValid || !camPosValid)
            {
                // Fallback: previous camViewData-based rotation, so lights keep
                // a consistent (if not vanilla-exact) space rather than garbage.
                const auto &gfxState = RE::BSGraphics::State::GetSingleton();
                viewRows = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(gfxState.cameraState.camViewData.viewMat);
                const auto &p = gfxState.cameraState.posAdjust;
                camPos = DirectX::XMFLOAT3{ p.x, p.y, p.z };
            }

#if defined(FALLOUT_PRE_NG)
            preNGDFLightLastSnapshotCameraPos = DirectX::XMFLOAT4{ camPos.x, camPos.y, camPos.z, 0.0f };
            preNGDFLightLastSnapshotViewRows = viewRows;
            preNGDFLightLastSnapshotViewValid = viewRowsValid && camPosValid;
#endif

            DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewRows);
            DirectX::XMVECTOR camPosV = DirectX::XMLoadFloat3(&camPos);
            {
                FO4CS_LLF_ZONE("LLF/TransformLights");
                for (auto &light : frameLights)
                {
                    DirectX::XMFLOAT3 worldPos{
                        light.positionWS[0].data.x,
                        light.positionWS[0].data.y,
                        light.positionWS[0].data.z };
                    DirectX::XMVECTOR rel = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&worldPos), camPosV);
                    DirectX::XMVECTOR viewPos = DirectX::XMVector3TransformCoord(rel, view);
                    DirectX::XMStoreFloat3(
                        reinterpret_cast<DirectX::XMFLOAT3 *>(&light.positionWS[1].data),
                        viewPos);
                    light.positionWS[1].pad = 0;
                }
            }

            FO4CS_LLF_ZONE("LLF/UploadLights");
            const auto lightUploadBytes = static_cast<UINT>(currentLightCount * sizeof(LightData));
#if defined(FALLOUT_PRE_NG)
            currentLightsBufferIndex = (currentLightsBufferIndex + 1) % kPreNGLightsBufferFrames;
            auto &lightsBuffer = lightsBuffers[currentLightsBufferIndex];
            D3D11_MAPPED_SUBRESOURCE lightMapped{};
            if (SUCCEEDED(context->Map(lightsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &lightMapped)))
            {
                std::memcpy(lightMapped.pData, frameLights.data(), lightUploadBytes);
                context->Unmap(lightsBuffer.get(), 0);
            }
#else
            D3D11_MAPPED_SUBRESOURCE lightMapped{};
            if (SUCCEEDED(context->Map(lightsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &lightMapped)))
            {
                std::memcpy(lightMapped.pData, frameLights.data(), lightUploadBytes);
                context->Unmap(lightsBuffer.get(), 0);
            }
#endif
        }

#if defined(FALLOUT_PRE_NG)
        if (currentLightCount > 0)
        {
            static std::atomic_uint32_t nonZeroClusterUploadCount = 0;
            const auto uploadIndex = ++nonZeroClusterUploadCount;
            if (uploadIndex <= 8 || uploadIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered prepass uploaded uploads={} frame={} lights={} clusters={}",
                             uploadIndex, frameNumber, currentLightCount,
                             clusterSize[0] * clusterSize[1] * clusterSize[2]);
            }
        }
#endif

        LightBuildingCB buildingCBData{};
        buildingCBData.LightsNear = 1.0f;
        buildingCBData.LightsFar = 333333.0f;
        buildingCBData.pad0[0] = buildingCBData.pad0[1] = 0;
        buildingCBData.ClusterSize[0] = clusterSize[0];
        buildingCBData.ClusterSize[1] = clusterSize[1];
        buildingCBData.ClusterSize[2] = clusterSize[2];
        buildingCBData.ClusterSize[3] = 0;
        std::memcpy(&buildingCBData.CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));

        bool rebuildClusterAABBs = true;
#if defined(FALLOUT_PRE_NG)
        rebuildClusterAABBs =
            !clusterBuildCacheValid || !PreNGClusterBuildInputsMatch(clusterBuildCache, buildingCBData);
        // The vanilla camera cb12 (rows 20..27) is captured on the FIRST DFLight
        // batch pass, which happens AFTER the first Prepass. Building AABBs
        // before that capture would reconstruct corners from an all-zero b12 and
        // then get cached as "valid", poisoning every later frame. Defer the
        // build until the capture exists, and never cache the empty result.
        if (rebuildClusterAABBs && !s_preNGDFLightCameraCB)
        {
            rebuildClusterAABBs = false;
            clusterBuildCacheValid = false;
        }
#endif

        if (rebuildClusterAABBs)
        {
            D3D11_MAPPED_SUBRESOURCE mapped;
            const auto hr = context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                LogResourceFailure("Map(lightBuildingCB)", hr);
                clearComputeBindings();
                return;
            }
            std::memcpy(mapped.pData, &buildingCBData, sizeof(buildingCBData));
            context->Unmap(lightBuildingCB.get(), 0);

            FO4CS_LLF_ZONE("LLF/ClusterBuild");
            context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
            ID3D11Buffer *cbPtr = lightBuildingCB.get();
            context->CSSetConstantBuffers(0, 1, &cbPtr);
#if defined(FALLOUT_PRE_NG)
            if (s_preNGDFLightCameraCB)
            {
                ID3D11Buffer *cameraCB = s_preNGDFLightCameraCB.get();
                context->CSSetConstantBuffers(12, 1, &cameraCB);
            }
#endif
            ID3D11UnorderedAccessView *buildingUAVs[] = {clustersUAV.get()};
            context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
            context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
#if defined(FALLOUT_PRE_NG)
            clusterBuildCache.LightsNear = buildingCBData.LightsNear;
            clusterBuildCache.LightsFar = buildingCBData.LightsFar;
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                clusterBuildCache.ClusterSize[i] = buildingCBData.ClusterSize[i];
            }
            clusterBuildCacheValid = true;

            static std::atomic_uint32_t clusterBuildRebuildCount = 0;
            const auto rebuildIndex = ++clusterBuildRebuildCount;
            if (rebuildIndex <= 8 || rebuildIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass rebuilt cluster AABBs rebuilds={} frame={} "
                             "clusters={} near={:.3f} far={:.1f}",
                             rebuildIndex, frameNumber, clusterSize[0] * clusterSize[1] * clusterSize[2], CameraNear,
                             CameraFar);
            }
#endif
            clearComputeBindings();
        }
#if defined(FALLOUT_PRE_NG)
        else
        {
            static std::atomic_uint32_t clusterBuildReuseCount = 0;
            const auto reuseIndex = ++clusterBuildReuseCount;
            if (reuseIndex <= 8 || reuseIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG clustered Prepass reused cluster AABBs reuses={} frame={} "
                             "clusters={} stableKey=near/far/cluster-size tolerance={}",
                             reuseIndex, frameNumber, clusterSize[0] * clusterSize[1] * clusterSize[2],
                             kPreNGClusterBuildReuseTolerance);
            }
        }
#endif

        {
            // Self time here is the culling CB map/fill/unmap, the second renderer-state
            // probe, and the SRV/UAV binds; ClusterCull is a nested child. The constant
            // buffer stays mapped across all of that.
            FO4CS_LLF_ZONE("LLF/CullSetup");
            D3D11_MAPPED_SUBRESOURCE mapped;
            const auto hr = context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(hr))
            {
                LogResourceFailure("Map(lightCullingCB)", hr);
                clearComputeBindings();
                return;
            }
            auto *cb = static_cast<LightCullingCB *>(mapped.pData);
            cb->LightCount = currentLightCount;
            cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
            cb->ClusterSize[0] = clusterSize[0];
            cb->ClusterSize[1] = clusterSize[1];
            cb->ClusterSize[2] = clusterSize[2];
            cb->ClusterSize[3] = 0;
            // Culling must use the SAME camera the positionWS[1] fill just used.
            // The TLS rows can advance between those two points, which puts the
            // light grid in a different space than both cb2[1] and the payload.
            // Reuse the snapshot taken above; fall back to a fresh read only if
            // that snapshot was never populated.
            {
#if defined(FALLOUT_PRE_NG)
                DirectX::XMFLOAT4X4 viewRows = preNGDFLightLastSnapshotViewRows;
                DirectX::XMFLOAT3 camPos{
                    preNGDFLightLastSnapshotCameraPos.x,
                    preNGDFLightLastSnapshotCameraPos.y,
                    preNGDFLightLastSnapshotCameraPos.z };
                bool valid = preNGDFLightLastSnapshotViewValid;
#else
                // No PreNG renderer-state snapshot exists on these runtimes, so the
                // CommonLibF4 camera state below is the only source. It is still the
                // same camera the positionWS[1] fill used, because that fill took the
                // identical fallback.
                DirectX::XMFLOAT4X4 viewRows{};
                DirectX::XMFLOAT3 camPos{};
                bool valid = false;
#endif
                if (!valid)
                {
#if defined(FALLOUT_PRE_NG)
                    const auto rendererBase = GetPreNGDFLightRendererStateBase();
                    valid = IsPreNGDFLightRendererStateReadable(rendererBase);
                    if (valid)
                    {
                        std::memcpy(&viewRows, reinterpret_cast<const void *>(rendererBase + 7024 + 114 * 16), sizeof(viewRows));
                        std::memcpy(&camPos, reinterpret_cast<const void *>(rendererBase + 8736), sizeof(camPos));
                    }
#endif
                }
                if (!valid)
                {
                    const auto &gfxState = RE::BSGraphics::State::GetSingleton();
                    viewRows = *reinterpret_cast<const DirectX::XMFLOAT4X4 *>(gfxState.cameraState.camViewData.viewMat);
                    const auto &p = gfxState.cameraState.posAdjust;
                    camPos = DirectX::XMFLOAT3{ p.x, p.y, p.z };
                }
                DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&viewRows);
                DirectX::XMFLOAT4X4 viewUpload{};
                DirectX::XMStoreFloat4x4(&viewUpload, DirectX::XMMatrixTranspose(view));
                std::memcpy(&cb->CameraView, &viewUpload, sizeof(viewUpload));
                cb->CameraPos = DirectX::XMFLOAT4{ camPos.x, camPos.y, camPos.z, 0.0f };
            }
            context->Unmap(lightCullingCB.get(), 0);

            ID3D11ShaderResourceView *cullingSRVs[] = {clustersSRV.get(), GetCurrentLightsSRV()};
            context->CSSetShaderResources(0, 2, cullingSRVs);

            ID3D11UnorderedAccessView *cullingUAVs[] = {lightIndexCounterUAV.get(), lightIndexListUAV.get(),
                                                        lightGridUAV.get()};
            context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

            FO4CS_LLF_ZONE("LLF/ClusterCull");
            context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
            ID3D11Buffer *cullCBPtr = lightCullingCB.get();
            context->CSSetConstantBuffers(0, 1, &cullCBPtr);

            context->Dispatch((clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
                              (clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
                              (clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
        }

        FO4CS_LLF_ZONE("LLF/Finalize");
        clearComputeBindings();

#if defined(FALLOUT_PRE_NG)
        if (gpuTimerSlot != UINT32_MAX)
        {
            EndPreNGClusterGpuTimer(context, gpuTimerSlot);
            ResolvePreNGClusterGpuTimer(context, gpuTimerSlot, frameNumber, currentLightCount);
        }
        clusterPayloadCache = preNGClusterPayloadCurrent;
        clusterPayloadCacheValid = true;
#endif
    }
    // Runs to the end of the function: the Prepass SRV bind and
    // TryBindPreNGBSLightingDeferredDescriptorResources, neither of which has been
    // measured yet.
    FO4CS_LLF_ZONE("LLF/Tail");
    frameLights.clear();

#if defined(FALLOUT_PRE_NG)
    if (ShouldBindPreNGPrepassResources())
    {
        if (ShouldBindPreNGClusterSRVs())
        {
            ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
            context->PSSetShaderResources(35, ARRAYSIZE(views), views);

            static std::atomic_uint32_t prepassBindCount = 0;
            const auto prepassBindIndex = ++prepassBindCount;
            if (prepassBindIndex <= 8 || prepassBindIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG cluster SRVs bound to PS t35-t37 from Prepass "
                             "binds={} frame={} lights={} clusters={}",
                             prepassBindIndex, frameNumber, currentLightCount,
                             clusterSize[0] * clusterSize[1] * clusterSize[2]);
            }
        }
    }
    TryBindPreNGBSLightingDeferredDescriptorResources(*this);
#else
    if (frameNumber >= 3 && currentLightCount > 0)
    {
        ID3D11ShaderResourceView *views[3]{GetCurrentLightsSRV(), lightIndexListSRV.get(), lightGridSRV.get()};
        context->PSSetShaderResources(35, ARRAYSIZE(views), views);

        if (frameNumber % 300 == 0)
        {
            logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)", currentLightCount,
                         clusterSize[0] * clusterSize[1] * clusterSize[2]);
        }
    }
#endif
}
