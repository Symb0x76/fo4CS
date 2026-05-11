#include "Features/LightLimitFix.h"
#include <DirectXMath.h>
#include <cstring>

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCache.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_NG)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif
#if defined(FALLOUT_POST_NG)
#include "RE/B/BSFadeNode.h"
#else
#include "RE/Bethesda/BSFadeNode.h"
#endif
#if defined(FALLOUT_POST_NG)
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#else
#include "RE/Bethesda/TESObjectREFRs.h"  // PreNG uses plural
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESBoundAnimObjects.h"  // TESObjectLIGH
#endif
#if !defined(FALLOUT_PRE_NG)
#include "RE/N/NiLight.h"
#endif

#include "SimpleIni.h"

namespace
{
	constexpr std::uint32_t kClusterMaxLights = 128;
	constexpr std::uint32_t kMaxLights = 1024;

	std::string GetShaderPath()
	{
		return "Data\\Shaders\\LightLimitFix\\";
	}
}

void LightLimitFix::LoadSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	const auto path = GetSettingsPath();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		ini.LoadFile(path.string().c_str());
	}

	settings.EnableLightsVisualisation = ini.GetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	settings.LightsVisualisationMode = static_cast<std::uint32_t>(
		ini.GetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode)));
}

void LightLimitFix::SaveSettings()
{
	constexpr auto kSection = "Settings";
	constexpr auto kVizEnabled = "bEnableLightsVisualisation";
	constexpr auto kVizMode = "uLightsVisualisationMode";

	CSimpleIniA ini;
	ini.SetUnicode();

	ini.SetBoolValue(kSection, kVizEnabled, settings.EnableLightsVisualisation);
	ini.SetLongValue(kSection, kVizMode, static_cast<long>(settings.LightsVisualisationMode));

	const auto path = GetSettingsPath();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	ini.SaveFile(path.string().c_str());
}

void LightLimitFix::RestoreDefaultSettings()
{
	settings = {};
}

LightLimitFix::PerFrame LightLimitFix::GetCommonBufferData()
{
	PerFrame perFrame{};
	perFrame.EnableLightsVisualisation = settings.EnableLightsVisualisation;
	perFrame.LightsVisualisationMode = settings.LightsVisualisationMode;
	perFrame.ClusterSize[0] = clusterSize[0];
	perFrame.ClusterSize[1] = clusterSize[1];
	perFrame.ClusterSize[2] = clusterSize[2];
	return perFrame;
}

void LightLimitFix::SetupResources()
{
	auto* device = CommunityShaders::Runtime::GetSingleton()->GetDevice();
	if (!device) {
		logger::warn("[LightLimitFix] SetupResources: D3D11 device not available");
		return;
	}

	ClearShaderCache();

	auto shaderPath = GetShaderPath();

	auto compileOrLoad = [&](const char* a_name, ID3D11ComputeShader*& a_out) {
		auto fullPath = shaderPath + a_name;
		std::error_code ec;
		if (!std::filesystem::exists(fullPath, ec)) {
			logger::info("[LightLimitFix] Shader not found: {}", fullPath);
			return;
		}
		auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(fullPath);
		if (compiled.empty()) {
			logger::warn("[LightLimitFix] Failed to compile: {}", fullPath);
			return;
		}
		device->CreateComputeShader(compiled.data(), compiled.size(), nullptr, &a_out);
	};

	compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS);
	compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS);

	if (!clusterBuildingCS || !clusterCullingCS) {
		logger::info("[LightLimitFix] GPU resources pending — shaders not yet available");
		return;
	}

	// Constant buffers
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightBuildingCB);
		device->CreateBuffer(&desc, nullptr, &lightBuildingCB);
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightCullingCB);
		device->CreateBuffer(&desc, nullptr, &lightCullingCB);
	}

	// Lights structured buffer
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightData);
		desc.ByteWidth = kMaxLights * sizeof(LightData);
		device->CreateBuffer(&desc, nullptr, &lightsBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = kMaxLights;
		device->CreateShaderResourceView(lightsBuffer, &srvDesc, &lightsSRV);
	}

	// Clusters structured buffer (RW from building CS, read by culling CS)
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(ClusterAABB);
		desc.ByteWidth = clusterCount * sizeof(ClusterAABB);
		device->CreateBuffer(&desc, nullptr, &clustersBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		device->CreateShaderResourceView(clustersBuffer, &srvDesc, &clustersSRV);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		device->CreateUnorderedAccessView(clustersBuffer, &uavDesc, &clustersUAV);
	}

	// Light index counter (atomic append offset)
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = sizeof(std::uint32_t);
		device->CreateBuffer(&desc, nullptr, &lightIndexCounterBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = 1;
		device->CreateShaderResourceView(lightIndexCounterBuffer, &srvDesc, &lightIndexCounterSRV);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = 1;
		device->CreateUnorderedAccessView(lightIndexCounterBuffer, &uavDesc, &lightIndexCounterUAV);
	}

	// Light index list (appended per-cluster light indices)
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = clusterCount * kClusterMaxLights * sizeof(std::uint32_t);
		device->CreateBuffer(&desc, nullptr, &lightIndexListBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		device->CreateShaderResourceView(lightIndexListBuffer, &srvDesc, &lightIndexListSRV);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		device->CreateUnorderedAccessView(lightIndexListBuffer, &uavDesc, &lightIndexListUAV);
	}

	// Light grid (per-cluster offset + count)
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightGrid);
		desc.ByteWidth = clusterCount * sizeof(LightGrid);
		device->CreateBuffer(&desc, nullptr, &lightGridBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		device->CreateShaderResourceView(lightGridBuffer, &srvDesc, &lightGridSRV);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		device->CreateUnorderedAccessView(lightGridBuffer, &uavDesc, &lightGridUAV);
	}

	resourcesCreated = true;
	logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
	             clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

void LightLimitFix::ClearShaderCache()
{
	if (clusterBuildingCS) { clusterBuildingCS->Release(); clusterBuildingCS = nullptr; }
	if (clusterCullingCS) { clusterCullingCS->Release(); clusterCullingCS = nullptr; }
	if (lightBuildingCB) { lightBuildingCB->Release(); lightBuildingCB = nullptr; }
	if (lightCullingCB) { lightCullingCB->Release(); lightCullingCB = nullptr; }
	if (lightsBuffer) { lightsBuffer->Release(); lightsBuffer = nullptr; }
	if (lightsSRV) { lightsSRV->Release(); lightsSRV = nullptr; }
	if (clustersBuffer) { clustersBuffer->Release(); clustersBuffer = nullptr; }
	if (clustersSRV) { clustersSRV->Release(); clustersSRV = nullptr; }
	if (clustersUAV) { clustersUAV->Release(); clustersUAV = nullptr; }
	if (lightIndexCounterBuffer) { lightIndexCounterBuffer->Release(); lightIndexCounterBuffer = nullptr; }
	if (lightIndexCounterSRV) { lightIndexCounterSRV->Release(); lightIndexCounterSRV = nullptr; }
	if (lightIndexCounterUAV) { lightIndexCounterUAV->Release(); lightIndexCounterUAV = nullptr; }
	if (lightIndexListBuffer) { lightIndexListBuffer->Release(); lightIndexListBuffer = nullptr; }
	if (lightIndexListSRV) { lightIndexListSRV->Release(); lightIndexListSRV = nullptr; }
	if (lightIndexListUAV) { lightIndexListUAV->Release(); lightIndexListUAV = nullptr; }
	if (lightGridBuffer) { lightGridBuffer->Release(); lightGridBuffer = nullptr; }
	if (lightGridSRV) { lightGridSRV->Release(); lightGridSRV = nullptr; }
	if (lightGridUAV) { lightGridUAV->Release(); lightGridUAV = nullptr; }
	resourcesCreated = false;
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_NG)
	auto* setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iMagicLightMaxCount");
	if (setting) {
		setting->SetInt(0x7FFFFFFF);
		logger::info("[LightLimitFix] Unlocked magic light limit");
	}
#endif
}

void LightLimitFix::PostPostLoad()
{
	Hooks::Install();
}

void LightLimitFix::Prepass()
{
	if (!resourcesCreated) return;

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) return;

	// --- Read camera matrices from game state ---
	const auto& gState = RE::BSGraphics::State::GetSingleton();
	const auto& camView = gState.cameraState.camViewData;
	// camView.viewMat and camView.projMat are __m128[4] (row-major XMMATRIX)
	// HLSL cbuffer uses column-major — we transpose before storing.
	// Use SSE intrinsics to avoid DirectXMath dependency.

	// Read projection inverse for cluster building (unproject screen→view)
	DirectX::XMFLOAT4X4 projInvTransposed;
	{
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.projMat));
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
	}

	// Read view matrix for cluster culling (world→view transform)
	DirectX::XMFLOAT4X4 viewTransposed;
	{
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
	}


	// --- Upload lights collected last frame (via SetupGeometry hooks) ---
	CollectLightsFromScene();

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());
	if (currentLightCount > 0) {
		context->UpdateSubresource(lightsBuffer, 0, nullptr, frameLights.data(),
		                           static_cast<UINT>(frameLights.size() * sizeof(LightData)), 0);
	}

	// Diagnostic: log every 300 frames (~5 sec) to avoid spam
	if (++diagFrameCounter % 300 == 0) {
		logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}",
		             diagFrameCounter, currentLightCount,
		             clusterSize[0], clusterSize[1], clusterSize[2],
		             CameraNear, CameraFar);
	}

	// Reset collections for the new frame's SetupGeometry passes
	seenLights.clear();
	seenCBHashes.clear();
	frameLights.clear();

	// --- Clear light index counter ---
	const std::uint32_t zero = 0;
	context->UpdateSubresource(lightIndexCounterBuffer, 0, nullptr, &zero, sizeof(zero), 0);

	// --- Cluster building pass ---
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightBuildingCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto cb = static_cast<LightBuildingCB*>(mapped.pData);
		cb->LightsNear = CameraNear;
		cb->LightsFar = CameraFar;
		cb->pad0[0] = cb->pad0[1] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));
		context->Unmap(lightBuildingCB, 0);

		context->CSSetShader(clusterBuildingCS, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &lightBuildingCB);
		ID3D11UnorderedAccessView* buildingUAVs[] = { clustersUAV };
		context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
	}

	// --- Unbind UAVs before culling pass ---
	{
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// --- Cluster culling pass ---
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightCullingCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto cb = static_cast<LightCullingCB*>(mapped.pData);
		cb->LightCount = currentLightCount;
		cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraView, &viewTransposed, sizeof(viewTransposed));
		context->Unmap(lightCullingCB, 0);

		ID3D11ShaderResourceView* cullingSRVs[] = { clustersSRV, lightsSRV };
		context->CSSetShaderResources(0, 2, cullingSRVs);

		ID3D11UnorderedAccessView* cullingUAVs[] = { lightIndexCounterUAV, lightIndexListUAV, lightGridUAV };
		context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

		context->CSSetShader(clusterCullingCS, nullptr, 0);
		context->CSSetConstantBuffers(0, 1, &lightCullingCB);

		context->Dispatch(
			(clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
			(clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
			(clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
	}

	// --- Unbind compute resources ---
	{
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[3]{};
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// --- Bind output SRVs for pixel shader consumption ---
	// Disabled: slots 35-37 may conflict with Godrays / other effects.
	// Enable after verifying slot availability and deploying PS replacements.
#if 0
	ID3D11ShaderResourceView* views[3]{
		lightsSRV,
		lightIndexListSRV,
		lightGridSRV
	};
	context->PSSetShaderResources(35, ARRAYSIZE(views), views);
#endif
}

void LightLimitFix::Reset()
{
	if (!resourcesCreated) return;

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) return;

	ID3D11ShaderResourceView* nullViews[3]{};
	context->PSSetShaderResources(35, 3, nullViews);
}

void LightLimitFix::CollectLightsFromPass(RE::BSRenderPass* a_pass)
{
	if (!a_pass) return;

	auto* shaderProp = *reinterpret_cast<RE::BSShaderProperty**>(reinterpret_cast<std::uintptr_t>(a_pass) + 0x10);
	if (!shaderProp) return;

	auto* fadeNode = *reinterpret_cast<RE::BSFadeNode**>(reinterpret_cast<std::uintptr_t>(shaderProp) + 0x48);
	if (!fadeNode) return;

	auto* lightData = reinterpret_cast<RE::BSShaderPropertyLightData*>(reinterpret_cast<std::uintptr_t>(fadeNode) + 0x140);
	if (lightData->lightList.empty()) return;

	// Track which BSLight objects this pass sees.
	// Real light data is extracted from the D3D11 CB by CollectLightCB().
	for (auto* light : lightData->lightList) {
		if (!light || seenLights.contains(light)) continue;
		seenLights.insert(light);
		seenThisPass.push_back(light);
	}
}

void LightLimitFix::CollectLightCB()
{
	// Read the per-draw light constant buffer written by the game's SetupGeometry.
	// Slot 2 = PS register b2 (verified via IDA decompilation of buffer map functions).
	//
	// CB layout (192 bytes, 4 lights, 3 float4 per light):
	//   [0..15]:  pos.x,  pos.y,  pos.z,  radius       (float4)
	//   [16..31]: col.r,  col.g,  col.b,  intensity    (float4)
	//   [32..47]: dir.x,  dir.y,  dir.z,  spotCutoff   (float4)
	//   ...repeat 4x...

	auto* rendererData = fo4cs::GetRendererData();
	auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!ctx || !device) return;

	// Get the PS constant buffer at slot 2
	ID3D11Buffer* lightCB = nullptr;
	ctx->PSGetConstantBuffers(2, 1, &lightCB);
	if (!lightCB) return;

	// Get buffer description for size
	D3D11_BUFFER_DESC desc;
	lightCB->GetDesc(&desc);
	if (desc.ByteWidth < 48) { lightCB->Release(); return; }

	// Create staging buffer for CPU readback
	D3D11_BUFFER_DESC stagingDesc{};
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.ByteWidth = desc.ByteWidth;

	ID3D11Buffer* stagingCB = nullptr;
	if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, &stagingCB))) {
		lightCB->Release();
		return;
	}

	ctx->CopyResource(stagingCB, lightCB);
	lightCB->Release();

	// Map staging buffer
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped))) {
		stagingCB->Release();
		return;
	}

	const float* rawData = static_cast<const float*>(mapped.pData);
	std::uint32_t lightCount = desc.ByteWidth / 48;
	if (lightCount > 4) lightCount = 4;

	for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++) {
		const float* l = rawData + i * 12;  // 3 float4 = 12 floats per light

		// Skip if position is zero (unused slot)
		if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f) continue;

		// Build a lookup key from the CB data (position+color hash)
		auto cbHash = static_cast<std::uint64_t>(l[0] * 1000.0f) ^
		              (static_cast<std::uint64_t>(l[1] * 1000.0f) << 20) ^
		              (static_cast<std::uint64_t>(l[4] * 255.0f) << 40);
		
		if (seenCBHashes.contains(cbHash)) continue;
		seenCBHashes.insert(cbHash);

		LightData data{};
		data.positionWS[0].data.x = l[0];
		data.positionWS[0].data.y = l[1];
		data.positionWS[0].data.z = l[2];
		data.radius       = l[3];
		data.color.x      = l[4];
		data.color.y      = l[5];
		data.color.z      = l[6];
		data.fade         = l[7];
		data.invRadius    = data.radius > 0.0f ? 1.0f / data.radius : 0.0f;
		data.lightFlags   = static_cast<std::uint32_t>(LightFlags::Initialised);
		frameLights.push_back(data);
	}

	ctx->Unmap(stagingCB, 0);
	stagingCB->Release();
}

void LightLimitFix::CollectLightsFromScene()
{
#if !defined(FALLOUT_PRE_NG)
	// PostNG/PostAE: NiLight scene traversal using fully defined headers.
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return;

	auto& refs = dh->GetFormArray<RE::TESObjectREFR>();
	for (auto* ref : refs) {
		if (!ref || frameLights.size() >= kMaxLights) break;

		auto* baseObj = ref->GetObjectReference();
		auto* lightForm = baseObj ? baseObj->As<RE::TESObjectLIGH>() : nullptr;
		if (!lightForm) continue;

		auto* niObj = ref->Get3D();
		if (!niObj) continue;

		auto* niLight = static_cast<RE::NiLight*>(niObj);
		if (!niLight) continue;

		auto* lightKey = reinterpret_cast<RE::BSLight*>(niLight);
		if (seenLights.contains(lightKey)) continue;
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
#else
	// PreNG: lights collected incrementally via CollectLightsFromPass + CollectLightCB.
#endif
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass* /*a_pass*/)
{
	seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass* a_pass)
{
	// Collect BSLight pointers from the pass
	CollectLightsFromPass(a_pass);
	// For PreNG (no NiLight headers): read game's light CB for actual data
#if defined(FALLOUT_PRE_NG)
	if (!seenThisPass.empty()) {
		CollectLightCB();
	}
#endif
}

namespace RE::VTABLE
{
}

void LightLimitFix::Hooks::Install()
{
	stl::write_vfunc<0x7, BSLightingShader_SetupGeometry>(RE::VTABLE::BSLightingShader[0]);
	stl::write_vfunc<0x7, BSEffectShader_SetupGeometry>(RE::VTABLE::BSEffectShader[0]);
	logger::info("[LightLimitFix] Installed SetupGeometry hooks (vfunc index 7)");
}

void LightLimitFix::Hooks::BSLightingShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags)
{
	auto& self = globals::features::lightLimitFix;
	self.SetupGeometryBefore(a_pass);
	func(a_this, a_pass, a_renderFlags);
	self.SetupGeometryAfter(a_pass);
}

void LightLimitFix::Hooks::BSEffectShader_SetupGeometry::thunk(
	RE::BSShader* a_this, RE::BSRenderPass* a_pass, std::uint32_t a_renderFlags)
{
	func(a_this, a_pass, a_renderFlags);
	auto& self = globals::features::lightLimitFix;
	self.SetupGeometryBefore(a_pass);
	self.SetupGeometryAfter(a_pass);
}
