#include "d3d11_rtx.h"
#include "d3d11_device.h"

namespace dxvk {
D3D11Rtx::D3D11Rtx(D3D11Device *d3d11Device)
  : m_parent(d3d11Device),
    m_rtStagingData(d3d11Device->m_dxvkDevice,
      (VkMemoryPropertyFlagBits)(VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
{
  Logger::info("Welcome to D3D11Rtx");
}

// TODO
void D3D11Rtx::Initialize() {}
void D3D11Rtx::ResetSwapChain(const DXGI_SWAP_CHAIN_DESC1 &presentationParameters) {}
uint32_t D3D11Rtx::processRenderState() {
  return uint32_t();
}

void D3D11Rtx::EndFrame() {
  // Inform backend of end-frame
  ID3D11DeviceContext* idctx = nullptr;
  m_parent->GetImmediateContext(&idctx);
  auto dctx = static_cast<D3D11DeviceContext*>(idctx);
  dctx->EmitCs([](DxvkContext* ctx) { static_cast<RtxContext*>(ctx)->endFrame(); });

  // Reset for the next frame
  m_rtxInjectTriggered = false;
  m_drawCallID = 0;
}
} // namespace dxvk
