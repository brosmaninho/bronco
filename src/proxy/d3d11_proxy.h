#pragma once

#include <Windows.h>
#include <d3d11.h>

namespace bronco::proxy {

/// Initialize the proxy by loading the real d3d11.dll from System32.
/// Returns true on success.
bool initialize(HMODULE ourModule);

/// Shut down the proxy and unload the real DLL.
void shutdown();

/// Get a pointer to an original exported function by name.
FARPROC getOriginalFunction(const char* name);

/// Forwarded D3D11 functions
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
        ID3D11DeviceContext** ppImmediateContext);

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
        ID3D11DeviceContext** ppImmediateContext);

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
        D3D_FEATURE_LEVEL* pChosenFeatureLevel);
}

} // namespace bronco::proxy
