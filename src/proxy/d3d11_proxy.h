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
    // All D3DKMT functions take a single pointer argument and return NTSTATUS (LONG).
    LONG WINAPI proxied_D3D11CoreCreateDevice(void* pData);
    LONG WINAPI proxied_D3D11CoreCreateLayeredDevice(void* pData);
    LONG WINAPI proxied_D3D11CoreGetLayeredDeviceSize(void* pData);
    LONG WINAPI proxied_D3D11CoreRegisterLayers(void* pData);
    LONG WINAPI proxied_D3DKMTCloseAdapter(void* pData);
    LONG WINAPI proxied_D3DKMTCreateAllocation(void* pData);
    LONG WINAPI proxied_D3DKMTCreateContext(void* pData);
    LONG WINAPI proxied_D3DKMTCreateDevice(void* pData);
    LONG WINAPI proxied_D3DKMTCreateSynchronizationObject(void* pData);
    LONG WINAPI proxied_D3DKMTDestroyAllocation(void* pData);
    LONG WINAPI proxied_D3DKMTDestroyContext(void* pData);
    LONG WINAPI proxied_D3DKMTDestroyDevice(void* pData);
    LONG WINAPI proxied_D3DKMTDestroySynchronizationObject(void* pData);
    LONG WINAPI proxied_D3DKMTEscape(void* pData);
    LONG WINAPI proxied_D3DKMTGetContextSchedulingPriority(void* pData);
    LONG WINAPI proxied_D3DKMTGetDeviceState(void* pData);
    LONG WINAPI proxied_D3DKMTGetDisplayModeList(void* pData);
    LONG WINAPI proxied_D3DKMTGetMultisampleMethodList(void* pData);
    LONG WINAPI proxied_D3DKMTGetRuntimeData(void* pData);
    LONG WINAPI proxied_D3DKMTGetSharedPrimaryHandle(void* pData);
    LONG WINAPI proxied_D3DKMTLock(void* pData);
    LONG WINAPI proxied_D3DKMTOpenAdapterFromHdc(void* pData);
    LONG WINAPI proxied_D3DKMTOpenResource(void* pData);
    LONG WINAPI proxied_D3DKMTPresent(void* pData);
    LONG WINAPI proxied_D3DKMTQueryAdapterInfo(void* pData);
    LONG WINAPI proxied_D3DKMTQueryAllocationResidency(void* pData);
    LONG WINAPI proxied_D3DKMTQueryResourceInfo(void* pData);
    LONG WINAPI proxied_D3DKMTRender(void* pData);
    LONG WINAPI proxied_D3DKMTSetAllocationPriority(void* pData);
    LONG WINAPI proxied_D3DKMTSetContextSchedulingPriority(void* pData);
    LONG WINAPI proxied_D3DKMTSetDisplayMode(void* pData);
    LONG WINAPI proxied_D3DKMTSetDisplayPrivateDriverFormat(void* pData);
    LONG WINAPI proxied_D3DKMTSetGammaRamp(void* pData);
    LONG WINAPI proxied_D3DKMTSetVidPnSourceOwner(void* pData);
    LONG WINAPI proxied_D3DKMTSignalSynchronizationObject(void* pData);
    LONG WINAPI proxied_D3DKMTUnlock(void* pData);
    LONG WINAPI proxied_D3DKMTWaitForSynchronizationObject(void* pData);
    LONG WINAPI proxied_D3DKMTWaitForVerticalBlankEvent(void* pData);
    LONG WINAPI proxied_EnableFeatureLevelUpgrade(void* pData);
    LONG WINAPI proxied_OpenAdapter10(void* pData);
    LONG WINAPI proxied_OpenAdapter10_2(void* pData);
    LONG WINAPI proxied_D3D11CreateDeviceForD3D12(void* pData);
    LONG WINAPI proxied_CreateDirect3D11DeviceFromDXGIDevice(void* pData);
    LONG WINAPI proxied_CreateDirect3D11SurfaceFromDXGISurface(void* pData);
}
