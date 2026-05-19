#include "Features/LightLimitFix.h"
#include <DirectXMath.h>
#include <cstring>

#include "Core/CommunityShaders.h"
#include "Core/Globals.h"
#include "Core/ShaderCompiler.h"
#include "Core/State.h"
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSGraphics.h"
#else
#include "RE/Bethesda/BSGraphics.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/B/BSFadeNode.h"
#else
#include "RE/Bethesda/BSFadeNode.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectLIGH.h"
#else
#include "RE/Bethesda/TESObjectREFRs.h"
#include "RE/Bethesda/TESDataHandler.h"
#include "RE/Bethesda/TESBoundAnimObjects.h"
#endif
#if defined(FALLOUT_POST_AE)
#include "RE/N/NiAVObject.h"
#include "RE/N/NiBound.h"
#include "RE/N/NiColor.h"
#else
#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiBound.h"
#include "RE/NetImmerse/NiColor.h"
#endif

#include "SimpleIni.h"

#include <imgui.h>

namespace
{
	constexpr std::uint32_t kClusterMaxLights = 128;
	constexpr std::uint32_t kMaxLights = 1024;

	std::string GetShaderPath()
	{
		return "LightLimitFix\\";
	}

#pragma warning(push)
#pragma warning(disable: 4324)
	struct NiLightView : RE::NiAVObject
	{
		RE::NiColor amb;
		RE::NiColor diff;
		RE::NiColor spec;
		float dimmer;
		alignas(16) RE::NiBound modelBound;
		void* rendererData;
	};
#pragma warning(pop)

	static_assert(sizeof(NiLightView) == 0x170);
	static_assert(offsetof(NiLightView, diff) == 0x12C);
	static_assert(offsetof(NiLightView, modelBound) == 0x150);
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

void LightLimitFix::DrawSettings()
{
	if (ImGui::CollapsingHeader("Light Limit Fix")) {
		int changed = 0;
		changed |= ImGui::Checkbox("Lights Visualisation", &settings.EnableLightsVisualisation) ? 1 : 0;

		const char* modes[] = { "Clusters", "Lights", "Both" };
		int mode = static_cast<int>(settings.LightsVisualisationMode);
		if (ImGui::Combo("Visualisation Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
			settings.LightsVisualisationMode = static_cast<std::uint32_t>(std::clamp(mode, 0, IM_ARRAYSIZE(modes) - 1));
			changed = 1;
		}

		ImGui::Text("Lights: %u", currentLightCount);
		ImGui::Text("Clusters: %ux%ux%u", clusterSize[0], clusterSize[1], clusterSize[2]);

		if (changed) {
			SaveSettings();
		}
	}
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

	// com_ptr auto-releases previous resources on reassignment — no manual ClearShaderCache needed

	auto shaderPath = GetShaderPath();

	auto compileOrLoad = [&](const char* a_name, winrt::com_ptr<ID3D11ComputeShader>& a_out) {
		auto compiled = CommunityShaders::ShaderCompiler::GetSingleton()->CompileFromFile(
			shaderPath + a_name);
		if (!compiled) {
			logger::warn("[LightLimitFix] Failed to compile: {}{}", shaderPath, a_name);
			return;
		}
		device->CreateComputeShader(compiled->data(), compiled->size(), nullptr, a_out.put());
	};

	compileOrLoad("clusterBuildingCS.hlsl", clusterBuildingCS);
	compileOrLoad("clusterCullingCS.hlsl", clusterCullingCS);

	if (!HasResources()) {
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
		device->CreateBuffer(&desc, nullptr, lightBuildingCB.put());
	}
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.ByteWidth = sizeof(LightCullingCB);
		device->CreateBuffer(&desc, nullptr, lightCullingCB.put());
	}

	// Lights structured buffer
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightData);
		desc.ByteWidth = kMaxLights * sizeof(LightData);
		device->CreateBuffer(&desc, nullptr, lightsBuffer.put());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = kMaxLights;
		device->CreateShaderResourceView(lightsBuffer.get(), &srvDesc, lightsSRV.put());
	}

	// Clusters structured buffer
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(ClusterAABB);
		desc.ByteWidth = clusterCount * sizeof(ClusterAABB);
		device->CreateBuffer(&desc, nullptr, clustersBuffer.put());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		device->CreateShaderResourceView(clustersBuffer.get(), &srvDesc, clustersSRV.put());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		device->CreateUnorderedAccessView(clustersBuffer.get(), &uavDesc, clustersUAV.put());
	}

	// Light index counter
	{
		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = sizeof(std::uint32_t);
		device->CreateBuffer(&desc, nullptr, lightIndexCounterBuffer.put());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = 1;
		device->CreateShaderResourceView(lightIndexCounterBuffer.get(), &srvDesc, lightIndexCounterSRV.put());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = 1;
		device->CreateUnorderedAccessView(lightIndexCounterBuffer.get(), &uavDesc, lightIndexCounterUAV.put());
	}

	// Light index list
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		desc.ByteWidth = clusterCount * kClusterMaxLights * sizeof(std::uint32_t);
		device->CreateBuffer(&desc, nullptr, lightIndexListBuffer.put());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		device->CreateShaderResourceView(lightIndexListBuffer.get(), &srvDesc, lightIndexListSRV.put());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount * kClusterMaxLights;
		device->CreateUnorderedAccessView(lightIndexListBuffer.get(), &uavDesc, lightIndexListUAV.put());
	}

	// Light grid
	{
		std::uint32_t clusterCount = clusterSize[0] * clusterSize[1] * clusterSize[2];

		D3D11_BUFFER_DESC desc{};
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(LightGrid);
		desc.ByteWidth = clusterCount * sizeof(LightGrid);
		device->CreateBuffer(&desc, nullptr, lightGridBuffer.put());

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = clusterCount;
		device->CreateShaderResourceView(lightGridBuffer.get(), &srvDesc, lightGridSRV.put());

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = clusterCount;
		device->CreateUnorderedAccessView(lightGridBuffer.get(), &uavDesc, lightGridUAV.put());
	}

	logger::info("[LightLimitFix] GPU resources created ({} clusters, {} max lights)",
	             clusterSize[0] * clusterSize[1] * clusterSize[2], kMaxLights);
}

void LightLimitFix::DataLoaded()
{
#if defined(FALLOUT_POST_AE)
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
#if defined(FALLOUT_PRE_NG)
	++diagFrameCounter;

	if (diagFrameCounter < 5) {
		if (diagFrameCounter == 1)
			logger::info("[LightLimitFix] gate: waiting for frame 5 (current: {})", diagFrameCounter);
		return;
	}

	if (!HasResources()) {
		if (diagFrameCounter == 5)
			logger::warn("[LightLimitFix] gate: resources not created at frame {}", diagFrameCounter);
		return;
	}

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData || !rendererData->context) {
		if (diagFrameCounter % 300 == 0)
			logger::warn("[LightLimitFix] gate: D3D context unavailable at frame {}", diagFrameCounter);
		return;
	}
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);

	const auto& gState = RE::BSGraphics::State::GetSingleton();
	const auto& camView = gState.cameraState.camViewData;
	if (camView.projMat[0].m128_f32[0] == 0.0f &&
	    camView.projMat[0].m128_f32[1] == 0.0f &&
	    camView.projMat[0].m128_f32[2] == 0.0f) {
		if (diagFrameCounter % 300 == 0)
			logger::warn("[LightLimitFix] gate: camera proj uninitialized at frame {}", diagFrameCounter);
		return;
	}

	// Camera matrices
	DirectX::XMFLOAT4X4 projInvTransposed;
	{
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.projMat));
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
	}
	DirectX::XMFLOAT4X4 viewTransposed;
	{
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
	}

	// Light collection
	if (!seenLights.empty())
		CollectLightsFromBSLight();
	else
		CollectLightsFromScene();

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());

	// ── GPU dispatch ─────────────────────────────────────────

	if (currentLightCount > 0) {
		context->UpdateSubresource(lightsBuffer.get(), 0, nullptr, frameLights.data(),
			static_cast<UINT>(frameLights.size() * sizeof(LightData)), 0);
	}

	if (diagFrameCounter % 300 == 0) {
		logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}",
		             diagFrameCounter, currentLightCount,
		             clusterSize[0], clusterSize[1], clusterSize[2],
		             CameraNear, CameraFar);
	}

	seenLights.clear();
	seenCBHashes.clear();
	frameLights.clear();

	const std::uint32_t zero = 0;
	context->UpdateSubresource(lightIndexCounterBuffer.get(), 0, nullptr, &zero, sizeof(zero), 0);

	// Cluster building pass
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto* cb = static_cast<LightBuildingCB*>(mapped.pData);
		cb->LightsNear = CameraNear;
		cb->LightsFar = CameraFar;
		cb->pad0[0] = cb->pad0[1] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));
		context->Unmap(lightBuildingCB.get(), 0);

		context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
		ID3D11Buffer* cbPtr = lightBuildingCB.get();
		context->CSSetConstantBuffers(0, 1, &cbPtr);
		ID3D11UnorderedAccessView* buildingUAVs[] = { clustersUAV.get() };
		context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
	}

	{
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	// Cluster culling pass
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto* cb = static_cast<LightCullingCB*>(mapped.pData);
		cb->LightCount = currentLightCount;
		cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraView, &viewTransposed, sizeof(viewTransposed));
		context->Unmap(lightCullingCB.get(), 0);

		ID3D11ShaderResourceView* cullingSRVs[] = { clustersSRV.get(), lightsSRV.get() };
		context->CSSetShaderResources(0, 2, cullingSRVs);

		ID3D11UnorderedAccessView* cullingUAVs[] = { lightIndexCounterUAV.get(), lightIndexListUAV.get(), lightGridUAV.get() };
		context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

		context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
		ID3D11Buffer* cullCBPtr = lightCullingCB.get();
		context->CSSetConstantBuffers(0, 1, &cullCBPtr);

		context->Dispatch(
			(clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
			(clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
			(clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
	}

	// Unbind compute resources
	{
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[3]{};
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	// Bind PS SRVs (t35-t37)
	if (diagFrameCounter >= 3 && currentLightCount > 0) {
		ID3D11ShaderResourceView* views[3]{
			lightsSRV.get(),
			lightIndexListSRV.get(),
			lightGridSRV.get()
		};
		context->PSSetShaderResources(35, ARRAYSIZE(views), views);

		if (diagFrameCounter % 300 == 0) {
			logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)",
				currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
		}
	}
#else
	if (!HasResources()) return;

	auto* rendererData = fo4cs::GetRendererData();
	if (!rendererData) return;
	auto* context = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	if (!context) return;

	const auto& gState = RE::BSGraphics::State::GetSingleton();
	const auto& camView = gState.cameraState.camViewData;

	DirectX::XMFLOAT4X4 projInvTransposed;
	{
		DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.projMat));
		DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(nullptr, proj);
		DirectX::XMStoreFloat4x4(&projInvTransposed, DirectX::XMMatrixTranspose(invProj));
	}

	DirectX::XMFLOAT4X4 viewTransposed;
	{
		DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(
			reinterpret_cast<const DirectX::XMFLOAT4X4*>(camView.viewMat));
		DirectX::XMStoreFloat4x4(&viewTransposed, DirectX::XMMatrixTranspose(view));
	}

	if (!seenLights.empty()) {
		CollectLightsFromBSLight();
	} else {
		CollectLightsFromScene();
	}

	currentLightCount = static_cast<std::uint32_t>(frameLights.size());
	if (currentLightCount > 0) {
		context->UpdateSubresource(lightsBuffer.get(), 0, nullptr, frameLights.data(),
		                           static_cast<UINT>(frameLights.size() * sizeof(LightData)), 0);
	}

	if (diagFrameCounter % 300 == 0) {
		logger::info("[LightLimitFix] frame={} lights={} clusters={}x{}x{} near={:.1f} far={:.0f}",
		             diagFrameCounter, currentLightCount,
		             clusterSize[0], clusterSize[1], clusterSize[2],
		             CameraNear, CameraFar);
	}

	seenLights.clear();
	seenCBHashes.clear();
	frameLights.clear();

	const std::uint32_t zero = 0;
	context->UpdateSubresource(lightIndexCounterBuffer.get(), 0, nullptr, &zero, sizeof(zero), 0);

	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightBuildingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto* cb = static_cast<LightBuildingCB*>(mapped.pData);
		cb->LightsNear = CameraNear;
		cb->LightsFar = CameraFar;
		cb->pad0[0] = cb->pad0[1] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraProjInverse, &projInvTransposed, sizeof(projInvTransposed));
		context->Unmap(lightBuildingCB.get(), 0);

		context->CSSetShader(clusterBuildingCS.get(), nullptr, 0);
		ID3D11Buffer* cbPtr = lightBuildingCB.get();
		context->CSSetConstantBuffers(0, 1, &cbPtr);
		ID3D11UnorderedAccessView* buildingUAVs[] = { clustersUAV.get() };
		context->CSSetUnorderedAccessViews(0, 1, buildingUAVs, nullptr);
		context->Dispatch(clusterSize[0], clusterSize[1], clusterSize[2]);
	}

	{
		ID3D11UnorderedAccessView* nullUAV = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	}

	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		context->Map(lightCullingCB.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		auto* cb = static_cast<LightCullingCB*>(mapped.pData);
		cb->LightCount = currentLightCount;
		cb->pad[0] = cb->pad[1] = cb->pad[2] = 0;
		cb->ClusterSize[0] = clusterSize[0];
		cb->ClusterSize[1] = clusterSize[1];
		cb->ClusterSize[2] = clusterSize[2];
		cb->ClusterSize[3] = 0;
		std::memcpy(&cb->CameraView, &viewTransposed, sizeof(viewTransposed));
		context->Unmap(lightCullingCB.get(), 0);

		ID3D11ShaderResourceView* cullingSRVs[] = { clustersSRV.get(), lightsSRV.get() };
		context->CSSetShaderResources(0, 2, cullingSRVs);

		ID3D11UnorderedAccessView* cullingUAVs[] = { lightIndexCounterUAV.get(), lightIndexListUAV.get(), lightGridUAV.get() };
		context->CSSetUnorderedAccessViews(0, 3, cullingUAVs, nullptr);

		context->CSSetShader(clusterCullingCS.get(), nullptr, 0);
		ID3D11Buffer* cullCBPtr = lightCullingCB.get();
		context->CSSetConstantBuffers(0, 1, &cullCBPtr);

		context->Dispatch(
			(clusterSize[0] + NUMTHREAD_X - 1) / NUMTHREAD_X,
			(clusterSize[1] + NUMTHREAD_Y - 1) / NUMTHREAD_Y,
			(clusterSize[2] + NUMTHREAD_Z - 1) / NUMTHREAD_Z);
	}

	{
		ID3D11ShaderResourceView* nullSRVs[2]{};
		context->CSSetShaderResources(0, 2, nullSRVs);
		ID3D11UnorderedAccessView* nullUAVs[3]{};
		context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);
		context->CSSetShader(nullptr, nullptr, 0);
	}

	if (diagFrameCounter >= 3 && currentLightCount > 0) {
		ID3D11ShaderResourceView* views[3]{
			lightsSRV.get(),
			lightIndexListSRV.get(),
			lightGridSRV.get()
		};
		context->PSSetShaderResources(35, ARRAYSIZE(views), views);

		if (diagFrameCounter % 300 == 0) {
			logger::info("[LightLimitFix] SRVs bound to PS slots t35-t37 ({} lights, {} clusters)",
			             currentLightCount, clusterSize[0] * clusterSize[1] * clusterSize[2]);
		}
	}
#endif
}

void LightLimitFix::Reset()
{
	if (!HasResources()) return;

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

	for (auto* light : lightData->lightList) {
		if (!light || seenLights.contains(light)) continue;
		seenLights.insert(light);
		seenThisPass.push_back(light);
	}
}

void LightLimitFix::CollectLightCB()
{
	auto* rendererData = fo4cs::GetRendererData();
	auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(rendererData->context);
	auto* device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	if (!ctx || !device) return;

	ID3D11Buffer* lightCB = nullptr;
	ctx->PSGetConstantBuffers(2, 1, &lightCB);
	if (!lightCB) return;

	D3D11_BUFFER_DESC desc;
	lightCB->GetDesc(&desc);
	if (desc.ByteWidth < 48) { lightCB->Release(); return; }

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

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(stagingCB, 0, D3D11_MAP_READ, 0, &mapped))) {
		stagingCB->Release();
		return;
	}

	const float* rawData = static_cast<const float*>(mapped.pData);
	std::uint32_t lightCount = desc.ByteWidth / 48;
	if (lightCount > 4) lightCount = 4;

	for (std::uint32_t i = 0; i < lightCount && frameLights.size() < kMaxLights; i++) {
		const float* l = rawData + i * 12;

		if (l[0] == 0.0f && l[1] == 0.0f && l[2] == 0.0f) continue;

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

void LightLimitFix::CollectLightsFromBSLight()
{
	for (auto* light : seenLights) {
		if (!light || frameLights.size() >= kMaxLights) break;
		auto* niLight = reinterpret_cast<NiLightView*>(light);
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

		auto* niLight = reinterpret_cast<NiLightView*>(niObj);
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
}

void LightLimitFix::SetupGeometryBefore(RE::BSRenderPass* /*a_pass*/)
{
	seenThisPass.clear();
}

void LightLimitFix::SetupGeometryAfter(RE::BSRenderPass* a_pass)
{
	CollectLightsFromPass(a_pass);
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
