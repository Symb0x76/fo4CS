#include "Render/RuntimeAdapter.h"

#include <RE/FO4Runtime.h>

#include <Windows.h>

namespace fo4cs {
RuntimeAdapter::RuntimeAdapter() noexcept {
#if defined(FALLOUT_POST_AE)
  capabilities = {.flavor = RuntimeFlavor::kPostAE,
                  .name = "PostAE",
                  .supportsStreamline = true,
                  .supportsD3D12Proxy = true,
                  .requiresExistingRendererRecovery = true,
                  .supportsDeferredPipeline = true};
#elif defined(FALLOUT_POST_NG)
  capabilities = {.flavor = RuntimeFlavor::kPostNG,
                  .name = "PostNG",
                  .supportsStreamline = true,
                  .supportsD3D12Proxy = true,
                  .requiresExistingRendererRecovery = false,
                  .supportsDeferredPipeline = true};
#else
  capabilities = {.flavor = RuntimeFlavor::kPreNG,
                  .name = "PreNG",
                  // PreNG supports Streamline. This was false until 2026-09-12 on the
                  // belief that 1.10.163 had an engine defect; measured in game, DLSS,
                  // DLSS-G and Reflex all initialise and run there. The two things that
                  // actually broke were a loader-lock deadlock (see
                  // Streamline::PostDevice) and RenderDoc suppressing NVAPI in the
                  // process, which makes NGX report the GPU unsupported.
                  .supportsStreamline = true,
                  .supportsD3D12Proxy = true,
                  .requiresExistingRendererRecovery = false,
                  .supportsDeferredPipeline = true};
#endif
}

const RuntimeAdapter &RuntimeAdapter::Get() noexcept {
  static RuntimeAdapter adapter;
  return adapter;
}

std::optional<ExistingRenderer>
RuntimeAdapter::FindExistingRenderer() const noexcept {
#if defined(FALLOUT_POST_AE)
  auto *rendererData = RE::BSGraphics::GetRendererData();
  if (!rendererData) {
    return std::nullopt;
  }

  auto *device = reinterpret_cast<ID3D11Device *>(rendererData->device);
  if (!device) {
    return std::nullopt;
  }

  auto isUsableWindow = [](const auto &a_window) {
    return a_window.hwnd && a_window.swapChain &&
           IsWindow(reinterpret_cast<HWND>(a_window.hwnd));
  };

  if (auto *currentWindow = RE::BSGraphics::GetCurrentRendererWindow();
      currentWindow && isUsableWindow(*currentWindow)) {
    return ExistingRenderer{.device = device,
                            .swapChain = reinterpret_cast<IDXGISwapChain *>(
                                currentWindow->swapChain)};
  }

  for (auto &renderWindow : rendererData->renderWindow) {
    if (isUsableWindow(renderWindow)) {
      return ExistingRenderer{.device = device,
                              .swapChain = reinterpret_cast<IDXGISwapChain *>(
                                  renderWindow.swapChain)};
    }
  }
#endif
  return std::nullopt;
}
} // namespace fo4cs
