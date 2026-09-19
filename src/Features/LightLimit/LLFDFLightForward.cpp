// LightLimitFix -- the DFLight forward consumer.
//
// The zero-output replacement pixel shader, the forward cb3 camera buffer, the
// one-shot capture of vanilla cb12, and the per-draw forward replacement.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #128-131).
//
// THIS IS THE VERIFIED CONSUMER PATH. HandlePreNGDFLightForwardBatchPostCall is
// reached from BSDFLightShader::SetupGeometry vfunc 7; RenderDoc confirmed it
// and it was visually confirmed 2026-08-16. See
// docs/llf-dflight-forward-consumer.md. Nothing here changed shape in the
// split -- the descriptor predicates, the raw BindShaders call and the per-frame
// dedup statics are transcribed as they were.
//
// s_preNGDFLightCameraCB is defined in LightLimitFix.cpp, not here, and is
// declared extern in LLFInternal.h: the capture below writes it and the
// clustered compute dispatch in LLFClusterPrepass.cpp reads it. One object.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"

#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif

#include <atomic>
#include <cstdint>
#include <mutex>

using namespace CommunityShaders::lightlimit;

// One FALLOUT_PRE_NG region in the parent spanned from the zero-shader state
// through the binding audit. Only the DFLight forward half lives here, so the
// guard is re-opened around exactly that half.
#if defined(FALLOUT_PRE_NG)
namespace
{
    struct PreNGDFLightForwardZeroShaderState
    {
        bool attempted = false;
        winrt::com_ptr<ID3D11PixelShader> shader;
        RE::BSGraphics::PixelShader entry{};
    };
    std::mutex s_preNGDFLightForwardZeroLock;
    PreNGDFLightForwardZeroShaderState s_preNGDFLightForwardZeroState;
}

RE::BSGraphics::PixelShader *GetPreNGDFLightForwardZeroPixelShader()
{
    std::scoped_lock lock(s_preNGDFLightForwardZeroLock);
    auto &state = s_preNGDFLightForwardZeroState;
    if (state.shader && state.entry.shader)
    {
        return std::addressof(state.entry);
    }
    if (state.attempted)
    {
        return nullptr;
    }
    state.attempted = true;

    auto *device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
    if (!device)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed reason=device-unavailable");
        return nullptr;
    }

    auto bytecode = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
        "LightLimitFix/DFLightZeroOutputPS.hlsl", "ps_5_0", nullptr, "main");
    if (!bytecode)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed reason=compile-failed");
        return nullptr;
    }

    ID3D11PixelShader *shader = nullptr;
    const auto hr = device->CreatePixelShader(bytecode->data(), bytecode->size(), nullptr, &shader);
    if (FAILED(hr) || !shader)
    {
        logger::warn("[LightLimitFix] PreNG DFLight forward zero PS create failed bytecode={} hr=0x{:08X}",
                     bytecode->size(), static_cast<std::uint32_t>(hr));
        return nullptr;
    }

    state.shader.attach(shader);
    state.entry.id = F4Runtime::PreNG::DF_LIGHT_FORWARD_PIXEL_DESCRIPTOR_8004;
    state.entry.shader = state.shader.get();
    logger::info("[LightLimitFix] PreNG DFLight forward zero PS created psD3D=0x{:X}",
                 reinterpret_cast<std::uintptr_t>(state.shader.get()));
    return std::addressof(state.entry);
}

void LightLimitFix::UpdatePreNGDFLightForwardCameraCB(
    const DirectX::XMFLOAT4X4 &a_viewMatrix,
    float a_cameraNear,
    float a_cameraFar)
{
    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    auto *device = runtime ? runtime->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    if (!dflightForwardCB)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 96;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, dflightForwardCB.put())))
        {
            logger::warn("[LightLimitFix] PreNG DFLight forward cb3 create failed");
            return;
        }
    }

    auto *rendererData = fo4cs::GetRendererData();
    auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
    if (!context)
    {
        return;
    }

    DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&a_viewMatrix);
    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, view);
    DirectX::XMFLOAT4X4 invViewTransposed;
    DirectX::XMStoreFloat4x4(&invViewTransposed, DirectX::XMMatrixTranspose(invView));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(dflightForwardCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        struct PreNGDFLightForwardCBData
        {
            float invView[4][4];
            float cameraNear;
            float cameraFar;
            float pad[2];
        };
        auto *data = static_cast<PreNGDFLightForwardCBData *>(mapped.pData);
        std::memcpy(data->invView, &invViewTransposed, sizeof(invViewTransposed));
        data->cameraNear = a_cameraNear;
        data->cameraFar = a_cameraFar;
        data->pad[0] = 0.0f;
        data->pad[1] = 0.0f;
        context->Unmap(dflightForwardCB.get(), 0);
    }
}

#if defined(FALLOUT_PRE_NG)
namespace
{
    struct PreNGDFLightCB12CaptureState
    {
        bool captured = false;
        winrt::com_ptr<ID3D11Buffer> staging;
    };
    std::mutex s_preNGDFLightCB12CaptureLock;
    PreNGDFLightCB12CaptureState s_preNGDFLightCB12CaptureState;
} // namespace

namespace CommunityShaders::lightlimit
{
    // Capture the vanilla DFLight camera cb12 (slot 12) on the first batch pass
    // where it is bound. ClusterBuildingCS reads rows 20..27 from this copy.
    //
    // This was file-static in the parent, which hid the fact that the caller is
    // the batch-setup thunk in LLFHooks.cpp, not this cluster. The capture state
    // above stays private here; only the entry point crosses.
    void CapturePreNGDFLightCameraCBOnce()
    {
        if (s_preNGDFLightCameraCBCaptured.load(std::memory_order_acquire))
        {
            return;
        }
        std::scoped_lock lock(s_preNGDFLightCB12CaptureLock);
        auto &state = s_preNGDFLightCB12CaptureState;
        if (state.captured)
        {
            return;
        }

        auto *rendererData = fo4cs::GetRendererData();
        auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
        auto *device = rendererData ? reinterpret_cast<ID3D11Device *>(rendererData->device) : nullptr;
        if (!context || !device)
        {
            return;
        }

        ID3D11Buffer *cb12 = nullptr;
        context->PSGetConstantBuffers(12, 1, &cb12);
        if (!cb12)
        {
            return;
        }

        D3D11_BUFFER_DESC desc{};
        cb12->GetDesc(&desc);
        cb12->Release();
        if (desc.ByteWidth == 0 || desc.ByteWidth > 4096)
        {
            return;
        }

        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = desc.ByteWidth;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, state.staging.put())))
        {
            return;
        }

        ID3D11Buffer *cb12Again = nullptr;
        context->PSGetConstantBuffers(12, 1, &cb12Again);
        if (!cb12Again)
        {
            return;
        }
        context->CopyResource(state.staging.get(), cb12Again);
        if (!s_preNGDFLightCameraCBCaptured.exchange(true, std::memory_order_relaxed))
        {
            s_preNGDFLightCameraCB.copy_from(cb12Again);
        }
        cb12Again->Release();
        state.captured = true;
    }

}
#endif

void LightLimitFix::HandlePreNGDFLightForwardBatchPostCall(RE::BSShader *a_shader)
{
    if (!ShouldBindPreNGDFLightForwardVisibleLLF() || !a_shader)
    {
        return;
    }
    if (a_shader->shaderType != static_cast<std::int32_t>(F4Runtime::PreNG::DF_LIGHTING_SHADER_TYPE))
    {
        return;
    }
    if (GetCachedPreNGBSLightingSetupGeometryPreviewReason() != 0)
    {
        // Menu 3D previews keep the vanilla BSLighting path; do not double-light.
        return;
    }

    const auto pixelState = ReadPreNGCurrentPixelShaderEntryState();
    if (!F4Runtime::PreNG::IsDFLightForwardPixelDescriptor(pixelState.id))
    {
        return;
    }

#if defined(FALLOUT_PRE_NG)
    // Never replace a vanilla pass while the camera cb12 used to build cluster
    // AABBs has not been captured yet; the clusters would be garbage/empty and
    // every vanilla point-light pass would be zeroed or mis-culled.
    if (!s_preNGDFLightCameraCB)
    {
        return;
    }
#endif

    auto *runtime = CommunityShaders::Runtime::GetSingleton();
    if (!runtime)
    {
        return;
    }

    static std::atomic_uint64_t s_lastDFLightConsumerBoundFrame = UINT64_MAX;
    static std::atomic_uint64_t s_lastDFLightConsumerAttemptFrame = UINT64_MAX;
    const auto boundFrame = s_lastDFLightConsumerBoundFrame.load(std::memory_order_relaxed);
    auto attemptFrame = s_lastDFLightConsumerAttemptFrame.load(std::memory_order_relaxed);
    const auto frame = runtime->GetFrameCount();

    RE::BSGraphics::PixelShader *pixelEntry = nullptr;
    bool bindConsumer = false;
    PreNGDFLightResourceBindingState consumerResourceState{};
    if (boundFrame == frame)
    {
        // Consumer already emitted this frame's clustered list; zero the
        // remaining vanilla point-light passes so they do not double-count.
        pixelEntry = GetPreNGDFLightForwardZeroPixelShader();
        if (!pixelEntry)
        {
            return;
        }
    }
    else if (attemptFrame != frame)
    {
        // First eligible item of the frame. Try once per frame; on failure the
        // whole frame stays vanilla (never zero passes without a consumer).
        if (!s_lastDFLightConsumerAttemptFrame.compare_exchange_strong(
                attemptFrame, frame, std::memory_order_relaxed))
        {
            return;
        }

        // Preflight: replacing vanilla point-light passes only makes sense when
        // the clustered payload is actually live. A held prepass (menu preview,
        // shadow-scene overload gate) leaves currentLightCount = 0; in that
        // case keep every pass vanilla so the scene is never blacked out.
        if (currentLightCount == 0)
        {
            static std::atomic_uint32_t emptyPayloadHoldCount = 0;
            const auto holdIndex = ++emptyPayloadHoldCount;
            if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
            {
                logger::info("[LightLimitFix] PreNG DFLight forward replacement held holds={} frame={} "
                             "reason=clustered-payload-empty lights={}",
                             holdIndex, frame, currentLightCount);
            }
            return;
        }
        // Vanilla cb2[1] matches the prepass camera payload; keep the grid in
        // that same space.
        consumerResourceState = BindPreNGDescriptorResourcesToPixelShader("DFLight forward preflight");
        if (!consumerResourceState.clusterSRVsBound)
        {
            static std::atomic_uint32_t resourceHoldCount = 0;
            const auto holdIndex = ++resourceHoldCount;
            if (holdIndex <= 8 || (holdIndex & (holdIndex - 1)) == 0)
            {
                logger::info("[LightLimitFix] PreNG DFLight forward replacement held holds={} frame={} "
                             "reason=cluster-srv-bind-failed lights={}",
                             holdIndex, frame, consumerResourceState.lightCount);
            }
            return;
        }

        pixelEntry = CommunityShaders::ShaderCache::GetSingleton()->GetPixelShader(*a_shader, pixelState.id);
        if (!pixelEntry)
        {
            return;
        }
        bindConsumer = true;
    }
    else
    {
        // Consumer compile/bind already failed earlier this frame; keep vanilla.
        return;
    }

    const auto vertexEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_VERTEX_SHADER_ENTRY.address());
    const auto hullEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_HULL_SHADER_ENTRY.address());
    const auto domainEntry = F4Runtime::ReadPointer(F4Runtime::PreNG::CURRENT_DOMAIN_SHADER_ENTRY.address());
    const auto pixelGlobal = F4Runtime::PreNG::CURRENT_PIXEL_SHADER_ENTRY.address();
    const auto bindAddr = F4Runtime::PreNG::BIND_SHADERS.address();
    if (!vertexEntry || !F4Runtime::IsReadableAddress(bindAddr, 16) ||
        !F4Runtime::IsWritableAddress(pixelGlobal, sizeof(std::uintptr_t)))
    {
        return;
    }
    if (!F4Runtime::WriteValue(pixelGlobal, reinterpret_cast<std::uintptr_t>(pixelEntry)))
    {
        return;
    }

    using PreNGBindShadersFn = void *(*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    auto bindShaders = reinterpret_cast<PreNGBindShadersFn>(bindAddr);
    bindShaders(
        F4Runtime::PreNG::RENDERER_STATE.address(),
        vertexEntry,
        hullEntry,
        domainEntry,
        reinterpret_cast<std::uintptr_t>(pixelEntry));

    if (bindConsumer)
    {
        auto *rendererData = fo4cs::GetRendererData();
        auto *context = rendererData ? reinterpret_cast<ID3D11DeviceContext *>(rendererData->context) : nullptr;
        if (context && dflightForwardCB)
        {
            ID3D11Buffer *cb = dflightForwardCB.get();
            context->PSSetConstantBuffers(3, 1, &cb);
        }

        s_lastDFLightConsumerBoundFrame.store(frame, std::memory_order_relaxed);
    }
    else
    {
        // Zero pass: the consumer already emitted this frame's clustered list.
    }
}
#endif
