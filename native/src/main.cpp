#include "AppUi.h"
#include "UiScale.h"

#include <cyui/Core.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <d3d11.h>
#include <shellapi.h>
#include <windows.h>

#include <cwchar>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
ID3D11Device* device {};
ID3D11DeviceContext* context {};
IDXGISwapChain* swapChain {};
ID3D11RenderTargetView* target {};
UINT resizeWidth {}, resizeHeight {};
bool imguiReady {};
float uiDpiMultiplier { 1.0f };

void createTarget()
{
    ID3D11Texture2D* buffer {};
    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) {
        device->CreateRenderTargetView(buffer, nullptr, &target);
        buffer->Release();
    }
}
void releaseTarget()
{
    if (target) { target->Release(); target = nullptr; }
}
bool createDevice(HWND window)
{
    DXGI_SWAP_CHAIN_DESC desc {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL level {};
    auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);
    if (result == DXGI_ERROR_UNSUPPORTED)
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION, &desc, &swapChain, &device, &level, &context);
    if (FAILED(result)) return false;
    createTarget();
    return target != nullptr;
}
void releaseDevice()
{
    releaseTarget();
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
}
bool captureDemoFrame(const wchar_t* path)
{
    ID3D11Texture2D* backBuffer {};
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    D3D11_TEXTURE2D_DESC desc {};
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging {};
    const bool created = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging));
    if (created) context->CopyResource(staging, backBuffer);
    backBuffer->Release();
    if (!created) return false;
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        staging->Release();
        return false;
    }
    const std::size_t stride = static_cast<std::size_t>(desc.Width) * 4;
    std::vector<unsigned char> pixels(stride * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* source = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
        auto* destination = pixels.data() + y * stride;
        for (UINT x = 0; x < desc.Width; ++x) {
            destination[x * 4] = source[x * 4 + 2];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = source[x * 4];
            destination[x * 4 + 3] = 255;
        }
    }
    context->Unmap(staging, 0);
    staging->Release();
    BITMAPFILEHEADER fileHeader {};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER imageHeader {};
    imageHeader.biSize = sizeof(imageHeader);
    imageHeader.biWidth = static_cast<LONG>(desc.Width);
    imageHeader.biHeight = -static_cast<LONG>(desc.Height);
    imageHeader.biPlanes = 1;
    imageHeader.biBitCount = 32;
    imageHeader.biCompression = BI_RGB;
    imageHeader.biSizeImage = static_cast<DWORD>(pixels.size());
    FILE* file {};
    if (_wfopen_s(&file, path, L"wb") != 0 || !file) return false;
    const bool written = std::fwrite(&fileHeader, sizeof(fileHeader), 1, file) == 1
        && std::fwrite(&imageHeader, sizeof(imageHeader), 1, file) == 1
        && std::fwrite(pixels.data(), 1, pixels.size(), file) == pixels.size();
    std::fclose(file);
    return written;
}
LRESULT WINAPI windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (imguiReady && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return true;
    switch (message) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) { resizeWidth = LOWORD(lParam); resizeHeight = HIWORD(lParam); }
        return 0;
    case WM_DPICHANGED:
        ui_scale::setMonitorDpi(static_cast<float>(HIWORD(wParam)) / 96.0f);
        if (imguiReady) cyui::applyDpi(ui_scale::combinedDpi() * uiDpiMultiplier);
        if (const auto* recommended = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(window, nullptr, recommended->left, recommended->top,
                recommended->right - recommended->left, recommended->bottom - recommended->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    bool demoMode = false, performanceAtStart = false;
    std::string themeKey = "navy";
    cyui::Density density = cyui::Density::comfortable;
    std::wstring demoCapturePath;
    int argumentCount {};
    if (auto** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount)) {
        for (int i = 1; i < argumentCount; ++i) {
            demoMode |= std::wcscmp(arguments[i], L"--demo") == 0;
            performanceAtStart |= std::wcscmp(arguments[i], L"--performance") == 0;
            if (std::wcscmp(arguments[i], L"--theme") == 0 && i + 1 < argumentCount) {
                const std::wstring value = arguments[++i];
                themeKey.assign(value.begin(), value.end());
            }
            if (std::wcscmp(arguments[i], L"--density") == 0 && i + 1 < argumentCount) {
                const std::wstring value = arguments[++i];
                density = value == L"compact" ? cyui::Density::compact : cyui::Density::comfortable;
            }
            if (std::wcscmp(arguments[i], L"--ui-scale") == 0 && i + 1 < argumentCount) {
                const double value = std::wcstod(arguments[++i], nullptr);
                if (std::isfinite(value)) uiDpiMultiplier = static_cast<float>(std::clamp(value, 0.75, 2.0));
            }
            if (std::wcscmp(arguments[i], L"--capture-demo") == 0 && i + 1 < argumentCount)
                demoCapturePath = arguments[++i];
        }
        LocalFree(arguments);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc { sizeof(wc), CS_CLASSDC, windowProc, 0, 0, instance,
        LoadIconW(instance, MAKEINTRESOURCEW(1)), LoadCursorW(nullptr, IDC_ARROW), nullptr,
        nullptr, L"CyWinTaskNative", LoadIconW(instance, MAKEINTRESOURCEW(1)) };
    RegisterClassExW(&wc);
    RECT work {};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
        work = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    const int availableWidth = std::max(800, static_cast<int>(work.right - work.left));
    const int availableHeight = std::max(600, static_cast<int>(work.bottom - work.top));
    const int width = std::min(1500, availableWidth - 40);
    const int height = std::min(930, availableHeight - 40);
    HWND window = CreateWindowW(wc.lpszClassName, L"CyWinTask — Gestionnaire des tâches",
        WS_OVERLAPPEDWINDOW, work.left + (availableWidth - width) / 2,
        work.top + (availableHeight - height) / 2, width, height, nullptr, nullptr, instance, nullptr);
    if (!createDevice(window)) {
        MessageBoxW(nullptr, L"Direct3D 11 est indisponible.", L"CyWinTask", MB_OK | MB_ICONERROR);
        DestroyWindow(window);
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    ui_scale::load();
    ui_scale::setMonitorDpi(static_cast<float>(GetDpiForWindow(window)) / 96.0f);
    cyui::Config theme;
    theme.themeKey = themeKey.c_str();
    theme.density = density;
    theme.dpiScale = ui_scale::combinedDpi() * uiDpiMultiplier;
    cyui::apply(theme);
    cyui::loadFonts();
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(device, context);
    imguiReady = true;
    {
        AppUi app(device, demoMode, performanceAtStart);
        bool done = false;
        int demoFrames = 0;
        while (!done) {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT) done = true;
            }
            if (done) break;
            if (resizeWidth && resizeHeight) {
                releaseTarget();
                swapChain->ResizeBuffers(0, resizeWidth, resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                resizeWidth = resizeHeight = 0;
                createTarget();
            }
            if (IsIconic(window)) { Sleep(100); continue; }
            if (ui_scale::consumeChanged()) cyui::applyDpi(ui_scale::combinedDpi() * uiDpiMultiplier);
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app.frame();
            cyui::renderNotifications();
            ImGui::Render();
            const auto clear = cyui::vec(cyui::palette().bg0);
            const float rgba[] { clear.x, clear.y, clear.z, clear.w };
            context->OMSetRenderTargets(1, &target, nullptr);
            context->ClearRenderTargetView(target, rgba);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
            if (demoMode && !demoCapturePath.empty() && ++demoFrames >= 30) {
                captureDemoFrame(demoCapturePath.c_str());
                done = true;
            }
            swapChain->Present(1, 0);
            Sleep(GetForegroundWindow() == window ? 16 : 80);
        }
    }
    imguiReady = false;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    releaseDevice();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
