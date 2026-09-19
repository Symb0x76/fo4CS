// LightLimitFix -- every light-collection path.
//
// The render-pass light list, the PreNG BSRenderPass::sceneLights decode, the
// world ShadowSceneNode bucket walk with its fast-reuse cache, the vanilla
// cb2 staging read, the seenLights drain, the TESObjectREFR scene walk, and
// the SetupGeometry before/after pair that drives them.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #114-116,
// #139-143).
//
// CollectLightsFromPreNGShadowScene and CollectLightsFromPreNGSceneLights were
// two halves of one FALLOUT_PRE_NG region in the parent. Each gets its own
// guard here; the region is not one contiguous block any more, so an
// unguarded member would silently acquire a definition on PostNG/PostAE where
// its declaration does not exist.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"

#if defined(FALLOUT_POST_AE)
#include "RE/N/NiLight.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#include "RE/T/TESObjectREFR.h"
#else
#include "RE/Bethesda/TESBoundAnimObjects.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESObjectREFRs.h"
#include "RE/NetImmerse/NiLight.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>

// The members below are LightLimitFix:: members at global scope, and they name
// the cluster helpers and shared state unqualified exactly as they did in the
// parent translation unit.
using namespace CommunityShaders::lightlimit;

void LightLimitFix::CollectLightsFromPass(RE::BSRenderPass *a_pass)
{
    if (!a_pass)
        return;

    const RE::FO4Runtime::PreNGBSRenderPassView passView{a_pass};
    auto *lightData = passView.GetShaderPropertyLightData();
    if (!lightData || lightData->lightList.empty())
        return;

    for (auto *light : lightData->lightList)
    {
        if (!light || seenLights.contains(light))
            continue;
        seenLights.insert(light);
        seenThisPass.push_back(light);
    }
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::CollectLightsFromPreNGSceneLights(RE::BSRenderPass *a_pass,
                                                               std::uint32_t a_requestedLightCount,
                                                               std::uint32_t a_shadowArg)
{
    if (!a_pass)
    {
        return 0;
    }

    const auto passAddress = reinterpret_cast<std::uintptr_t>(a_pass);
    const F4Runtime::PreNGBSRenderPassView passView{a_pass};

    std::uintptr_t sceneLightsAddress = 0;
    std::uint8_t rawLightCount = 0;
    if (!passView.ReadSceneLights(sceneLightsAddress) || !passView.ReadRawLightCount(rawLightCount) ||
        sceneLightsAddress == 0 || rawLightCount <= kPreNGBSRenderPassSceneLightFirstIndex)
    {
        return 0;
    }

    // FO4 vanilla passes (pass->numLights - 1) into the point-light writer and
    // reads physical sceneLights entries starting at index 1.
    auto availableLightCount = static_cast<std::uint32_t>(rawLightCount - kPreNGBSRenderPassSceneLightFirstIndex);
    if (a_requestedLightCount < availableLightCount)
    {
        availableLightCount = a_requestedLightCount;
    }

    std::uint32_t collected = 0;
    std::uint32_t missingEntryCount = 0;
    std::uint32_t missingWrapperDataCount = 0;
    std::uint32_t invalidNiLightDataCount = 0;
    std::uint32_t inactiveLightDataCount = 0;
    std::uint32_t duplicateLightCount = 0;
    std::uint32_t unreadableShadowMaskCount = 0;
    std::uint32_t invalidShadowMaskCount = 0;

    for (std::uint32_t i = 0; i < availableLightCount && frameLights.size() < kMaxLights; ++i)
    {
        std::uintptr_t wrapperAddress = 0;
        if (!passView.ReadSceneLightWrapper(i, wrapperAddress) || wrapperAddress == 0)
        {
            ++missingEntryCount;
            continue;
        }

        LightData data{};
        std::uintptr_t niLightAddress = 0;
        bool shadowMaskUnreadable = false;
        bool shadowMaskInvalid = false;
        std::uint32_t shadowMaskBit = 0;
        const auto decodeResult = DecodePreNGBSLightWrapper(wrapperAddress, data, niLightAddress, shadowMaskUnreadable,
                                                            shadowMaskInvalid, shadowMaskBit);
        if (decodeResult == PreNGLightDecodeResult::MissingWrapperData)
        {
            ++missingWrapperDataCount;
            continue;
        }
        if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData)
        {
            ++invalidNiLightDataCount;
            continue;
        }
        if (decodeResult == PreNGLightDecodeResult::NonContributingLightData)
        {
            ++inactiveLightDataCount;
            continue;
        }

        if (shadowMaskUnreadable)
        {
            ++unreadableShadowMaskCount;
        }
        else if (shadowMaskInvalid)
        {
            ++invalidShadowMaskCount;
        }

        auto *lightKey = reinterpret_cast<RE::BSLight *>(niLightAddress);
        if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey))
        {
            ++duplicateLightCount;
            continue;
        }

        seenLights.insert(lightKey);
        seenThisPass.push_back(lightKey);
        frameLights.push_back(data);
        ++collected;
    }

    static std::atomic_uint32_t decodeDiagCount = 0;
    if (availableLightCount > 0 && (collected > 0 || missingEntryCount > 0 || missingWrapperDataCount > 0 ||
                                    invalidNiLightDataCount > 0 || inactiveLightDataCount > 0))
    {
        const auto diagIndex = ++decodeDiagCount;
        if (diagIndex <= 8 || diagIndex % 512 == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG scene-light decode pass=0x{:X} table=0x{:X} raw={} requested={} available={} "
                "collected={} shadowArg={} skips(entry={}, wrapper={}, niLight={}, "
                "inactive={}, duplicate={}, shadowMaskUnreadable={}, shadowMaskInvalid={})",
                passAddress, sceneLightsAddress, static_cast<std::uint32_t>(rawLightCount), a_requestedLightCount,
                availableLightCount, collected, a_shadowArg, missingEntryCount, missingWrapperDataCount,
                invalidNiLightDataCount, inactiveLightDataCount, duplicateLightCount, unreadableShadowMaskCount,
                invalidShadowMaskCount);
        }
    }

    return collected;
}
#endif

// PreNG only. Every member defined between here and the #endif below is
// declared inside a FALLOUT_PRE_NG block in LightLimitFix.h, so on the other
// runtimes these definitions have no matching declaration and the raw
// renderer-state offsets they read do not apply. The guard was missing, which
// is why PostNG and PostAE never compiled on this branch.
#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::CollectLightsFromPreNGShadowScene()
{
    std::uintptr_t activeLightsAddress = 0;
    std::uintptr_t activeShadowLightsAddress = 0;
    std::uintptr_t activeExtraLightsAddress = 0;
    std::uint32_t activeLightCount = 0;
    std::uint32_t activeShadowLightCount = 0;
    std::uint32_t activeExtraLightCount = 0;
    const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
    const auto shadowSceneNode = shadowSceneNodeRef.node;

    auto logFailure = [&](const char *a_reason) {
        shadowSceneFastReuseValid = false;
        shadowSceneFastReuse = {};
        static std::atomic_uint32_t failureCount = 0;
        const auto failureIndex = ++failureCount;
        if (failureIndex <= 8 || failureIndex % 512 == 0)
        {
            logger::warn(
                "[LightLimitFix] PreNG shadow-scene light decode held failures={} reason={} node=0x{:X} "
                "selectedIndex={} currentIndex={} currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}) "
                "shadow=(ptr=0x{:X}, count={}) extra=(ptr=0x{:X}, count={})",
                failureIndex, a_reason, shadowSceneNode, static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
                static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex), shadowSceneNodeRef.currentIndexRead,
                shadowSceneNodeRef.usedFallback, activeLightsAddress, activeLightCount, activeShadowLightsAddress,
                activeShadowLightCount, activeExtraLightsAddress, activeExtraLightCount);
        }
    };

    if (shadowSceneNode == 0)
    {
        logFailure("world-shadow-scene-node-unavailable");
        return 0;
    }

    F4Runtime::PreNGShadowSceneBuckets buckets{};
    const F4Runtime::PreNGShadowSceneNodeView shadowSceneView{shadowSceneNode};
    if (!shadowSceneView.ReadBuckets(buckets))
    {
        logFailure("arrays-unreadable");
        return 0;
    }
    activeLightsAddress = buckets.active.entries;
    activeLightCount = buckets.active.count;
    activeShadowLightsAddress = buckets.shadow.entries;
    activeShadowLightCount = buckets.shadow.count;
    activeExtraLightsAddress = buckets.extra.entries;
    activeExtraLightCount = buckets.extra.count;

    if (activeLightCount > kPreNGMaxShadowSceneActiveLights ||
        activeShadowLightCount > kPreNGMaxShadowSceneActiveLights ||
        activeExtraLightCount > kPreNGMaxShadowSceneActiveLights)
    {
        logFailure("count-out-of-range");
        return 0;
    }

    if ((activeLightCount > 0 && activeLightsAddress == 0) ||
        (activeShadowLightCount > 0 && activeShadowLightsAddress == 0) ||
        (activeExtraLightCount > 0 && activeExtraLightsAddress == 0))
    {
        logFailure("array-null");
        return 0;
    }

    const auto decodeActiveLightCount = std::min(activeLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto decodeActiveShadowLightCount = std::min(activeShadowLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto decodeActiveExtraLightCount = std::min(activeExtraLightCount, kPreNGMaxShadowSceneDecodeLights);
    const auto totalBucketCount = static_cast<std::uint64_t>(activeLightCount) +
                                  static_cast<std::uint64_t>(activeShadowLightCount) +
                                  static_cast<std::uint64_t>(activeExtraLightCount);
    const bool shadowSceneDecodeTruncated =
        activeLightCount != decodeActiveLightCount || activeShadowLightCount != decodeActiveShadowLightCount ||
        activeExtraLightCount != decodeActiveExtraLightCount || totalBucketCount > kPreNGMaxShadowSceneDecodeLights;
    if (shadowSceneDecodeTruncated)
    {
        static std::atomic_uint32_t truncationCount = 0;
        const auto truncationIndex = ++truncationCount;
        if (truncationIndex <= 8 || truncationIndex % 512 == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG shadow-scene decode truncating oversized buckets truncations={} active={}=>{} "
                "shadow={}=>{} extra={}=>{} total={} decodeCapacity={} hardMax={}; LLF payload remains bounded",
                truncationIndex, activeLightCount, decodeActiveLightCount, activeShadowLightCount,
                decodeActiveShadowLightCount, activeExtraLightCount, decodeActiveExtraLightCount, totalBucketCount,
                kPreNGMaxShadowSceneDecodeLights, kPreNGMaxShadowSceneActiveLights);
        }
    }

    ShadowSceneFastReuseKey fastReuseKey{};
    const bool fastReuseEnabled = ShouldReusePreNGShadowSceneFastReuse();
    const auto fastReuseRefreshInterval = fastReuseEnabled ? GetPreNGShadowSceneFastReuseRefreshInterval() : 0;
    bool fastReuseKeyReady = false;
    if (fastReuseEnabled)
    {
        fastReuseKeyReady = MakePreNGShadowSceneFastReuseKey(shadowSceneNodeRef, buckets, fastReuseKey);
        if (!fastReuseKeyReady)
        {
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
        else if (shadowSceneFastReuseValid &&
                 SamePreNGShadowSceneFastReuseStructure(shadowSceneFastReuse.Key, fastReuseKey) &&
                 shadowSceneFastReuse.ReuseAge < fastReuseRefreshInterval)
        {
            frameLights = shadowSceneFastReuse.Lights;
            ++shadowSceneFastReuse.ReuseAge;

            static std::atomic_uint32_t fastReuseCount = 0;
            const auto reuseIndex = ++fastReuseCount;
            if (reuseIndex <= 8 || reuseIndex % 128 == 0)
            {
                logger::info("[LightLimitFix] PreNG ShadowScene fast-reused decoded lights reuses={} age={} "
                             "refreshInterval={} node=0x{:X} active=(ptr=0x{:X}, count={}, hash=0x{:016X}) "
                             "shadow=(ptr=0x{:X}, count={}, hash=0x{:016X}) extra=(ptr=0x{:X}, count={}, "
                             "hash=0x{:016X}) lights={} lightsHash=0x{:016X}",
                             reuseIndex, shadowSceneFastReuse.ReuseAge, fastReuseRefreshInterval,
                             shadowSceneFastReuse.Key.Node, shadowSceneFastReuse.Key.ActiveEntries,
                             shadowSceneFastReuse.Key.ActiveCount, shadowSceneFastReuse.Key.ActiveHash,
                             shadowSceneFastReuse.Key.ShadowEntries, shadowSceneFastReuse.Key.ShadowCount,
                             shadowSceneFastReuse.Key.ShadowHash, shadowSceneFastReuse.Key.ExtraEntries,
                             shadowSceneFastReuse.Key.ExtraCount, shadowSceneFastReuse.Key.ExtraHash,
                             shadowSceneFastReuse.LightCount, shadowSceneFastReuse.LightsHash);
            }
            return shadowSceneFastReuse.LightCount;
        }
    }

    std::uint32_t collected = 0;
    std::uint32_t missingEntryCount = 0;
    std::uint32_t missingWrapperDataCount = 0;
    std::uint32_t invalidNiLightDataCount = 0;
    std::uint32_t inactiveLightDataCount = 0;
    std::uint32_t duplicateLightCount = 0;
    std::uint32_t unreadableShadowMaskCount = 0;
    std::uint32_t invalidShadowMaskCount = 0;

    auto decodeBucket = [&](const F4Runtime::PreNGShadowSceneBucket &a_bucket, std::uint32_t a_count) {
        for (std::uint32_t i = 0; i < a_count && frameLights.size() < kMaxLights; ++i)
        {
            std::uintptr_t wrapperAddress = 0;
            const auto entryAddress = a_bucket.entries + (static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t));
            ReadPreNGRaw(entryAddress, wrapperAddress);
            if (wrapperAddress == 0)
            {
                ++missingEntryCount;
                continue;
            }

            LightData data{};
            std::uintptr_t niLightAddress = 0;
            bool shadowMaskUnreadable = false;
            bool shadowMaskInvalid = false;
            std::uint32_t shadowMaskBit = 0;
            const auto decodeResult = DecodePreNGBSLightWrapper(wrapperAddress, data, niLightAddress,
                                                                shadowMaskUnreadable, shadowMaskInvalid, shadowMaskBit);
            if (decodeResult == PreNGLightDecodeResult::MissingWrapperData)
            {
                ++missingWrapperDataCount;
                continue;
            }
            if (decodeResult == PreNGLightDecodeResult::InvalidNiLightData)
            {
                ++invalidNiLightDataCount;
                continue;
            }
            if (decodeResult == PreNGLightDecodeResult::NonContributingLightData)
            {
                ++inactiveLightDataCount;
                continue;
            }

            if (shadowMaskUnreadable)
            {
                ++unreadableShadowMaskCount;
            }
            else if (shadowMaskInvalid)
            {
                ++invalidShadowMaskCount;
            }

            auto *lightKey = reinterpret_cast<RE::BSLight *>(niLightAddress);
            if (frameLights.size() >= kMaxLights || seenLights.contains(lightKey))
            {
                ++duplicateLightCount;
                continue;
            }

            seenLights.insert(lightKey);
            seenThisPass.push_back(lightKey);
            frameLights.push_back(data);
            ++collected;
        }
    };

    decodeBucket(buckets.active, decodeActiveLightCount);
    decodeBucket(buckets.shadow, decodeActiveShadowLightCount);
    decodeBucket(buckets.extra, decodeActiveExtraLightCount);

    static std::atomic_uint32_t decodeDiagCount = 0;
    const auto diagIndex = ++decodeDiagCount;
    if (diagIndex <= 8 || diagIndex % 512 == 0)
    {
        logger::info("[LightLimitFix] PreNG shadow-scene light decode node=0x{:X} selectedIndex={} currentIndex={} "
                     "currentIndexRead={} fallback={} active=(ptr=0x{:X}, count={}, decode={}) shadow=(ptr=0x{:X}, "
                     "count={}, decode={}) extra=(ptr=0x{:X}, count={}, decode={}) truncated={} collected={} "
                     "skips(entry={}, wrapper={}, niLight={}, inactive={}, duplicate={}, "
                     "shadowMaskUnreadable={}, shadowMaskInvalid={})",
                     shadowSceneNode, static_cast<std::uint32_t>(shadowSceneNodeRef.selectedIndex),
                     static_cast<std::uint32_t>(shadowSceneNodeRef.currentIndex), shadowSceneNodeRef.currentIndexRead,
                     shadowSceneNodeRef.usedFallback, activeLightsAddress, activeLightCount, decodeActiveLightCount,
                     activeShadowLightsAddress, activeShadowLightCount, decodeActiveShadowLightCount,
                     activeExtraLightsAddress, activeExtraLightCount, decodeActiveExtraLightCount,
                     shadowSceneDecodeTruncated, collected, missingEntryCount, missingWrapperDataCount,
                     invalidNiLightDataCount, inactiveLightDataCount, duplicateLightCount, unreadableShadowMaskCount,
                     invalidShadowMaskCount);
    }

    if (fastReuseEnabled)
    {
        const auto cachedLightCount = static_cast<std::uint32_t>(frameLights.size());
        if (fastReuseKeyReady && cachedLightCount > 0)
        {
            const auto lightsHash = frameLights.empty()
                                        ? kPreNGFNVOffsetBasis
                                        : HashPreNGBytes(frameLights.data(), frameLights.size() * sizeof(LightData));
            const bool stableWithPrevious =
                shadowSceneFastReuseValid && SamePreNGShadowSceneFastReuseKey(shadowSceneFastReuse.Key, fastReuseKey) &&
                shadowSceneFastReuse.LightCount == cachedLightCount &&
                shadowSceneFastReuse.LightsHash == lightsHash;

            auto stableDecodeCount = stableWithPrevious ? shadowSceneFastReuse.StableDecodeCount : 0u;
            if (stableDecodeCount != 0xFFFFFFFFu)
            {
                ++stableDecodeCount;
            }

            shadowSceneFastReuse.Key = fastReuseKey;
            shadowSceneFastReuse.Lights = frameLights;
            shadowSceneFastReuse.LightCount = cachedLightCount;
            shadowSceneFastReuse.LightsHash = lightsHash;
            shadowSceneFastReuse.StableDecodeCount = stableDecodeCount;
            shadowSceneFastReuse.ReuseAge = 0;
            shadowSceneFastReuseValid = true;

            static std::atomic_uint32_t fastReuseCaptureCount = 0;
            const auto captureIndex = ++fastReuseCaptureCount;
            if (captureIndex <= 8 || captureIndex % 512 == 0)
            {
                logger::info("[LightLimitFix] PreNG ShadowScene fast-reuse captured decode captures={} "
                             "stableDecodes={} refreshInterval={} node=0x{:X} lights={} "
                             "lightsHash=0x{:016X}",
                             captureIndex, shadowSceneFastReuse.StableDecodeCount, fastReuseRefreshInterval,
                             shadowSceneFastReuse.Key.Node, shadowSceneFastReuse.LightCount,
                             shadowSceneFastReuse.LightsHash);
            }
        }
        else
        {
            shadowSceneFastReuseValid = false;
            shadowSceneFastReuse = {};
        }
    }

    return collected;
}
#endif

void LightLimitFix::CollectLightCB()
{
    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
        return;
    auto *ctx = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    auto *device = reinterpret_cast<ID3D11Device *>(rendererData->device);
    if (!ctx || !device)
        return;

    ID3D11Buffer *lightCB = nullptr;
    ctx->PSGetConstantBuffers(2, 1, &lightCB);
    if (!lightCB)
        return;

    D3D11_BUFFER_DESC desc;
    lightCB->GetDesc(&desc);
    if (desc.ByteWidth < 48)
    {
        lightCB->Release();
        return;
    }

    D3D11_BUFFER_DESC stagingDesc{};
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.ByteWidth = desc.ByteWidth;

    ID3D11Buffer *stagingCB = nullptr;
    if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &stagingCB)))
    {
        lightCB->Release();
        return;
    }

    ctx->CopyResource(stagingCB, lightCB);
    lightCB->Release();

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        stagingCB->Release();
        return;
    }

    const float *rawData = static_cast<const float *>(mapped.pData);
    std::uint32_t lightCount = desc.ByteWidth / 48;
    if (lightCount > 4)
        lightCount = 4;

    for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++)
    {
        const float *l = rawData + i * 12;

        if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f)
            continue;

        auto cbHash = static_cast<std::uint64_t>(l[0] * 1000.0f) ^ (static_cast<std::uint64_t>(l[1] * 1000.0f) << 20) ^
                      (static_cast<std::uint64_t>(l[4] * 255.0f) << 40);

        if (seenCBHashes.contains(cbHash))
            continue;
        seenCBHashes.insert(cbHash);

        LightData data{};
        data.positionWS[0].data.x = l[0];
        data.positionWS[0].data.y = l[1];
        data.positionWS[0].data.z = l[2];
        data.radius = l[3];
        data.color.x = l[4];
        data.color.y = l[5];
        data.color.z = l[6];
        data.fade = l[7];
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }

    ctx->Unmap(stagingCB, 0);
    stagingCB->Release();
}

void LightLimitFix::CollectLightsFromBSLight()
{
    for (auto *light : seenLights)
    {
        if (!light || frameLights.size() >= kMaxLights)
            break;
        auto *niLight = reinterpret_cast<RE::NiLight *>(light);
        LightData data{};
        data.color.x = niLight->diff.r;
        data.color.y = niLight->diff.g;
        data.color.z = niLight->diff.b;
        data.fade = niLight->dimmer;
        data.radius = niLight->modelBound.fRadius;
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.positionWS[0].data.x = niLight->world.translate.x;
        data.positionWS[0].data.y = niLight->world.translate.y;
        data.positionWS[0].data.z = niLight->world.translate.z;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }
}

void LightLimitFix::CollectLightsFromScene()
{
    auto *dh = RE::TESDataHandler::GetSingleton();
    if (!dh)
        return;

    auto &refs = dh->GetFormArray<RE::TESObjectREFR>();
    for (auto *ref : refs)
    {
        if (!ref || frameLights.size() >= kMaxLights)
            break;

        auto *baseObj = ref->GetObjectReference();
        auto *lightForm = baseObj ? baseObj->As<RE::TESObjectLIGH>() : nullptr;
        if (!lightForm)
            continue;

        auto *niObj = ref->Get3D();
        if (!niObj)
            continue;

        auto *niLight = reinterpret_cast<RE::NiLight *>(niObj);
        if (!niLight)
            continue;

        auto *lightKey = reinterpret_cast<RE::BSLight *>(niLight);
        if (seenLights.contains(lightKey))
            continue;
        seenLights.insert(lightKey);

        LightData data{};
        data.color.x = niLight->diff.r;
        data.color.y = niLight->diff.g;
        data.color.z = niLight->diff.b;
        data.fade = niLight->dimmer;
        data.radius = niLight->modelBound.fRadius;
        data.invRadius = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
        data.positionWS[0].data.x = niLight->world.translate.x;
        data.positionWS[0].data.y = niLight->world.translate.y;
        data.positionWS[0].data.z = niLight->world.translate.z;
        data.lightFlags = static_cast<std::uint32_t>(LightFlags::Initialised);
        frameLights.push_back(data);
    }
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass * /*a_pass*/)
{
    seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass *a_pass)
{
#if defined(FALLOUT_PRE_NG)
    if (!TryReservePreNGSetupGeometryCall())
    {
        return;
    }
    if (!TryReservePreNGSetupGeometryFrameSample())
    {
        return;
    }

    const auto collected = CollectLightsFromPreNGSceneLights(a_pass);
    if (collected > 0)
    {
        static std::atomic_uint32_t setupGeometryCollectCount = 0;
        const auto collectIndex = ++setupGeometryCollectCount;
        if (collectIndex <= 8 || collectIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG SetupGeometry scene-light collection accepted samples={} pass=0x{:X} "
                         "collected={}",
                         collectIndex, reinterpret_cast<std::uintptr_t>(a_pass), collected);
        }
    }
    else
    {
        static std::atomic_uint32_t setupGeometryEmptyCount = 0;
        const auto emptyIndex = ++setupGeometryEmptyCount;
        if (emptyIndex <= 8 || emptyIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG SetupGeometry scene-light collection empty samples={} pass=0x{:X}",
                         emptyIndex, reinterpret_cast<std::uintptr_t>(a_pass));
        }
    }
#else
    CollectLightsFromPass(a_pass);
#endif
}
