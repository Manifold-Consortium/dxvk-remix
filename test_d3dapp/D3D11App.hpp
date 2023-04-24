#include <windows.h>
#include <d3d11.h>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

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
  D3D11App() : m_hwnd(), m_wc() { };
  ~D3D11App()
  {
    if (g_pSwapChain) {
      ImGui_ImplDX11_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
    }
    CleanupDeviceD3D();
    if (m_wc.lpszClassName) {
      ::DestroyWindow(m_hwnd);
      ::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
    }
  }
  int Run(int, char**);
  inline bool CreateDeviceD3D();
  inline void CleanupDeviceD3D();
  
  static inline bool CreateRenderTarget();
  static inline void CleanupRenderTarget();
};
