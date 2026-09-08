#include "Render/DX12SwapChain.h"

#include <cstring>

#include <d3dcompiler.h>
#include <directx/d3dx12.h>

#include "Render/DX12SwapChainInternal.h"

// HDR output path: converts the Rec.709 sRGB back buffer to Rec.2020 PQ when the
// swap chain is in an HDR colour space.
namespace fo4cs::render
{
constexpr const char* kColorSpaceShaderSource = R"_HDR_(
struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};
VSOutput VSMain(uint id : SV_VertexID) {
    VSOutput o;
    o.pos.x = (float)(id == 1 ? 3 : -1);
    o.pos.y = (float)(id == 2 ? -3 : 1);
    o.pos.z = 0;
    o.pos.w = 1;
    o.uv.x = o.pos.x * 0.5 + 0.5;
    o.uv.y = -o.pos.y * 0.5 + 0.5;
    return o;
}
cbuffer ColorSpaceCB : register(b0) {
    float cb_peakNits;
    uint cb_isHDR10;
};
float3 sRGBToLinear(float3 c) {
    float3 sel = step(c, 0.04045);
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), sel);
}
static const float3x3 Rec709ToRec2020 = {
    0.6274, 0.3293, 0.0433,
    0.0691, 0.9195, 0.0114,
    0.0164, 0.0880, 0.8956
};
static const float PQ_m1 = 0.1593017578125;
static const float PQ_m2 = 78.84375;
static const float PQ_c1 = 0.8359375;
static const float PQ_c2 = 18.8515625;
static const float PQ_c3 = 18.6875;
float3 LinearToPQ(float3 c, float peakNits) {
    c = max(c / peakNits, 0);
    float3 c1 = pow(c, PQ_m1);
    return pow((PQ_c1 + PQ_c2 * c1) / (1.0 + PQ_c3 * c1), PQ_m2);
}
Texture2D<float4> sourceTex : register(t0);
SamplerState pointSampler : register(s0);
float4 PSMain(VSOutput i) : SV_TARGET {
    float4 color = sourceTex.Sample(pointSampler, i.uv);
    float3 linear = sRGBToLinear(color.rgb);
    [branch] if (cb_isHDR10) {
        float3 rec2020 = mul(Rec709ToRec2020, linear);
        float3 pq = LinearToPQ(rec2020, cb_peakNits);
        return float4(pq, color.a);
    } else {
        return float4(linear, color.a);
    }
}
)_HDR_";

const char* const kColorSpaceShader = kColorSpaceShaderSource;
}

using fo4cs::render::kColorSpaceShader;

void DX12SwapChain::EnsureColorSpaceResources()
{
    if (colorSpacePSO)
    {
        return; // already created
    }

    // --- Root signature: root constants (b0) + descriptor table (t0) + static sampler ---
    CD3DX12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER params[2] = {};
    params[0].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                 D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.Init(static_cast<UINT>(std::size(params)), params, 1, &sampler,
                     D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    winrt::com_ptr<ID3DBlob> sigBlob, errorBlob;
    DX::ThrowIfFailed(
        D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, sigBlob.put(), errorBlob.put()));
    DX::ThrowIfFailed(d3d12Device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                                       IID_PPV_ARGS(&colorSpaceRootSig)));

    // --- Compile shader and create PSO ---
    winrt::com_ptr<ID3DBlob> vsBlob, psBlob;
    {
        winrt::com_ptr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(kColorSpaceShader, strlen(kColorSpaceShader), nullptr, nullptr, nullptr, "VSMain",
                                "vs_5_0", 0, 0, vsBlob.put(), errors.put());
        if (FAILED(hr))
        {
            const char *msg = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "unknown";
            logger::error("[DX12SwapChain] VS compile failed: {}", msg);
            DX::ThrowIfFailed(hr);
        }
    }
    {
        winrt::com_ptr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(kColorSpaceShader, strlen(kColorSpaceShader), nullptr, nullptr, nullptr, "PSMain",
                                "ps_5_0", 0, 0, psBlob.put(), errors.put());
        if (FAILED(hr))
        {
            const char *msg = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "unknown";
            logger::error("[DX12SwapChain] PS compile failed: {}", msg);
            DX::ThrowIfFailed(hr);
        }
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = colorSpaceRootSig.get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = swapChainDesc.Format;
    psoDesc.SampleDesc.Count = 1;
    DX::ThrowIfFailed(d3d12Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&colorSpacePSO)));

    // --- SRV descriptor heap (2 entries, one per backbuffer) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&colorSpaceSRVHeap)));
        colorSpaceSRVHandleSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = swapChainDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        for (int i = 0; i < 2; i++)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(colorSpaceSRVHeap->GetCPUDescriptorHandleForHeapStart(), i,
                                                 colorSpaceSRVHandleSize);
            d3d12Device->CreateShaderResourceView(swapChainBufferWrapped[i]->resource.get(), &srvDesc, handle);
        }
    }

    // --- RTV descriptor heap (2 entries, one per swapchain backbuffer) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        DX::ThrowIfFailed(d3d12Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&colorSpaceRTVHeap)));
        colorSpaceRTVHandleSize = d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        for (int i = 0; i < 2; i++)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(colorSpaceRTVHeap->GetCPUDescriptorHandleForHeapStart(), i,
                                                 colorSpaceRTVHandleSize);
            d3d12Device->CreateRenderTargetView(swapChainBuffers[i].get(), nullptr, handle);
        }
    }

    logger::info("[DX12SwapChain] Color space conversion resources created (fmt={})",
                 static_cast<uint32_t>(swapChainDesc.Format));
}

void DX12SwapChain::DestroyColorSpaceResources()
{
    colorSpaceRootSig = nullptr;
    colorSpacePSO = nullptr;
    colorSpaceSRVHeap = nullptr;
    colorSpaceRTVHeap = nullptr;
    colorSpaceCB = nullptr;
    colorSpaceSRVHandleSize = 0;
    colorSpaceRTVHandleSize = 0;
}
