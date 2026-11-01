// Direct3D 11 renderer (Windows).

#include "renderer.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <d3d11.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace minidaw::app {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
int g_width = 0;
int g_height = 0;
std::string g_name = "Direct3D 11";

void createRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

void releaseRenderTarget()
{
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

template <typename T>
void release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

HRESULT createDevice(HWND hwnd, D3D_DRIVER_TYPE driver)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained{};
    return D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd,
                                         &g_swapChain, &g_device, &obtained, &g_context);
}

} // namespace

void rendererWindowHints()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // no OpenGL context at all
}

bool rendererInit(GLFWwindow* window, std::string& error)
{
    HWND hwnd = glfwGetWin32Window(window);

    // MINIDAW_D3D_WARP=1 skips the GPU and goes straight to WARP (for testing).
    const char* forceWarp = std::getenv("MINIDAW_D3D_WARP");
    HRESULT hr = E_FAIL;
    if (!(forceWarp && forceWarp[0] == '1')) {
        hr = createDevice(hwnd, D3D_DRIVER_TYPE_HARDWARE);
        g_name = "Direct3D 11 (hardware)";
    }
    if (FAILED(hr)) {
        // No usable GPU driver (typical in a VM): use Windows' software renderer.
        hr = createDevice(hwnd, D3D_DRIVER_TYPE_WARP);
        g_name = "Direct3D 11 (WARP software renderer)";
    }
    if (FAILED(hr)) {
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "Direct3D 11 could not be started, not even the software renderer "
                      "(error 0x%08lX).",
                      static_cast<unsigned long>(hr));
        error = buf;
        return false;
    }
    createRenderTarget();
    glfwGetFramebufferSize(window, &g_width, &g_height);

    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplDX11_Init(g_device, g_context);
    return true;
}

const std::string& rendererName()
{
    return g_name;
}

void rendererNewFrame(GLFWwindow* window)
{
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    if (w > 0 && h > 0 && (w != g_width || h != g_height)) {
        releaseRenderTarget();
        g_swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
        createRenderTarget();
        g_width = w;
        g_height = h;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void rendererRender(GLFWwindow* /*window*/, const float clearColor[4])
{
    g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
    g_context->ClearRenderTargetView(g_renderTarget, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapChain->Present(1, 0);  // vsync
}

void rendererShutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    releaseRenderTarget();
    release(g_swapChain);
    release(g_context);
    release(g_device);
}

void showFatalError(const std::string& message)
{
    std::fprintf(stderr, "error: %s\n", message.c_str());
    MessageBoxA(nullptr, message.c_str(), "mini-daw", MB_OK | MB_ICONERROR);
}

} // namespace minidaw::app
