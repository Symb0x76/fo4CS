// LightLimitFix -- BSLight/NiLight decode, FNV hashing and the shadow-scene
// fast-reuse keys.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #5-14, #25, #26).
// Pure functions over game memory -- no function-local statics, so nothing
// latched moved with them.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include <DirectXMath.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
bool PreNGClusterBuildInputsMatch(const LightLimitFix::ClusterBuildCacheState &a_cached,
                                  const LightLimitFix::LightBuildingCB &a_current)
{
    if (std::fabs(a_cached.LightsNear - a_current.LightsNear) > kPreNGClusterBuildReuseTolerance ||
        std::fabs(a_cached.LightsFar - a_current.LightsFar) > kPreNGClusterBuildReuseTolerance)
    {
        return false;
    }

    for (std::uint32_t i = 0; i < 4; ++i)
    {
        if (a_cached.ClusterSize[i] != a_current.ClusterSize[i])
        {
            return false;
        }
    }

    // Projection jitter can change the inverse matrix every frame. For the
    // targeted DFLight refresh path, reuse built AABBs while near/far and
    // grid size are stable; SetupResources invalidates resource changes.
    return true;
}


void HashPreNGAppendBytes(std::uint64_t &a_hash, const void *a_data, std::size_t a_size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(a_data);
    for (std::size_t i = 0; i < a_size; ++i)
    {
        a_hash ^= bytes[i];
        a_hash *= kPreNGFNVPrime;
    }
}

std::uint64_t HashPreNGBytes(const void *a_data, std::size_t a_size)
{
    auto hash = kPreNGFNVOffsetBasis;
    HashPreNGAppendBytes(hash, a_data, a_size);
    return hash;
}

LightLimitFix::ClusterPayloadCacheState MakePreNGClusterPayloadCacheState(
    const std::vector<LightLimitFix::LightData> &a_lights, std::uint32_t a_lightCount,
    const DirectX::XMFLOAT4X4 &a_viewTransposed,
    float a_lightsNear, float a_lightsFar, const std::uint32_t (&a_clusterSize)[3])
{
    LightLimitFix::ClusterPayloadCacheState state{};
    state.LightsNear = a_lightsNear;
    state.LightsFar = a_lightsFar;
    state.ClusterSize[0] = a_clusterSize[0];
    state.ClusterSize[1] = a_clusterSize[1];
    state.ClusterSize[2] = a_clusterSize[2];
    state.ClusterSize[3] = 0;
    state.LightCount = a_lightCount;
    state.LightsHash = a_lights.empty()
                           ? kPreNGFNVOffsetBasis
                           : HashPreNGBytes(a_lights.data(), a_lights.size() * sizeof(LightLimitFix::LightData));
    state.ViewHash = HashPreNGBytes(&a_viewTransposed, sizeof(a_viewTransposed));
    return state;
}


bool SamePreNGShadowSceneFastReuseKey(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                      const LightLimitFix::ShadowSceneFastReuseKey &a_rhs)
{
    return a_lhs.Node == a_rhs.Node && a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
           a_lhs.CurrentIndex == a_rhs.CurrentIndex && a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
           a_lhs.UsedFallback == a_rhs.UsedFallback && a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
           a_lhs.ShadowEntries == a_rhs.ShadowEntries && a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
           a_lhs.ActiveCount == a_rhs.ActiveCount && a_lhs.ShadowCount == a_rhs.ShadowCount &&
           a_lhs.ExtraCount == a_rhs.ExtraCount && a_lhs.ActiveHash == a_rhs.ActiveHash &&
           a_lhs.ShadowHash == a_rhs.ShadowHash && a_lhs.ExtraHash == a_rhs.ExtraHash;
}

// Structural (pointer/hash-independent) fast-reuse comparison. The per-light
// wrapper pointers folded into ActiveHash/ShadowHash/ExtraHash churn every
// frame even for a static light set (the engine reorders/reallocates the
// wrapper array), so the strict SamePreNGShadowSceneFastReuseKey never
// matches for dense scenes and the decode re-runs every frame (sub-1-FPS in
// 1000+ light interiors). This weaker key matches when the same node exposes
// the same bucket layout and light counts, which together with a hard
// ReuseAge < refreshInterval gate bounds full re-decodes to once per
// refresh interval regardless of pointer churn.
bool SamePreNGShadowSceneFastReuseStructure(const LightLimitFix::ShadowSceneFastReuseKey &a_lhs,
                                            const LightLimitFix::ShadowSceneFastReuseKey &a_rhs)
{
    return a_lhs.Node == a_rhs.Node && a_lhs.SelectedIndex == a_rhs.SelectedIndex &&
           a_lhs.CurrentIndex == a_rhs.CurrentIndex && a_lhs.CurrentIndexRead == a_rhs.CurrentIndexRead &&
           a_lhs.UsedFallback == a_rhs.UsedFallback && a_lhs.ActiveEntries == a_rhs.ActiveEntries &&
           a_lhs.ShadowEntries == a_rhs.ShadowEntries && a_lhs.ExtraEntries == a_rhs.ExtraEntries &&
           a_lhs.ActiveCount == a_rhs.ActiveCount && a_lhs.ShadowCount == a_rhs.ShadowCount &&
           a_lhs.ExtraCount == a_rhs.ExtraCount;
}


bool ReadPreNGShadowSceneBucketHash(const F4Runtime::PreNGShadowSceneBucket &a_bucket, std::uint64_t &a_hash)
{
    a_hash = kPreNGFNVOffsetBasis;
    HashPreNGAppendBytes(a_hash, &a_bucket.entries, sizeof(a_bucket.entries));
    HashPreNGAppendBytes(a_hash, &a_bucket.count, sizeof(a_bucket.count));
    for (std::uint32_t i = 0; i < a_bucket.count; ++i)
    {
        std::uintptr_t wrapperAddress = 0;
        const auto entryAddress = a_bucket.entries + (static_cast<std::uintptr_t>(i) * sizeof(std::uintptr_t));
        ReadPreNGRaw(entryAddress, wrapperAddress);
        HashPreNGAppendBytes(a_hash, &wrapperAddress, sizeof(wrapperAddress));
    }
    return true;
}

bool MakePreNGShadowSceneFastReuseKey(const F4Runtime::PreNGShadowSceneNodeRef &a_nodeRef,
                                      const F4Runtime::PreNGShadowSceneBuckets &a_buckets,
                                      LightLimitFix::ShadowSceneFastReuseKey &a_key)
{
    LightLimitFix::ShadowSceneFastReuseKey key{};
    key.Node = a_nodeRef.node;
    key.SelectedIndex = a_nodeRef.selectedIndex;
    key.CurrentIndex = a_nodeRef.currentIndex;
    key.CurrentIndexRead = a_nodeRef.currentIndexRead;
    key.UsedFallback = a_nodeRef.usedFallback;
    key.ActiveEntries = a_buckets.active.entries;
    key.ShadowEntries = a_buckets.shadow.entries;
    key.ExtraEntries = a_buckets.extra.entries;
    key.ActiveCount = a_buckets.active.count;
    key.ShadowCount = a_buckets.shadow.count;
    key.ExtraCount = a_buckets.extra.count;
    if (!ReadPreNGShadowSceneBucketHash(a_buckets.active, key.ActiveHash) ||
        !ReadPreNGShadowSceneBucketHash(a_buckets.shadow, key.ShadowHash) ||
        !ReadPreNGShadowSceneBucketHash(a_buckets.extra, key.ExtraHash))
    {
        return false;
    }

    a_key = key;
    return true;
}

PreNGShadowSceneNodeRef GetPreNGWorldShadowSceneNode()
{
    return F4Runtime::GetPreNGWorldShadowSceneNode();
}

PreNGLightDecodeResult DecodePreNGBSLightWrapper(std::uintptr_t a_wrapperAddress, LightLimitFix::LightData &a_data,
                                                 std::uintptr_t &a_niLightAddress, bool &a_shadowMaskUnreadable,
                                                 bool &a_shadowMaskInvalid, std::uint32_t &a_shadowMaskBit)
{
    a_data = {};
    a_niLightAddress = 0;
    a_shadowMaskUnreadable = false;
    a_shadowMaskInvalid = false;
    a_shadowMaskBit = 0;

    float wrapperFade = 1.0f;
    if (a_wrapperAddress == 0 ||
        !ReadPreNGRawField(F4Runtime::PreNG::BS_LIGHT_WRAPPER_FADE, a_wrapperAddress, wrapperFade) ||
        !ReadPreNGRawField(F4Runtime::PreNG::BS_LIGHT_WRAPPER_NI_LIGHT, a_wrapperAddress, a_niLightAddress) ||
        a_niLightAddress == 0 || !std::isfinite(wrapperFade))
    {
        return PreNGLightDecodeResult::MissingWrapperData;
    }

    F4Runtime::PreNGNiLightData niLight{};
    const bool niLightRead =
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_DIFFUSE, a_niLightAddress, niLight.diffuse[0]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_DIFFUSE.address(a_niLightAddress) + sizeof(float), niLight.diffuse[1]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_DIFFUSE.address(a_niLightAddress) + (2 * sizeof(float)), niLight.diffuse[2]) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_RADIUS, a_niLightAddress, niLight.radius) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_DIMMER, a_niLightAddress, niLight.dimmer) &&
        ReadPreNGRawField(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE, a_niLightAddress, niLight.position[0]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE.address(a_niLightAddress) + sizeof(float), niLight.position[1]) &&
        ReadPreNGRaw(F4Runtime::PreNG::NI_LIGHT_WORLD_TRANSLATE.address(a_niLightAddress) + (2 * sizeof(float)), niLight.position[2]);
    if (!niLightRead || !std::isfinite(niLight.diffuse[0]) || !std::isfinite(niLight.diffuse[1]) ||
        !std::isfinite(niLight.diffuse[2]) || !std::isfinite(niLight.radius) || !std::isfinite(niLight.dimmer) ||
        !std::isfinite(niLight.position[0]) || !std::isfinite(niLight.position[1]) ||
        !std::isfinite(niLight.position[2]) || niLight.radius <= 0.0f)
    {
        return PreNGLightDecodeResult::InvalidNiLightData;
    }

    const float fade = niLight.dimmer * wrapperFade;
    const float contribution = (niLight.diffuse[0] + niLight.diffuse[1] + niLight.diffuse[2]) * fade;
    if (niLight.radius <= kPreNGLightRadiusThreshold || contribution <= kPreNGLightContributionThreshold)
    {
        return PreNGLightDecodeResult::NonContributingLightData;
    }

    a_data.color.x = niLight.diffuse[0];
    a_data.color.y = niLight.diffuse[1];
    a_data.color.z = niLight.diffuse[2];
    a_data.fade = fade;
    a_data.radius = niLight.radius;
    a_data.invRadius = a_data.radius > 0.0f ? 1.0f / a_data.radius : 0.0f;
    a_data.positionWS[0].data.x = niLight.position[0];
    a_data.positionWS[0].data.y = niLight.position[1];
    a_data.positionWS[0].data.z = niLight.position[2];
    a_data.lightFlags = static_cast<std::uint32_t>(LightLimitFix::LightFlags::Initialised);

    std::uint32_t shadowMaskIndex = kPreNGInvalidShadowLightMaskIndex;
    if (ReadPreNGRawField(F4Runtime::PreNG::BS_SHADOW_LIGHT_MASK_INDEX, a_wrapperAddress, shadowMaskIndex))
    {
        if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex && shadowMaskIndex < kPreNGMaxShadowLightMaskBits)
        {
            a_data.lightFlags |= static_cast<std::uint32_t>(LightLimitFix::LightFlags::Shadow);
            a_data.shadowLightIndex = shadowMaskIndex;
            a_shadowMaskBit = (1u << shadowMaskIndex);
        }
        else if (shadowMaskIndex != kPreNGInvalidShadowLightMaskIndex)
        {
            a_shadowMaskInvalid = true;
        }
    }
    else
    {
        a_shadowMaskUnreadable = true;
    }

    return PreNGLightDecodeResult::Decoded;
}
} // namespace CommunityShaders::lightlimit
#endif
