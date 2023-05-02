#include <iostream>
#include <tchar.h>

#include "D3D11App.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

constexpr auto WAPPNAME = L"D3D11App";
constexpr auto APPNAME  =  "D3D11App";

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


int D3D11App::Run(const int argc, const char **argv)
{
  ImGui_ImplWin32_EnableDpiAwareness();

  auto width  = 2560;
  auto height = 1080;
  if (argc >= 3) {
    width  = atoi(argv[1]);
    height = atoi(argv[2]);
  }

  // Create application window
  m_wc = {
    sizeof(m_wc), CS_CLASSDC, WndProc,
    0L,         0L,         GetModuleHandle(nullptr),
    nullptr,    nullptr,    nullptr,
    nullptr,    WAPPNAME,   nullptr
  };
  ::RegisterClassExW(&m_wc);

  // Initialize Direct3D
  m_hwnd = ::CreateWindowW(m_wc.lpszClassName,
    WAPPNAME, WS_OVERLAPPEDWINDOW,
    100,  100,
    width, height,
    nullptr, nullptr, m_wc.hInstance, nullptr
  );
  if (!CreateDeviceD3D(width, height))
    return 1;

  // Show the window
  ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(m_hwnd);

  // Setup Dear ImGui context
  {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
  }

  // Create Vertex Shader
  ID3DBlob *vsBlob;
  ID3D11VertexShader *vertexShader;
  {
    ID3DBlob *shaderCompileErrorsBlob;
    HRESULT hResult =
      D3DCompileFromFile(L"shaders.hlsl",
        nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsBlob, &shaderCompileErrorsBlob
      );
    if (FAILED(hResult)) {
      const char *errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = static_cast<const char *>(shaderCompileErrorsBlob->GetBufferPointer());
        shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error", MB_ICONERROR | MB_OK);
      return 1;
    }
    hResult = g_pd3dDevice->CreateVertexShader(
      vsBlob->GetBufferPointer(),
      vsBlob->GetBufferSize(), nullptr,
      &vertexShader
    );
    assert(SUCCEEDED(hResult));
  }

  // Create Vertex Buffer
  ID3D11Buffer *vertexBuffer;
  UINT numVerts;
  UINT stride;
  UINT offset;
  {
    const float vertexData[] = {
      // x,  y,    r,   g,   b,   a
      0.0f,  0.5f, 0.f, 1.f, 0.f, 1.f,
      0.5f, -0.5f, 1.f, 0.f, 0.f, 1.f,
     -0.5f, -0.5f, 0.f, 0.f, 1.f, 1.f
    };
    stride = 6 * sizeof(float);
    numVerts = sizeof(vertexData) / stride;
    offset = 0;

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.ByteWidth = sizeof(vertexData);
    vertexBufferDesc.Usage     = D3D11_USAGE_IMMUTABLE;
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexSubresourceData = {vertexData};

    const HRESULT hResult = g_pd3dDevice->CreateBuffer(
        &vertexBufferDesc, &vertexSubresourceData, &vertexBuffer
    );
    assert(SUCCEEDED(hResult));
  }

  // Create Pixel Shader
  ID3D11PixelShader *pixelShader;
  {
    ID3DBlob *psBlob;
    ID3DBlob *shaderCompileErrorsBlob;
    HRESULT hResult =
      D3DCompileFromFile(L"shaders.hlsl",
        nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psBlob, &shaderCompileErrorsBlob
      );
    if (FAILED(hResult)) {
      const char *errorString = NULL;
      if (hResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
        errorString = "Could not compile shader; file not found";
      else if (shaderCompileErrorsBlob) {
        errorString = static_cast<const char *>(shaderCompileErrorsBlob->GetBufferPointer());
        shaderCompileErrorsBlob->Release();
      }
      MessageBoxA(0, errorString, "Shader Compiler Error", MB_ICONERROR | MB_OK);
      return 1;
    }
    hResult = g_pd3dDevice->CreatePixelShader(
      psBlob->GetBufferPointer(),
      psBlob->GetBufferSize(), nullptr,
      &pixelShader
    );
    assert(SUCCEEDED(hResult));
    psBlob->Release();
  }

  // Load Image
  int texWidth, texHeight, texNumChannels, texBytesPerRow;
  unsigned char* testTextureBytes;
  {
    testTextureBytes = stbi_load("testTexture.png",
      &texWidth, &texHeight, &texNumChannels, 4
    );
    assert(testTextureBytes);
    texBytesPerRow = 4 * texWidth;
  }
  
  // Create Texture
  ID3D11Texture2D* texture;
  D3D11_TEXTURE2D_DESC textureDesc = {};
  {
    textureDesc.Width              = texWidth;
    textureDesc.Height             = texHeight;
    textureDesc.MipLevels          = 1;
    textureDesc.ArraySize          = 1;
    textureDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    textureDesc.SampleDesc.Count   = 1;
    textureDesc.Usage              = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA textureSubresourceData = {};
    textureSubresourceData.pSysMem = testTextureBytes;
    textureSubresourceData.SysMemPitch = texBytesPerRow;

    g_pd3dDevice->CreateTexture2D(&textureDesc, &textureSubresourceData, &texture);
  }

  // Create Input Layout
  ID3D11InputLayout *inputLayout;
  {
    const D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = {
        {"POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    const HRESULT hResult = g_pd3dDevice->CreateInputLayout(
        inputElementDesc, ARRAYSIZE(inputElementDesc),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    assert(SUCCEEDED(hResult));
    vsBlob->Release();
  }

  // Our state
  bool show_demo_window = true;
  const auto& clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
  const float clear_color_with_alpha[4] = {
    clear_color.x * clear_color.w, clear_color.y * clear_color.w,
    clear_color.z * clear_color.w, clear_color.w
  };

  // Main loop
  bool done = false;
  while (!done) {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      if (msg.message == WM_QUIT)
        done = true;
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
    
    // Clear output and set target ouput & viewport
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    g_pd3dDeviceContext->RSSetViewports(1, &g_pViewport);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);

    // Render Triangle
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pd3dDeviceContext->IASetInputLayout(inputLayout);
    g_pd3dDeviceContext->VSSetShader(vertexShader, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(pixelShader,  nullptr, 0);
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    g_pd3dDeviceContext->Draw(numVerts, 0);

    // Start the Dear ImGui frame
    {
      ImGui_ImplDX11_NewFrame();
      ImGui_ImplWin32_NewFrame();
      ImGui::NewFrame();

      // just show something that does work for now
      ImGui::ShowDemoWindow(&show_demo_window);

      // Rendering
      ImGui::Render();
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    // Present with vsync
    g_pSwapChain->Present(1, 0);
  }
  // Complete
  g_pd3dDeviceContext->ClearState();
  g_pd3dDeviceContext->Release();
  g_pSwapChain->Release();
  g_pd3dDevice->Release();
  std::ignore = g_pd3dDeviceContext, std::ignore = g_pSwapChain, std::ignore = g_pd3dDevice;
  return 0;
}

inline bool D3D11App::CreateDeviceD3D(uint32_t width, uint32_t height)
{
  // Setup swap chain
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width   = 0;
  sd.BufferDesc.Height  = 0;
  sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = m_hwnd;
  sd.SampleDesc.Count   = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed   = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  const UINT createDeviceFlags = 0;
  // make sure we are debuggin it
  // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0
  };
  const HRESULT res = D3D11CreateDeviceAndSwapChain(
    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION,
    &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
  );
  if (res != S_OK)
    return false;
  if (!CreateRenderTarget(width, height))
    return false;

  g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

  return true;
}

inline bool D3D11App::CreateRenderTarget(uint32_t width, uint32_t height)
{
  // Create Viewport
  ZeroMemory(&g_pViewport, sizeof(D3D11_VIEWPORT));
  g_pViewport.Width    = static_cast<FLOAT>(width);
  g_pViewport.Height   = static_cast<FLOAT>(height);
  g_pViewport.MinDepth = 0.0f;
  g_pViewport.MaxDepth = 1.0f;
  g_pViewport.TopLeftX = 0;
  g_pViewport.TopLeftY = 0;
  
  // Create Render Target
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  if (pBackBuffer != nullptr) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    return true;
  }
  return false;
}

inline void D3D11App::CleanupRenderTarget()
{
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;
  switch (msg)
  {
    case WM_SIZE:
      if (msg == WM_SIZE) {
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
          D3D11App::CleanupRenderTarget();
          g_pSwapChain->ResizeBuffers(
            0,
            static_cast<UINT>(LOWORD(lParam)),
            static_cast<UINT>(HIWORD(lParam)),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0);
          D3D11App::CreateRenderTarget(
            static_cast<UINT>(LOWORD(lParam)),
            static_cast<UINT>(HIWORD(lParam))
          );
        }
      }
      break;
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
        return 0;
      break;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main(const int argc, const char **argv) {
  FILE* conout = nullptr;
  freopen_s(&conout, "CONOUT$", "w", stdout);
  if (!conout) {
    return 1;
  }

  auto App = std::make_unique<D3D11App>();
  App->Run(argc, argv);
  App.reset();

  fclose(conout);
  return 0;
}
