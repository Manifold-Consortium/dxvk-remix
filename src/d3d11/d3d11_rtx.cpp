#include "d3d11_rtx.h"
#include "d3d11_device.h"

namespace dxvk {
  D3D11Rtx::D3D11Rtx(D3D11Device* d3d11Device)
    : m_parent(d3d11Device),
      m_rtStagingData(d3d11Device->m_dxvkDevice,
      (VkMemoryPropertyFlagBits) (VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { }

// TODO
void D3D11Rtx::Initialize() {
  ID3D11DeviceContext* dctx = nullptr;
  m_parent->GetImmediateContext(&dctx);
  assert(dctx);
  m_dctx = static_cast<D3D11DeviceContext*>(dctx);
  Logger::info("Welcome to D3D11Rtx");
}

void D3D11Rtx::ResetSwapChain(const DXGI_SWAP_CHAIN_DESC1 &presentationParameters) {
  if (0 == memcmp(&m_activePresentParams, &presentationParameters, sizeof(presentationParameters)))
    return;

  // Cache these
  m_activePresentParams = presentationParameters;

  // Inform the backend about potential presenter update
  m_dctx->EmitCs([cWidth  = m_activePresentParams.Width,
                  cHeight = m_activePresentParams.Height](DxvkContext *ctx) {
    static_cast<RtxContext *>(ctx)->resetScreenResolution({cWidth, cHeight, 1});
  });
}

uint32_t D3D11Rtx::processRenderState() {
  return uint32_t();
}

void D3D11Rtx::EndFrame() {
  // Inform backend of end-frame
  m_dctx->EmitCs([](DxvkContext* ctx) { static_cast<RtxContext*>(ctx)->endFrame(); });

  // Reset for the next frame
  m_rtxInjectTriggered = false;
  m_drawCallID = 0;
}
} // namespace dxvk
