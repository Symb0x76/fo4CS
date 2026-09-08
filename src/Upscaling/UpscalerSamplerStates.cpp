#include "Upscaling/Upscaler.h"

#include "Platform/RE/CameraData.h"
#include "Platform/RE/SingletonAccessors.h"
#include <RE/FO4Runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Diagnostics/HangTrace.h"
#include "Render/DX12SwapChain.h"
#include "Render/PresentationMenuPolicy.h"
#include "Upscaling/FidelityFX.h"
#include "Upscaling/Streamline.h"
#include "Upscaling/UpscalingInternal.h"
#include "Upscaling/UpscalingRenderTargetIDs.h"

using fo4cs::upscaling::TraceRenderBackendStage;

void Upscaling::UpdateSamplerStates(float a_currentMipBias)
{
	static float previousMipBias = 1000.0f;
	if (previousMipBias == a_currentMipBias)
		return;
	previousMipBias = a_currentMipBias;

	auto rendererData = fo4cs::GetRendererData();
	auto device = reinterpret_cast<ID3D11Device*>(rendererData->device);
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();

	for (int i = 0; i < 320; i++) {
		originalSamplerStates[i] = samplerStates[i];
		if (!originalSamplerStates[i])
			continue;

		if (biasedSamplerStates[i]) {
			biasedSamplerStates[i]->Release();
			biasedSamplerStates[i] = nullptr;
		}

		D3D11_SAMPLER_DESC desc{};
		originalSamplerStates[i]->GetDesc(&desc);
		desc.MipLODBias = a_currentMipBias;
		DX::ThrowIfFailed(device->CreateSamplerState(&desc, &biasedSamplerStates[i]));
	}
}

void Upscaling::OverrideSamplerStates()
{
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();
	for (int i = 0; i < 320; i++) {
		if (biasedSamplerStates[i])
			samplerStates[i] = biasedSamplerStates[i];
	}
}

void Upscaling::ResetSamplerStates()
{
	auto samplerStates = fo4cs::RE::GetSamplerStateArray();
	for (int i = 0; i < 320; i++) {
		if (originalSamplerStates[i])
			samplerStates[i] = originalSamplerStates[i];
	}
}
