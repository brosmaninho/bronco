#include "d3d11_proxy.h"
#include "../hook/present_hook.h"

#include <filesystem>
#include <string>

namespace bronco::proxy {

namespace {
    HMODULE g_realD3D11 = nullptr;
    HMODULE g_ourModule = nullptr;

    // Function pointer types for the original D3D11 exports
    using PFN_D3D11CreateDevice = decltype(&D3D11CreateDevice);
    using PFN_D3D11CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

    PFN_D3D11CreateDevice g_originalCreateDevice = nullptr;
    PFN_D3D11CreateDeviceAndSwapChain g_originalCreateDeviceAndSwapChain = nullptr;

    // D3D11On12CreateDevice function pointer (not always available)
    using PFN_D3D11On12CreateDevice = HRESULT(WINAPI*)(
        IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT,
        IUnknown* const*, UINT, UINT,
        ID3D11Device**, ID3D11DeviceContext**, D3D_FEATURE_LEVEL*);

    PFN_D3D11On12CreateDevice g_originalD3D11On12CreateDevice = nullptr;
} // anonymous namespace

bool initialize(HMODULE ourModule)
{
    g_ourModule = ourModule;

    // Build path to the real d3d11.dll in System32
    wchar_t systemDir[MAX_PATH] = {};
    GetSystemDirectoryW(systemDir, MAX_PATH);

    std::wstring realDllPath = std::wstring(systemDir) + L"\\d3d11.dll";

    // Load the real d3d11.dll
    g_realD3D11 = LoadLibraryW(realDllPath.c_str());
    if (!g_realD3D11)
    {
        OutputDebugStringA("[Bronco] Failed to load real d3d11.dll from System32\n");
        return false;
    }

    // Resolve original function pointers
    g_originalCreateDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
        GetProcAddress(g_realD3D11, "D3D11CreateDevice"));

    g_originalCreateDeviceAndSwapChain = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(
        GetProcAddress(g_realD3D11, "D3D11CreateDeviceAndSwapChain"));

    g_originalD3D11On12CreateDevice = reinterpret_cast<PFN_D3D11On12CreateDevice>(
        GetProcAddress(g_realD3D11, "D3D11On12CreateDevice"));

    if (!g_originalCreateDevice || !g_originalCreateDeviceAndSwapChain)
    {
        OutputDebugStringA("[Bronco] Failed to resolve D3D11 function pointers\n");
        FreeLibrary(g_realD3D11);
        g_realD3D11 = nullptr;
        return false;
    }

    OutputDebugStringA("[Bronco] D3D11 proxy initialized successfully\n");
    return true;
}

void shutdown()
{
    if (g_realD3D11)
    {
        FreeLibrary(g_realD3D11);
        g_realD3D11 = nullptr;
    }
    g_originalCreateDevice = nullptr;
    g_originalCreateDeviceAndSwapChain = nullptr;
    g_originalD3D11On12CreateDevice = nullptr;

    OutputDebugStringA("[Bronco] D3D11 proxy shut down\n");
}

FARPROC getOriginalFunction(const char* name)
{
    if (!g_realD3D11) return nullptr;
    return GetProcAddress(g_realD3D11, name);
}

// --- Forwarded exports ---

extern "C" {

HRESULT WINAPI proxied_D3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    return g_originalCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);
}

HRESULT WINAPI proxied_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext)
{
    HRESULT hr = g_originalCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain,
        ppDevice, pFeatureLevel, ppImmediateContext);

    // If swap chain was created, hook Present()
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain)
    {
        bronco::hook::hookSwapChain(*ppSwapChain);
    }

    return hr;
}

HRESULT WINAPI proxied_D3D11On12CreateDevice(
    IUnknown* pDevice,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    IUnknown* const* ppCommandQueues,
    UINT NumQueues,
    UINT NodeMask,
    ID3D11Device** ppDevice,
    ID3D11DeviceContext** ppImmediateContext,
    D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    if (!g_originalD3D11On12CreateDevice)
        return E_NOTIMPL;

    return g_originalD3D11On12CreateDevice(
        pDevice, Flags, pFeatureLevels, FeatureLevels,
        ppCommandQueues, NumQueues, NodeMask,
        ppDevice, ppImmediateContext, pChosenFeatureLevel);
}

} // extern "C"

} // namespace bronco::proxy
