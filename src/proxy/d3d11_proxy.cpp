#include "d3d11_proxy.h"
#include "../hook/present_hook.h"

#include <filesystem>
#include <string>
#include <array>
#include <atomic>

namespace bronco::proxy {

// These are in the named namespace (not anonymous) so extern "C" trampolines
// can access them via bronco::proxy:: qualification.
static HMODULE g_realD3D11 = nullptr;
static HMODULE g_ourModule = nullptr;

// Function pointer types for the original D3D11 exports
using PFN_D3D11CreateDevice = decltype(&D3D11CreateDevice);
using PFN_D3D11CreateDeviceAndSwapChain = decltype(&D3D11CreateDeviceAndSwapChain);

static PFN_D3D11CreateDevice g_originalCreateDevice = nullptr;
static PFN_D3D11CreateDeviceAndSwapChain g_originalCreateDeviceAndSwapChain = nullptr;

// D3D11On12CreateDevice function pointer (not always available)
using PFN_D3D11On12CreateDevice = HRESULT(WINAPI*)(
    IUnknown*, UINT, const D3D_FEATURE_LEVEL*, UINT,
    IUnknown* const*, UINT, UINT,
    ID3D11Device**, ID3D11DeviceContext**, D3D_FEATURE_LEVEL*);

static PFN_D3D11On12CreateDevice g_originalD3D11On12CreateDevice = nullptr;

// Table of function pointers for the remaining 44 forwarded exports.
// These are resolved at init via GetProcAddress and called through trampolines.
struct ForwardedExport {
    const char* name;
    FARPROC proc;
};

// Indices into g_forwardedExports for each trampoline
enum ExportIndex : int {
    IDX_D3D11CoreCreateDevice = 0,
    IDX_D3D11CoreCreateLayeredDevice,
    IDX_D3D11CoreGetLayeredDeviceSize,
    IDX_D3D11CoreRegisterLayers,
    IDX_D3DKMTCloseAdapter,
    IDX_D3DKMTCreateAllocation,
    IDX_D3DKMTCreateContext,
    IDX_D3DKMTCreateDevice,
    IDX_D3DKMTCreateSynchronizationObject,
    IDX_D3DKMTDestroyAllocation,
    IDX_D3DKMTDestroyContext,
    IDX_D3DKMTDestroyDevice,
    IDX_D3DKMTDestroySynchronizationObject,
    IDX_D3DKMTEscape,
    IDX_D3DKMTGetContextSchedulingPriority,
    IDX_D3DKMTGetDeviceState,
    IDX_D3DKMTGetDisplayModeList,
    IDX_D3DKMTGetMultisampleMethodList,
    IDX_D3DKMTGetRuntimeData,
    IDX_D3DKMTGetSharedPrimaryHandle,
    IDX_D3DKMTLock,
    IDX_D3DKMTOpenAdapterFromHdc,
    IDX_D3DKMTOpenResource,
    IDX_D3DKMTPresent,
    IDX_D3DKMTQueryAdapterInfo,
    IDX_D3DKMTQueryAllocationResidency,
    IDX_D3DKMTQueryResourceInfo,
    IDX_D3DKMTRender,
    IDX_D3DKMTSetAllocationPriority,
    IDX_D3DKMTSetContextSchedulingPriority,
    IDX_D3DKMTSetDisplayMode,
    IDX_D3DKMTSetDisplayPrivateDriverFormat,
    IDX_D3DKMTSetGammaRamp,
    IDX_D3DKMTSetVidPnSourceOwner,
    IDX_D3DKMTSignalSynchronizationObject,
    IDX_D3DKMTUnlock,
    IDX_D3DKMTWaitForSynchronizationObject,
    IDX_D3DKMTWaitForVerticalBlankEvent,
    IDX_EnableFeatureLevelUpgrade,
    IDX_OpenAdapter10,
    IDX_OpenAdapter10_2,
    IDX_D3D11CreateDeviceForD3D12,
    IDX_CreateDirect3D11DeviceFromDXGIDevice,
    IDX_CreateDirect3D11SurfaceFromDXGISurface,
    IDX_COUNT
};

static std::array<ForwardedExport, IDX_COUNT> g_forwardedExports = {{
    {"D3D11CoreCreateDevice", nullptr},
    {"D3D11CoreCreateLayeredDevice", nullptr},
    {"D3D11CoreGetLayeredDeviceSize", nullptr},
    {"D3D11CoreRegisterLayers", nullptr},
    {"D3DKMTCloseAdapter", nullptr},
    {"D3DKMTCreateAllocation", nullptr},
    {"D3DKMTCreateContext", nullptr},
    {"D3DKMTCreateDevice", nullptr},
    {"D3DKMTCreateSynchronizationObject", nullptr},
    {"D3DKMTDestroyAllocation", nullptr},
    {"D3DKMTDestroyContext", nullptr},
    {"D3DKMTDestroyDevice", nullptr},
    {"D3DKMTDestroySynchronizationObject", nullptr},
    {"D3DKMTEscape", nullptr},
    {"D3DKMTGetContextSchedulingPriority", nullptr},
    {"D3DKMTGetDeviceState", nullptr},
    {"D3DKMTGetDisplayModeList", nullptr},
    {"D3DKMTGetMultisampleMethodList", nullptr},
    {"D3DKMTGetRuntimeData", nullptr},
    {"D3DKMTGetSharedPrimaryHandle", nullptr},
    {"D3DKMTLock", nullptr},
    {"D3DKMTOpenAdapterFromHdc", nullptr},
    {"D3DKMTOpenResource", nullptr},
    {"D3DKMTPresent", nullptr},
    {"D3DKMTQueryAdapterInfo", nullptr},
    {"D3DKMTQueryAllocationResidency", nullptr},
    {"D3DKMTQueryResourceInfo", nullptr},
    {"D3DKMTRender", nullptr},
    {"D3DKMTSetAllocationPriority", nullptr},
    {"D3DKMTSetContextSchedulingPriority", nullptr},
    {"D3DKMTSetDisplayMode", nullptr},
    {"D3DKMTSetDisplayPrivateDriverFormat", nullptr},
    {"D3DKMTSetGammaRamp", nullptr},
    {"D3DKMTSetVidPnSourceOwner", nullptr},
    {"D3DKMTSignalSynchronizationObject", nullptr},
    {"D3DKMTUnlock", nullptr},
    {"D3DKMTWaitForSynchronizationObject", nullptr},
    {"D3DKMTWaitForVerticalBlankEvent", nullptr},
    {"EnableFeatureLevelUpgrade", nullptr},
    {"OpenAdapter10", nullptr},
    {"OpenAdapter10_2", nullptr},
    {"D3D11CreateDeviceForD3D12", nullptr},
    {"CreateDirect3D11DeviceFromDXGIDevice", nullptr},
    {"CreateDirect3D11SurfaceFromDXGISurface", nullptr},
}};

static void resolveForwardedExports()
{
    for (auto& entry : g_forwardedExports)
    {
        entry.proc = GetProcAddress(g_realD3D11, entry.name);
    }
}

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

    // Resolve all remaining forwarded exports
    resolveForwardedExports();

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

    for (auto& entry : g_forwardedExports)
    {
        entry.proc = nullptr;
    }

    OutputDebugStringA("[Bronco] D3D11 proxy shut down\n");
}

FARPROC getOriginalFunction(const char* name)
{
    if (!g_realD3D11) return nullptr;
    return GetProcAddress(g_realD3D11, name);
}

HMODULE getOurModule()
{
    return g_ourModule;
}

} // namespace bronco::proxy

// --- Global hook state ---
namespace {
    std::atomic<bool> g_presentHooked{false};

    /// Install the Present/ResizeBuffers hook by creating a temporary SwapChain
    /// from the given device. This is the standard technique used by overlays
    /// (arcdps, ReShade) to hook IDXGISwapChain::Present globally via VTable patching.
    void installHookViaDummySwapChain(ID3D11Device* device)
    {
        if (g_presentHooked.load()) return;

        // Query IDXGIDevice from the D3D11 device
        IDXGIDevice* dxgiDevice = nullptr;
        HRESULT hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
        if (FAILED(hr) || !dxgiDevice) return;

        // Get the DXGI adapter
        IDXGIAdapter* adapter = nullptr;
        hr = dxgiDevice->GetAdapter(&adapter);
        dxgiDevice->Release();
        if (FAILED(hr) || !adapter) return;

        // Get the DXGI factory
        IDXGIFactory* factory = nullptr;
        hr = adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
        adapter->Release();
        if (FAILED(hr) || !factory) return;

        // Register a temporary window class for the dummy window
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"BroncoDummyHookWnd";

        ATOM classAtom = RegisterClassExW(&wc);
        if (!classAtom)
        {
            // If the class already exists (e.g., from a previous partial attempt),
            // proceed -- we can still use the class name to create the window.
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                factory->Release();
                return;
            }
        }

        // Create a hidden 1x1 window
        HWND dummyHwnd = CreateWindowExW(
            0, L"BroncoDummyHookWnd", L"",
            WS_OVERLAPPED,
            0, 0, 1, 1,
            nullptr, nullptr, wc.hInstance, nullptr);

        if (!dummyHwnd)
        {
            UnregisterClassW(L"BroncoDummyHookWnd", wc.hInstance);
            factory->Release();
            return;
        }

        // Create a minimal swap chain on the dummy window
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        scDesc.BufferCount = 1;
        scDesc.BufferDesc.Width = 1;
        scDesc.BufferDesc.Height = 1;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferDesc.RefreshRate.Numerator = 60;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow = dummyHwnd;
        scDesc.SampleDesc.Count = 1;
        scDesc.Windowed = TRUE;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        IDXGISwapChain* tempSwapChain = nullptr;
        hr = factory->CreateSwapChain(device, &scDesc, &tempSwapChain);
        factory->Release();

        if (SUCCEEDED(hr) && tempSwapChain)
        {
            // Hook the VTable via the temporary swap chain.
            // All swap chains share the same VTable so this patches Present globally.
            bronco::hook::hookSwapChain(tempSwapChain);
            g_presentHooked.store(true);

            tempSwapChain->Release();
            OutputDebugStringA("[Bronco] Present hook installed via dummy SwapChain\n");
        }
        else
        {
            OutputDebugStringA("[Bronco] Failed to create dummy SwapChain for hook\n");
        }

        // Clean up dummy window
        DestroyWindow(dummyHwnd);
        UnregisterClassW(L"BroncoDummyHookWnd", wc.hInstance);
    }
} // anonymous namespace

// --- Forwarded exports (global scope for .def linkage) ---

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
    HRESULT hr = bronco::proxy::g_originalCreateDevice(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        ppDevice, pFeatureLevel, ppImmediateContext);

    // If device was created and we haven't hooked yet, install the Present hook
    // by creating a temporary SwapChain to access the VTable.
    if (SUCCEEDED(hr) && ppDevice && *ppDevice && !g_presentHooked.load())
    {
        installHookViaDummySwapChain(*ppDevice);
    }

    return hr;
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
    HRESULT hr = bronco::proxy::g_originalCreateDeviceAndSwapChain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain,
        ppDevice, pFeatureLevel, ppImmediateContext);

    // If swap chain was created, hook Present()
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && !g_presentHooked.load())
    {
        bronco::hook::hookSwapChain(*ppSwapChain);
        g_presentHooked.store(true);
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
    if (!bronco::proxy::g_originalD3D11On12CreateDevice)
        return E_NOTIMPL;

    return bronco::proxy::g_originalD3D11On12CreateDevice(
        pDevice, Flags, pFeatureLevels, FeatureLevels,
        ppCommandQueues, NumQueues, NodeMask,
        ppDevice, ppImmediateContext, pChosenFeatureLevel);
}

// --- Generic trampoline stubs for the remaining 44 exports ---
// D3DKMT functions have the signature: NTSTATUS WINAPI D3DKMTXxx(void* pData)
// They take a single pointer argument and return NTSTATUS.
// Non-D3DKMT forwards (D3D11Core*, OpenAdapter*, etc.) also take pointer args.
// We declare each trampoline with the proper signature to preserve both the
// argument and return value across the forwarding call.

// NTSTATUS is a LONG (typedef in Windows headers via ntdef.h / ntstatus.h).
// We use LONG here to avoid pulling in ntdef.h.
using NTSTATUS_PROXY = LONG;

#define DEFINE_TRAMPOLINE(exportName, idx) \
    NTSTATUS_PROXY WINAPI proxied_##exportName(void* pData) \
    { \
        auto proc = bronco::proxy::g_forwardedExports[bronco::proxy::idx].proc; \
        if (proc) \
        { \
            return reinterpret_cast<NTSTATUS_PROXY(WINAPI*)(void*)>(proc)(pData); \
        } \
        return 0; /* STATUS_SUCCESS when proc is not resolved */ \
    }

DEFINE_TRAMPOLINE(D3D11CoreCreateDevice, IDX_D3D11CoreCreateDevice)
DEFINE_TRAMPOLINE(D3D11CoreCreateLayeredDevice, IDX_D3D11CoreCreateLayeredDevice)
DEFINE_TRAMPOLINE(D3D11CoreGetLayeredDeviceSize, IDX_D3D11CoreGetLayeredDeviceSize)
DEFINE_TRAMPOLINE(D3D11CoreRegisterLayers, IDX_D3D11CoreRegisterLayers)
DEFINE_TRAMPOLINE(D3DKMTCloseAdapter, IDX_D3DKMTCloseAdapter)
DEFINE_TRAMPOLINE(D3DKMTCreateAllocation, IDX_D3DKMTCreateAllocation)
DEFINE_TRAMPOLINE(D3DKMTCreateContext, IDX_D3DKMTCreateContext)
DEFINE_TRAMPOLINE(D3DKMTCreateDevice, IDX_D3DKMTCreateDevice)
DEFINE_TRAMPOLINE(D3DKMTCreateSynchronizationObject, IDX_D3DKMTCreateSynchronizationObject)
DEFINE_TRAMPOLINE(D3DKMTDestroyAllocation, IDX_D3DKMTDestroyAllocation)
DEFINE_TRAMPOLINE(D3DKMTDestroyContext, IDX_D3DKMTDestroyContext)
DEFINE_TRAMPOLINE(D3DKMTDestroyDevice, IDX_D3DKMTDestroyDevice)
DEFINE_TRAMPOLINE(D3DKMTDestroySynchronizationObject, IDX_D3DKMTDestroySynchronizationObject)
DEFINE_TRAMPOLINE(D3DKMTEscape, IDX_D3DKMTEscape)
DEFINE_TRAMPOLINE(D3DKMTGetContextSchedulingPriority, IDX_D3DKMTGetContextSchedulingPriority)
DEFINE_TRAMPOLINE(D3DKMTGetDeviceState, IDX_D3DKMTGetDeviceState)
DEFINE_TRAMPOLINE(D3DKMTGetDisplayModeList, IDX_D3DKMTGetDisplayModeList)
DEFINE_TRAMPOLINE(D3DKMTGetMultisampleMethodList, IDX_D3DKMTGetMultisampleMethodList)
DEFINE_TRAMPOLINE(D3DKMTGetRuntimeData, IDX_D3DKMTGetRuntimeData)
DEFINE_TRAMPOLINE(D3DKMTGetSharedPrimaryHandle, IDX_D3DKMTGetSharedPrimaryHandle)
DEFINE_TRAMPOLINE(D3DKMTLock, IDX_D3DKMTLock)
DEFINE_TRAMPOLINE(D3DKMTOpenAdapterFromHdc, IDX_D3DKMTOpenAdapterFromHdc)
DEFINE_TRAMPOLINE(D3DKMTOpenResource, IDX_D3DKMTOpenResource)
DEFINE_TRAMPOLINE(D3DKMTPresent, IDX_D3DKMTPresent)
DEFINE_TRAMPOLINE(D3DKMTQueryAdapterInfo, IDX_D3DKMTQueryAdapterInfo)
DEFINE_TRAMPOLINE(D3DKMTQueryAllocationResidency, IDX_D3DKMTQueryAllocationResidency)
DEFINE_TRAMPOLINE(D3DKMTQueryResourceInfo, IDX_D3DKMTQueryResourceInfo)
DEFINE_TRAMPOLINE(D3DKMTRender, IDX_D3DKMTRender)
DEFINE_TRAMPOLINE(D3DKMTSetAllocationPriority, IDX_D3DKMTSetAllocationPriority)
DEFINE_TRAMPOLINE(D3DKMTSetContextSchedulingPriority, IDX_D3DKMTSetContextSchedulingPriority)
DEFINE_TRAMPOLINE(D3DKMTSetDisplayMode, IDX_D3DKMTSetDisplayMode)
DEFINE_TRAMPOLINE(D3DKMTSetDisplayPrivateDriverFormat, IDX_D3DKMTSetDisplayPrivateDriverFormat)
DEFINE_TRAMPOLINE(D3DKMTSetGammaRamp, IDX_D3DKMTSetGammaRamp)
DEFINE_TRAMPOLINE(D3DKMTSetVidPnSourceOwner, IDX_D3DKMTSetVidPnSourceOwner)
DEFINE_TRAMPOLINE(D3DKMTSignalSynchronizationObject, IDX_D3DKMTSignalSynchronizationObject)
DEFINE_TRAMPOLINE(D3DKMTUnlock, IDX_D3DKMTUnlock)
DEFINE_TRAMPOLINE(D3DKMTWaitForSynchronizationObject, IDX_D3DKMTWaitForSynchronizationObject)
DEFINE_TRAMPOLINE(D3DKMTWaitForVerticalBlankEvent, IDX_D3DKMTWaitForVerticalBlankEvent)
DEFINE_TRAMPOLINE(EnableFeatureLevelUpgrade, IDX_EnableFeatureLevelUpgrade)
DEFINE_TRAMPOLINE(OpenAdapter10, IDX_OpenAdapter10)
DEFINE_TRAMPOLINE(OpenAdapter10_2, IDX_OpenAdapter10_2)
DEFINE_TRAMPOLINE(D3D11CreateDeviceForD3D12, IDX_D3D11CreateDeviceForD3D12)
DEFINE_TRAMPOLINE(CreateDirect3D11DeviceFromDXGIDevice, IDX_CreateDirect3D11DeviceFromDXGIDevice)
DEFINE_TRAMPOLINE(CreateDirect3D11SurfaceFromDXGISurface, IDX_CreateDirect3D11SurfaceFromDXGISurface)

#undef DEFINE_TRAMPOLINE

} // extern "C"
