// LightLimitFix -- preview-menu detection and consumer suppression.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #39-43, #105,
// #106, #122-127).
//
// FO4 performs the shader lookups that drive the visible LLF consumer bind
// only for menu 3D previews, which is why this cluster exists at all: the
// consumer must be suppressed while a blocking menu is open, then resumed
// deliberately so the recovery is observable in the log rather than implied.
// The resume-pending flags are read by the cluster prepass, so they live in
// LLFInternal.h as single objects.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"

#if defined(FALLOUT_PRE_NG)
#include "RE/Bethesda/IMenu.h"
#include "RE/Bethesda/UI.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string_view>

using namespace CommunityShaders::lightlimit;

#if defined(FALLOUT_PRE_NG)
namespace CommunityShaders::lightlimit
{
std::string_view DetectPreNGBSLightingResourceProofMenuBlock()
{
    auto *ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return {};
    }

    for (std::uint32_t i = 0; i < kPreNGBSLightingResourceProofBlockingMenus.size(); ++i)
    {
        const auto menu = kPreNGBSLightingResourceProofBlockingMenus[i];
        if (ui->GetMenuOpen(menu.data()))
        {
            s_preNGBSLightingPreviewMenuLastReason.store(i + 1, std::memory_order_relaxed);
            return menu;
        }
    }

    return {};
}

// Cheap O(1) probe of the active world ShadowSceneNode bucket totals — reads
// only the bucket count fields, never walks/decodes the light pointers. Used
// by the resume gate so it can tell whether the dense preview-menu scene has
// drained without depending on the (proof-gated) full decode updating the
// count, which would deadlock the gate.
std::uint32_t ProbePreNGShadowSceneBucketTotal()
{
    const auto shadowSceneNodeRef = GetPreNGWorldShadowSceneNode();
    if (shadowSceneNodeRef.node == 0)
    {
        return 0;
    }
    F4Runtime::PreNGShadowSceneBuckets buckets{};
    const F4Runtime::PreNGShadowSceneNodeView shadowSceneView{shadowSceneNodeRef.node};
    if (!shadowSceneView.ReadBuckets(buckets))
    {
        return 0;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(buckets.active.count) +
                                static_cast<std::uint64_t>(buckets.shadow.count) +
                                static_cast<std::uint64_t>(buckets.extra.count);
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(total, 0xFFFFFFFFull));
}

std::string_view GetPreNGBSLightingLastPreviewMenuReason()
{
    const auto reason = s_preNGBSLightingPreviewMenuLastReason.load(std::memory_order_relaxed);
    if (reason > 0 && reason <= kPreNGBSLightingResourceProofBlockingMenus.size())
    {
        return kPreNGBSLightingResourceProofBlockingMenus[reason - 1];
    }
    return "unknown-preview-menu";
}

bool ShouldDeferPreNGBSLightingResourceProofForMenu()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    const auto frame = runtime ? runtime->GetFrameCount() : 0;
    const auto menuBlock = DetectPreNGBSLightingResourceProofMenuBlock();

    auto logDefer = [&](const char *a_reason, std::uint64_t a_until) {
        const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
        if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG BSLighting resource proof deferred for UI menu/settle deferrals={} frame={} "
                "until={} reason={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
                deferralIndex, frame, a_until, a_reason, kPreNGBSLightingResourceProofMenuSettleFrames);
        }
    };

    if (!menuBlock.empty())
    {
        s_preNGBSLightingPreviewMenuResumePending.store(true, std::memory_order_relaxed);
        // Scoped suppression: hold the clustered Prepass / deferred b3-t35-t37
        // bind only while a fullscreen preview menu (Lockpicking/Examine) is
        // actually open. Detection is per-frame and live, so the moment the
        // menu closes this returns false again and the prepass resumes
        // immediately — no post-close settle delay (BOSS: resume clustered
        // prepass right after the menu closes). The separate descriptor-burst
        // path (ExtendPreNGBSLightingResourceProofDescriptorSettle) still uses
        // bypassUntil for its own small buffer. See
        // .codex/docs/preview-menu-prepass-suppression.md.
        logDefer(menuBlock.data(), frame);
        return true;
    }

    // Resume gate: a preview menu may have just closed, but the engine can
    // keep the dense world ShadowSceneNode selected for a few frames before
    // the scene returns to its normal light count. Resuming the clustered
    // prepass during that overload window is what caused the residual
    // post-close stutter. Hold until the raw bucket total drops back to a
    // normal scene size. Uses a cheap live probe (not the proof-gated decode
    // output) so the gate can never deadlock itself. This gates resume on
    // scene state, not a timer, and is not a permanent light cap.
    const auto liveBucketTotal = ProbePreNGShadowSceneBucketTotal();
    s_preNGShadowSceneLastBucketTotal.store(liveBucketTotal, std::memory_order_relaxed);
    static const bool disableOverloadGate = IsTruthyEnvironmentSwitch(kPreNGDisablePreviewOverloadGateEnv);
    if (!disableOverloadGate && liveBucketTotal >= kPreNGShadowScenePreviewOverloadLights)
    {
        logDefer("post-menu-light-overload", frame);
        return true;
    }

    if (runtime)
    {
        const auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
        if (frame < bypassUntil)
        {
            logDefer("post-BSLighting-resource-settle", bypassUntil);
            return true;
        }
    }

    return false;
}

void ExtendPreNGBSLightingResourceProofDescriptorSettle()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return;
    }

    const auto frame = runtime->GetFrameCount();
    const auto newBypassUntil = frame + kPreNGBSLightingResourceProofMenuSettleFrames;
    auto bypassUntil = s_preNGBSLightingResourceProofBypassUntilFrame.load(std::memory_order_relaxed);
    while (bypassUntil < newBypassUntil)
    {
        if (s_preNGBSLightingResourceProofBypassUntilFrame.compare_exchange_weak(
                bypassUntil, newBypassUntil, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            break;
        }
    }

    const auto deferralIndex = ++s_preNGBSLightingResourceProofBypassLogs;
    if (deferralIndex <= 8 || (deferralIndex & (deferralIndex - 1)) == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting resource proof deferred after descriptor burst deferrals={} "
                     "frame={} until={} settleFrames={}; clustered Prepass and deferred b3/t35-t37 bind stay held",
                     deferralIndex, frame, newBypassUntil, kPreNGBSLightingResourceProofMenuSettleFrames);
    }
}
} // namespace CommunityShaders::lightlimit

bool LightLimitFix::ShouldSuppressPreNGBSLightingVisibleConsumerForMenu() const
{
    if (ShouldAllowPreNGBSLightingConsumerBindInMenu())
    {
        return false;
    }

    const auto menuBlock = DetectPreNGBSLightingResourceProofMenuBlock();
    if (menuBlock.empty())
    {
        return false;
    }

    // Some preview menus do not execute the world EarlyPrepass/Prepass hooks at
    // all. Arm recovery from the consumer/point-light suppression path as well,
    // otherwise the menu can be correctly protected but the post-menu recovery
    // chain never emits its "resumed" and "recovery complete" proof markers.
    s_preNGBSLightingPreviewMenuResumePending.store(true, std::memory_order_relaxed);

    const auto suppressIndex = ++s_preNGBSLightingVisibleConsumerMenuSuppressLogs;
    if (suppressIndex <= 8 || (suppressIndex & (suppressIndex - 1)) == 0)
    {
        auto *runtime = CommunityShaders::Runtime::GetSingleton();
        const auto frame = runtime ? runtime->GetFrameCount() : 0;
        logger::info("[LightLimitFix] PreNG BSLighting visible consumer held for UI menu suppressions={} frame={} "
                     "reason={}; vanilla BSLighting preserved",
                     suppressIndex, frame, menuBlock);
    }

    return true;
}

void LightLimitFix::NotifyPreNGBSLightingVisibleConsumerResumeComplete()
{
    if (!s_preNGBSLightingPreviewMenuConsumerResumePending.exchange(false, std::memory_order_relaxed))
    {
        return;
    }

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    logger::info("[LightLimitFix] PreNG UI preview LLF recovery complete frame={} lastMenu={} llfConsumerComplete=true "
                 "clusterSRVs=true lights={}",
                 runtime ? runtime->GetFrameCount() : 0, GetPreNGBSLightingLastPreviewMenuReason(), currentLightCount);
}

// These three were file-scope free functions in the parent. LLFHooks.cpp and
// LLFDFLightForward.cpp call them, so they are declared in LLFInternal.h and
// must be defined in the namespace that declares them.
namespace CommunityShaders::lightlimit
{
const char *GetPreNGBSLightingSetupGeometryPreviewReasonName(std::uint32_t a_reason)
{
    if (a_reason > 0 && a_reason <= kPreNGBSLightingSetupGeometryPreviewMenus.size())
    {
        return kPreNGBSLightingSetupGeometryPreviewMenus[a_reason - 1].data();
    }

    if (a_reason == kPreNGBSLightingSetupGeometryWorkshopPreviewReason)
    {
        return "WorkshopMenu3D";
    }

    return "none";
}

std::uint32_t DetectPreNGBSLightingSetupGeometryPreviewReason()
{
    auto *ui = RE::UI::GetSingleton();
    if (!ui)
    {
        return 0;
    }

    for (std::uint32_t i = 0; i < kPreNGBSLightingSetupGeometryPreviewMenus.size(); ++i)
    {
        if (ui->GetMenuOpen(kPreNGBSLightingSetupGeometryPreviewMenus[i].data()))
        {
            return i + 1;
        }
    }

    const auto workshopMenu = ui->GetMenu<RE::WorkshopMenu>();
    if (workshopMenu.get() && workshopMenu->OnStack())
    {
        const bool hasWorkshop3DPreview =
            workshopMenu->displayGeometry.get() || !workshopMenu->displayItemModels.empty() ||
            workshopMenu->inv3DModelManager.itemBase || workshopMenu->inv3DModelManager.tempRef;
        if (hasWorkshop3DPreview)
        {
            return kPreNGBSLightingSetupGeometryWorkshopPreviewReason;
        }
    }

    return 0;
}

std::uint32_t GetCachedPreNGBSLightingSetupGeometryPreviewReason()
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return DetectPreNGBSLightingSetupGeometryPreviewReason();
    }

    const auto frame = runtime->GetFrameCount();
    const auto cachedFrame = s_preNGBSLightingSetupGeometryPreviewCacheFrame.load(std::memory_order_acquire);
    if (cachedFrame == frame)
    {
        return s_preNGBSLightingSetupGeometryPreviewCacheReason.load(std::memory_order_acquire);
    }

    const auto reason = DetectPreNGBSLightingSetupGeometryPreviewReason();
    s_preNGBSLightingSetupGeometryPreviewCacheReason.store(reason, std::memory_order_release);
    s_preNGBSLightingSetupGeometryPreviewCacheFrame.store(frame, std::memory_order_release);
    return reason;
}
} // namespace CommunityShaders::lightlimit


bool LightLimitFix::ShouldProcessPreNGBSLightingSetupGeometryProof() const
{
    if (ShouldBindPreNGSetupGeometryStrictLightCB() || ShouldPersistPreNGSetupGeometryStrictLightCB() ||
        ShouldUpdatePreNGStrictLightCB())
    {
        return true;
    }

    if (!ShouldBindPreNGBSLightingSetupGeometryResources())
    {
        return true;
    }

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return true;
    }

    const auto frame = runtime->GetFrameCount();
    const auto bypassUntil = s_preNGBSLightingSetupGeometryBypassUntilFrame.load(std::memory_order_relaxed);
    if (frame < bypassUntil)
    {
        const auto bypassIndex = ++s_preNGBSLightingSetupGeometryBypassLogs;
        if (bypassIndex <= 8 || (bypassIndex & (bypassIndex - 1)) == 0)
        {
            logger::info(
                "[LightLimitFix] PreNG BSLighting SetupGeometry proof bypass active bypasses={} frame={} until={}",
                bypassIndex, frame, bypassUntil);
        }
        return false;
    }

    return true;
}

LightLimitFix::PreNGDFLightResourceBindingState LightLimitFix::BindPreNGBSLightingSetupGeometryResources(
    RE::BSRenderPass *a_pass)
{
    PreNGDFLightResourceBindingState state{};
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;

    if (!ShouldBindPreNGBSLightingSetupGeometryResources())
    {
        return state;
    }
    if (ShouldSuppressPreNGBSLightingVisibleConsumerForMenu())
    {
        return state;
    }

    static std::atomic_bool setupGeometryResourceProofComplete = false;
    if (setupGeometryResourceProofComplete.load(std::memory_order_relaxed))
    {
        return state;
    }

    currentLightCount = static_cast<std::uint32_t>(frameLights.size());
    state.lightCount = currentLightCount;
    state.strictLightCount = 0;
    state.shadowBitMask = 0;
    if (currentLightCount == 0)
    {
        state.strictLightCount = 0;
        state.shadowBitMask = 0;
        ExtendPreNGBSLightingSetupGeometryBypassWindow();
        if (TryReservePreNGBSLightingSetupGeometryNoLightProbeFrame())
        {
            static std::atomic_uint32_t noLightFrameCount = 0;
            const auto noLightFrameIndex = ++noLightFrameCount;
            if (noLightFrameIndex <= 8 || (noLightFrameIndex & (noLightFrameIndex - 1)) == 0)
            {
                logger::info(
                    "[LightLimitFix] PreNG BSLighting SetupGeometry resource proof held for no-light frame frames={} "
                    "pass=0x{:X}; descriptor/resource demand remains idle until scene lights are collected",
                    noLightFrameIndex, reinterpret_cast<std::uintptr_t>(a_pass));
            }
        }
        return state;
    }

    const auto currentPixelShader = ReadPreNGCurrentPixelShaderEntryState();
    NotifyPreNGBSLightingLLFConsumerDescriptorObserved(0, currentPixelShader.id, currentPixelShader.d3dObject != 0,
                                                       currentPixelShader.d3dObject);

    if (!HasPreNGBSLightingDescriptorConsumerData())
    {
        static std::atomic_uint32_t pendingCount = 0;
        const auto pendingIndex = ++pendingCount;
        if (pendingIndex <= 8 || pendingIndex % 512 == 0)
        {
            logger::info("[LightLimitFix] PreNG BSLighting SetupGeometry resource bind pending payload pending={} "
                         "pass=0x{:X} currentPS=0x{:X} currentPSId=0x{:X} lights={}",
                         pendingIndex, reinterpret_cast<std::uintptr_t>(a_pass), currentPixelShader.d3dObject,
                         currentPixelShader.id, currentLightCount);
        }
        return state;
    }

    state = BindPreNGDescriptorResourcesToPixelShader("BSLighting SetupGeometry");

    static std::atomic_uint32_t auditCount = 0;
    const auto auditIndex = ++auditCount;
    if (auditIndex <= 8 || auditIndex % 512 == 0)
    {
        TracePreNGActiveLightingBindings("bslighting-setup-geometry-resource-bind",
                                         static_cast<std::int32_t>(F4Runtime::PreNG::BS_LIGHTING_SHADER_TYPE), 0,
                                         currentPixelShader.id, currentPixelShader.d3dObject != 0,
                                         currentPixelShader.d3dObject);
    }

    if (state.clusterSRVsBound &&
        !setupGeometryResourceProofComplete.exchange(true, std::memory_order_relaxed))
    {
        logger::info("[LightLimitFix] PreNG BSLighting SetupGeometry resource-only proof reached t35-t37 completion "
                     "on current BSLighting path; future SetupGeometry resource binds are held until a visible-safe "
                     "consumer is implemented");
    }

    return state;
}

// Per-draw visible-consumer bind driven by BSLighting SetupGeometry or the
// normal-world batched per-item setup (sub_1428C37A0). Swaps the current pixel
// shader to the ShaderCache consumer PS and re-asserts t35-t37.
void LightLimitFix::TryBindPreNGBSLightingVisibleConsumerFromSetupGeometry(
    RE::BSShader *a_shader,
    const char *a_sourceName)
{
    if (!ShouldBindPreNGBSLightingLLFVisibleConsumer() || !a_shader)
    {
        return;
    }
    if (!HasPreNGBSLightingDescriptorConsumerData())
    {
        return;
    }
    if (ShouldSuppressPreNGBSLightingVisibleConsumerForMenu())
    {
        return;
    }

    const auto pixelState = ReadPreNGCurrentPixelShaderEntryState();
    const auto pixelDescriptor = pixelState.id;
    if (!F4Runtime::PreNG::IsBSLightingContractPixelDescriptor(pixelDescriptor))
    {
        return;
    }

    auto *consumerShader =
        CommunityShaders::ShaderCache::GetSingleton()->GetPixelShader(*a_shader, pixelDescriptor);
    const auto consumerPSD3D = consumerShader ? reinterpret_cast<std::uintptr_t>(consumerShader->shader) : 0;
    const auto pixelEntry = reinterpret_cast<std::uintptr_t>(consumerShader);
    if (!consumerShader || consumerPSD3D == 0 || pixelEntry == 0)
    {
        return;
    }

    const auto vertexEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
    const auto hullEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
    const auto domainEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
    if (vertexEntry == 0)
    {
        return;
    }

    const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
    const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
    if (!F4Runtime::IsReadableAddress(bindAddr, 16) || !F4Runtime::IsWritableAddress(pixelGlobal, sizeof(std::uintptr_t)))
    {
        return;
    }
    if (!F4Runtime::WriteValue(pixelGlobal, pixelEntry))
    {
        return;
    }

    using PreNGBindShadersFn = void *(*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
    bindShaders(F4Runtime::PreNG::RENDERER_STATE.address(), vertexEntry, hullEntry, domainEntry, pixelEntry);

    BindPreNGDescriptorResourcesToPixelShader("BSLighting SetupGeometry consumer");

    static std::atomic_uint32_t bindCount = 0;
    const auto bindIndex = ++bindCount;
    if (bindIndex <= 8 || (bindIndex & (bindIndex - 1)) == 0)
    {
        logger::info("[LightLimitFix] PreNG BSLighting LLF consumer bound via {} binds={} shaderType={} "
                     "descriptor=0x{:X} llfConsumerComplete=true lights={}",
                     a_sourceName, bindIndex, static_cast<std::int32_t>(a_shader->shaderType), pixelDescriptor,
                     currentLightCount);
    }
}
#endif
