#include <iostream>
#include <tchar.h>

#include "D3D11App.hpp"

#define WAPPNAME L"D3D11App"
#define APPNAME   "D3D11App"


int D3D11App::Run(int, char **)
{
  ImGui_ImplWin32_EnableDpiAwareness();
  // Create application window
  m_wc = {
    sizeof(m_wc), CS_CLASSDC, WndProc,
    0L,         0L,         GetModuleHandle(nullptr),
    nullptr,    nullptr,    nullptr,
    nullptr,    WAPPNAME,   nullptr
  };
  ::RegisterClassExW(&m_wc);
  m_hwnd = ::CreateWindowW(m_wc.lpszClassName,
    WAPPNAME, WS_OVERLAPPEDWINDOW,
    100,  100,
    1280, 800,
    nullptr, nullptr, m_wc.hInstance, nullptr
  );

  // Initialize Direct3D
  if (!CreateDeviceD3D()) {
    CleanupDeviceD3D();
    ::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);
    return 1;
  }

  // Show the window
  ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(m_hwnd);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(m_hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  // Our state
  bool show_demo_window = true;
  bool show_another_window = false;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // Main loop
  bool done = false;
  while (!done) {
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    
    // just show something that does work for now
    ImGui::ShowDemoWindow(&show_demo_window);

    // Rendering
    ImGui::Render();
    const float clear_color_with_alpha[4] = {
        clear_color.x * clear_color.w, clear_color.y * clear_color.w,
        clear_color.z * clear_color.w, clear_color.w
    };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0); // Present with vsync
  }
  return 0;
}

inline bool D3D11App::CreateDeviceD3D()
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = m_hwnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  // make sure we are debuggin it
  createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[1] = {
      D3D_FEATURE_LEVEL_11_0,
  };

  HRESULT res = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 1, D3D11_SDK_VERSION,
      &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
  );
  if (res != S_OK)
    return false;
  if (!CreateRenderTarget())
    return false;

  return true;
}

inline void D3D11App::CleanupDeviceD3D()
{
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

inline bool D3D11App::CreateRenderTarget()
{
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  if (pBackBuffer != nullptr) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return true;
  }
  std::cerr << "Failed to get spawnchain buffer (GetBuffer) on pBackBuffer"
            << std::endl;
  return false;
}

inline void D3D11App::CleanupRenderTarget()
{
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;
  switch (msg)
  {
    case WM_SIZE:
      if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
        D3D11App::CleanupRenderTarget();
        g_pSwapChain->ResizeBuffers(
          0,
          (UINT)LOWORD(lParam),
          (UINT)HIWORD(lParam),
          DXGI_FORMAT_UNKNOWN,
          0);
        D3D11App::CreateRenderTarget();
      }
      return 0;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
        return 0;
      break;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main(int argc, char **argv) {
  FILE* conout = nullptr;
  FILE* conerr = nullptr;
  freopen_s(&conout, "CONOUT$", "w", stdout);
  freopen_s(&conerr, "CONERR$", "w", stderr);
  if (!conout || !conerr) {
    return 1;
  }

  auto App = new D3D11App();
  App->Run(argc, argv);
  delete App;

  fclose(conout);
  fclose(conout);
  return 0;
}
