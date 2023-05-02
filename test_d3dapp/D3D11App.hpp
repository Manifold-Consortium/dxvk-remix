#pragma once
#include <windows.h>

#include <dxgi1_2.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

static D3D11_VIEWPORT           g_pViewport = {};
static ID3D11Device*            g_pd3dDevice;
static ID3D11DeviceContext*     g_pd3dDeviceContext;
static IDXGISwapChain*          g_pSwapChain;
static ID3D11RenderTargetView*  g_mainRenderTargetView;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class D3D11App
{
private:
  HWND m_hwnd;
  WNDCLASSEXW m_wc;
public:
   D3D11App() : m_hwnd(), m_wc() {};
  ~D3D11App()
  {
    if (g_pSwapChain) {
      ImGui_ImplDX11_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
    }
    if (m_wc.lpszClassName) {
      ::DestroyWindow(m_hwnd);
      ::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
    }
  }
  int Run(const int argc, const char **argv);
  inline bool CreateDeviceD3D(uint32_t width, uint32_t height);
  
  static inline bool CreateRenderTarget(uint32_t width, uint32_t height);
  static inline void CleanupRenderTarget();
};
