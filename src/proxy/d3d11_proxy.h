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

} // namespace bronco::proxy

// All forwarded DLL exports must be at global scope (no namespace) for the .def file
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

    // Generic trampoline exports (resolved at runtime via GetProcAddress)
    void WINAPI proxied_D3D11CoreCreateDevice();
    void WINAPI proxied_D3D11CoreCreateLayeredDevice();
    void WINAPI proxied_D3D11CoreGetLayeredDeviceSize();
    void WINAPI proxied_D3D11CoreRegisterLayers();
    void WINAPI proxied_D3DKMTCloseAdapter();
    void WINAPI proxied_D3DKMTCreateAllocation();
    void WINAPI proxied_D3DKMTCreateContext();
    void WINAPI proxied_D3DKMTCreateDevice();
    void WINAPI proxied_D3DKMTCreateSynchronizationObject();
    void WINAPI proxied_D3DKMTDestroyAllocation();
    void WINAPI proxied_D3DKMTDestroyContext();
    void WINAPI proxied_D3DKMTDestroyDevice();
    void WINAPI proxied_D3DKMTDestroySynchronizationObject();
    void WINAPI proxied_D3DKMTEscape();
    void WINAPI proxied_D3DKMTGetContextSchedulingPriority();
    void WINAPI proxied_D3DKMTGetDeviceState();
    void WINAPI proxied_D3DKMTGetDisplayModeList();
    void WINAPI proxied_D3DKMTGetMultisampleMethodList();
    void WINAPI proxied_D3DKMTGetRuntimeData();
    void WINAPI proxied_D3DKMTGetSharedPrimaryHandle();
    void WINAPI proxied_D3DKMTLock();
    void WINAPI proxied_D3DKMTOpenAdapterFromHdc();
    void WINAPI proxied_D3DKMTOpenResource();
    void WINAPI proxied_D3DKMTPresent();
    void WINAPI proxied_D3DKMTQueryAdapterInfo();
    void WINAPI proxied_D3DKMTQueryAllocationResidency();
    void WINAPI proxied_D3DKMTQueryResourceInfo();
    void WINAPI proxied_D3DKMTRender();
    void WINAPI proxied_D3DKMTSetAllocationPriority();
    void WINAPI proxied_D3DKMTSetContextSchedulingPriority();
    void WINAPI proxied_D3DKMTSetDisplayMode();
    void WINAPI proxied_D3DKMTSetDisplayPrivateDriverFormat();
    void WINAPI proxied_D3DKMTSetGammaRamp();
    void WINAPI proxied_D3DKMTSetVidPnSourceOwner();
    void WINAPI proxied_D3DKMTSignalSynchronizationObject();
    void WINAPI proxied_D3DKMTUnlock();
    void WINAPI proxied_D3DKMTWaitForSynchronizationObject();
    void WINAPI proxied_D3DKMTWaitForVerticalBlankEvent();
    void WINAPI proxied_EnableFeatureLevelUpgrade();
    void WINAPI proxied_OpenAdapter10();
    void WINAPI proxied_OpenAdapter10_2();
    void WINAPI proxied_D3D11CreateDeviceForD3D12();
    void WINAPI proxied_CreateDirect3D11DeviceFromDXGIDevice();
    void WINAPI proxied_CreateDirect3D11SurfaceFromDXGISurface();
}
