// LightLimitFix -- GPU resource lifetime.
//
// Shader compilation, every buffer/SRV/UAV this feature owns, the readiness
// predicate over them, the rotating lights-SRV accessor, the PreNG cluster GPU
// timestamp queries, and the pixel-shader slot teardown.
//
// Split out of src/Features/LightLimitFix.cpp per
// docs/refactor-outlines/outline-LightLimitFix.md (functions #2-4, #91, #94-96,
// #100, #101, #113).
//
// The three free helpers at the top were file-statics in the parent. They are
// declared in LLFInternal.h because the cluster prepass still calls
// LogResourceFailure and IsFiniteMatrix from the facade. None of the three owns
// a function-local static, so moving them changes no latch.

#include "Features/LightLimitFix.h"

#include "Features/LightLimit/LLFInternal.h"

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"

#include <DirectXMath.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace CommunityShaders::lightlimit
{
std::string GetShaderPath()
{
    return "LightLimitFix\\";
}

bool LogResourceFailure(const char *a_name, HRESULT a_hr)
{
    logger::error("[LightLimitFix] {} failed (hr=0x{:08X})", a_name, static_cast<std::uint32_t>(a_hr));
    return false;
}

bool IsFiniteMatrix(const DirectX::XMFLOAT4X4 &a_matrix)
{
    const auto *values = reinterpret_cast<const float *>(&a_matrix);
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (!std::isfinite(values[i]))
        {
            return false;
        }
    }
    return true;
}
} // namespace CommunityShaders::lightlimit

using namespace CommunityShaders::lightlimit;

void LightLimitFix::SetupResources()
{
    auto *device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
    if (!device)
    {
        logger::warn("[LightLimitFix] SetupResources: D3D11 device not available");
        return;
    }
#if defined(FALLOUT_PRE_NG)
    clusterBuildCacheValid = false;
    clusterBuildCache = {};
    clusterPayloadCacheValid = false;
    clusterPayloadCache = {};
    shadowSceneFastReuseValid = false;
    shadowSceneFastReuse = {};
#endif

    // com_ptr auto-releases previous resources on reassignment — no manual ClearShaderCache needed

    auto shaderPath = GetShaderPath();
#if defined(FALLOUT_PRE_NG)
    RunPreNGDFLightContractProbeCompileDiagnostic();
    RunPreNGDFLightFullShadowedCandidateCompileDiagnostic();
#endif

    auto compileOrLoad = [&](const char *a_name, winrt::com_ptr<ID3D11ComputeShader> &a_out) {
        auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(shaderPath + a_name);
        if (!compiled)
        {
            logger::warn("[LightLimitFix] Failed to compile: {}{}", shaderPath, a_name);
            return false;
        }
        const auto hr = device->CreateComputeShader(compiled->data(), compiled->size(), nullptr, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    if (!compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS) ||
        !compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS))
    {
        logger::warn("[LightLimitFix] GPU resources pending - compute shaders not available");
        return;
    }

    auto createBuffer = [&](const char *a_name, const D3D11_BUFFER_DESC &a_desc, winrt::com_ptr<ID3D11Buffer> &a_out) {
        const auto hr = device->CreateBuffer(&a_desc, nullptr, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    auto createSRV = [&](const char *a_name, ID3D11Resource *a_resource, const D3D11_SHADER_RESOURCE_VIEW_DESC &a_desc,
                         winrt::com_ptr<ID3D11ShaderResourceView> &a_out) {
        const auto hr = device->CreateShaderResourceView(a_resource, &a_desc, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    auto createUAV = [&](const char *a_name, ID3D11Resource *a_resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC &a_desc,
                         winrt::com_ptr<ID3D11UnorderedAccessView> &a_out) {
        const auto hr = device->CreateUnorderedAccessView(a_resource, &a_desc, a_out.put());
        if (FAILED(hr))
        {
            return LogResourceFailure(a_name, hr);
        }
        return true;
    };

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(LightBuildingCB);
        if (!createBuffer("CreateBuffer(lightBuildingCB)", desc, lightBuildingCB))
            return;
    }
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.ByteWidth = sizeof(LightCullingCB);
        if (!createBuffer("CreateBuffer(lightCullingCB)", desc, lightCullingCB))
            return;
    }

    // Lights structured buffers (triple-buffered dynamic + Map(DISCARD):
    // a single buffer forces the driver to serialize with the previous frame's
    // consumer draws, stalling the CPU ~12ms and lowering GPU utilisation).
#if defined(FALLOUT_PRE_NG)
    for (std::uint32_t i = 0; i < kPreNGLightsBufferFrames; ++i)
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightData);
        desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
        if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffers[i]))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = kMaxLights;
        if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffers[i].get(), srvDesc, lightsSRVs[i]))
            return;
    }
#else
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightData);
        desc.ByteWidth = static_cast<UINT>(kMaxLights * sizeof(LightData));
        if (!createBuffer("CreateBuffer(lightsBuffer)", desc, lightsBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = kMaxLights;
        if (!createSRV("CreateShaderResourceView(lightsSRV)", lightsBuffer.get(), srvDesc, lightsSRV))
            return;
    }
#endif

    // Clusters structured buffer
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ClusterAABB);
        desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(ClusterAABB));
        if (!createBuffer("CreateBuffer(clustersBuffer)", desc, clustersBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount;
        if (!createSRV("CreateShaderResourceView(clustersSRV)", clustersBuffer.get(), srvDesc, clustersSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount;
        if (!createUAV("CreateUnorderedAccessView(clustersUAV)", clustersBuffer.get(), uavDesc, clustersUAV))
            return;
    }

    // Light index counter
    {
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(std::uint32_t);
        desc.ByteWidth = sizeof(std::uint32_t);
        if (!createBuffer("CreateBuffer(lightIndexCounterBuffer)", desc, lightIndexCounterBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = 1;
        if (!createSRV("CreateShaderResourceView(lightIndexCounterSRV)", lightIndexCounterBuffer.get(), srvDesc,
                       lightIndexCounterSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = 1;
        if (!createUAV("CreateUnorderedAccessView(lightIndexCounterUAV)", lightIndexCounterBuffer.get(), uavDesc,
                       lightIndexCounterUAV))
            return;
    }

    // Light index list
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(std::uint32_t);
        desc.ByteWidth = static_cast<UINT>(clusterCount * kClusterMaxLights * sizeof(std::uint32_t));
        if (!createBuffer("CreateBuffer(lightIndexListBuffer)", desc, lightIndexListBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
        if (!createSRV("CreateShaderResourceView(lightIndexListSRV)", lightIndexListBuffer.get(), srvDesc,
                       lightIndexListSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
        if (!createUAV("CreateUnorderedAccessView(lightIndexListUAV)", lightIndexListBuffer.get(), uavDesc,
                       lightIndexListUAV))
            return;
    }

    // Light grid
    {
        std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(LightGrid);
        desc.ByteWidth = static_cast<UINT>(clusterCount * sizeof(LightGrid));
        if (!createBuffer("CreateBuffer(lightGridBuffer)", desc, lightGridBuffer))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = clusterCount;
        if (!createSRV("CreateShaderResourceView(lightGridSRV)", lightGridBuffer.get(), srvDesc, lightGridSRV))
            return;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = clusterCount;
        if (!createUAV("CreateUnorderedAccessView(lightGridUAV)", lightGridBuffer.get(), uavDesc, lightGridUAV))
            return;
    }

    if (!HasResources())
    {
        logger::error("[LightLimitFix] GPU resource creation finished with incomplete resources");
        return;
    }

    logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
                 clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

#if defined(FALLOUT_PRE_NG)
std::uint32_t LightLimitFix::BeginPreNGClusterGpuTimer(ID3D11DeviceContext *a_context, ID3D11Device *a_device)
{
    if (!ShouldTimePreNGClusterPrepassGpu() || !a_context || !a_device)
    {
        return UINT32_MAX;
    }

    if (!preNGClusterGpuTimersReady)
    {
        D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC tsDesc{D3D11_QUERY_TIMESTAMP, 0};
        bool ok = true;
        for (auto &timer : preNGClusterGpuTimers)
        {
            if (FAILED(a_device->CreateQuery(&disjointDesc, timer.disjoint.put())) ||
                FAILED(a_device->CreateQuery(&tsDesc, timer.begin.put())) ||
                FAILED(a_device->CreateQuery(&tsDesc, timer.end.put())))
            {
                ok = false;
                break;
            }
        }
        if (!ok)
        {
            static bool loggedFailure = false;
            if (!loggedFailure)
            {
                logger::warn("[LightLimitFix] PreNG GPU timing query creation failed; timing disabled this run");
                loggedFailure = true;
            }
            return UINT32_MAX;
        }
        preNGClusterGpuTimersReady = true;
    }

    const auto slot = preNGClusterGpuTimerIndex;
    auto &timer = preNGClusterGpuTimers[slot];
    a_context->Begin(timer.disjoint.get());
    a_context->End(timer.begin.get()); // timestamp queries record via End()
    return slot;
}

void LightLimitFix::EndPreNGClusterGpuTimer(ID3D11DeviceContext *a_context, std::uint32_t a_slot)
{
    if (a_slot >= kPreNGGpuTimerFrames || !a_context)
    {
        return;
    }
    auto &timer = preNGClusterGpuTimers[a_slot];
    a_context->End(timer.end.get());
    a_context->End(timer.disjoint.get());
    timer.pending = true;
    // Rotate to the other slot for the next timed frame so this frame's data has a
    // full frame to become available before we GetData it.
    preNGClusterGpuTimerIndex = (a_slot + 1) % kPreNGGpuTimerFrames;
}

void LightLimitFix::ResolvePreNGClusterGpuTimer(ID3D11DeviceContext *a_context, std::uint32_t a_slot,
                                                std::uint32_t a_frameNumber, std::uint32_t a_lightCount)
{
    if (a_slot >= kPreNGGpuTimerFrames || !a_context)
    {
        return;
    }

    // Read the OTHER buffer (previous timed frame) so GetData does not stall.
    const auto readSlot = (a_slot + 1) % kPreNGGpuTimerFrames;
    auto &timer = preNGClusterGpuTimers[readSlot];
    if (!timer.pending)
    {
        return;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
    if (a_context->GetData(timer.disjoint.get(), &disjointData, sizeof(disjointData), 0) != S_OK)
    {
        return; // not ready yet; try next timed frame
    }

    std::uint64_t beginTs = 0;
    std::uint64_t endTs = 0;
    if (a_context->GetData(timer.begin.get(), &beginTs, sizeof(beginTs), 0) != S_OK ||
        a_context->GetData(timer.end.get(), &endTs, sizeof(endTs), 0) != S_OK)
    {
        return;
    }

    timer.pending = false;

    if (disjointData.Disjoint || disjointData.Frequency == 0 || endTs <= beginTs)
    {
        return; // timing invalid this frame
    }

    const double ms = static_cast<double>(endTs - beginTs) * 1000.0 / static_cast<double>(disjointData.Frequency);
    logger::info("[LightLimitFix] PreNG cluster compute GPU time frame={} lights={} clusters={} gpuMs={:.3f}",
                 a_frameNumber, a_lightCount, clusterSize[0] * clusterSize[1] * clusterSize[2], ms);
}
#endif

bool LightLimitFix::HasResources() const
{
#if defined(FALLOUT_PRE_NG)
    for (std::uint32_t i = 0; i < kPreNGLightsBufferFrames; ++i)
    {
        if (!lightsBuffers[i] || !lightsSRVs[i])
        {
            return false;
        }
    }
#endif
    return clusterBuildingCS && clusterCullingCS && lightBuildingCB && lightCullingCB &&
#if !defined(FALLOUT_PRE_NG)
           lightsBuffer && lightsSRV &&
#endif
           clustersBuffer && clustersSRV && clustersUAV && lightIndexCounterBuffer && lightIndexCounterSRV &&
           lightIndexCounterUAV && lightIndexListBuffer && lightIndexListSRV && lightIndexListUAV && lightGridBuffer &&
           lightGridSRV && lightGridUAV;
}

ID3D11ShaderResourceView *LightLimitFix::GetCurrentLightsSRV()
{
#if defined(FALLOUT_PRE_NG)
    return lightsSRVs[currentLightsBufferIndex % kPreNGLightsBufferFrames].get();
#else
    // Was `return GetCurrentLightsSRV();` -- unconditional self-recursion, so any
    // call on these runtimes overflowed the stack. Only PreNG could ever have run
    // this code, because the other two variants did not compile. The single
    // lightsSRV declared in the #else half of LightLimitFix.h is the counterpart
    // to PreNG's rotating lightsSRVs ring.
    return lightsSRV.get();
#endif
}

void LightLimitFix::Reset()
{
    if (!HasResources())
        return;

    auto *rendererData = fo4cs::GetRendererData();
    if (!rendererData)
        return;
    auto *context = reinterpret_cast<ID3D11DeviceContext *>(rendererData->context);
    if (!context)
        return;

    ID3D11ShaderResourceView *nullViews[3]{};
    context->PSSetShaderResources(35, 3, nullViews);
}
