#include "present_hook.h"
#include "../overlay/imgui_overlay.h"

#include <atomic>
#include <mutex>

namespace bronco::hook {

namespace {
    // Original Present function pointer
    using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    PFN_Present g_originalPresent = nullptr;

    // Hook state
    std::atomic<bool> g_hookInstalled{false};
    std::mutex g_hookMutex;

    // Device context saved from first Present call
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    bool g_overlayInitialized = false;

    // Our hooked Present implementation
    HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        // Initialize overlay on first call
        if (!g_overlayInitialized)
        {
            // Get the device from the swap chain
            HRESULT hr = swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device));
            if (SUCCEEDED(hr) && g_device)
            {
                g_device->GetImmediateContext(&g_context);

                // Get swap chain description for window handle
                DXGI_SWAP_CHAIN_DESC desc = {};
                swapChain->GetDesc(&desc);

                if (bronco::overlay::initialize(desc.OutputWindow, g_device, g_context))
                {
                    g_overlayInitialized = true;
                    OutputDebugStringA("[Bronco] Overlay initialized on first Present()\n");
                }
            }
        }

        // Render our overlay before presenting
        if (g_overlayInitialized)
        {
            bronco::overlay::render(swapChain);
        }

        // Call the original Present
        return g_originalPresent(swapChain, syncInterval, flags);
    }
} // anonymous namespace

void install()
{
    g_hookInstalled.store(true);
    OutputDebugStringA("[Bronco] Present hook system ready\n");
}

void uninstall()
{
    std::lock_guard<std::mutex> lock(g_hookMutex);

    if (g_overlayInitialized)
    {
        bronco::overlay::shutdown();
        g_overlayInitialized = false;
    }

    if (g_context)
    {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device)
    {
        g_device->Release();
        g_device = nullptr;
    }

    g_hookInstalled.store(false);
    OutputDebugStringA("[Bronco] Present hook uninstalled\n");
}

void hookSwapChain(IDXGISwapChain* swapChain)
{
    if (!swapChain || g_originalPresent) return;

    std::lock_guard<std::mutex> lock(g_hookMutex);

    // Get the vtable of the swap chain
    void** vtable = *reinterpret_cast<void***>(swapChain);

    // Present is at index 8 in IDXGISwapChain vtable
    // (IUnknown: 0-2, IDXGIObject: 3-6, IDXGIDeviceSubObject: 7, Present: 8)
    constexpr int PRESENT_VTABLE_INDEX = 8;

    // Save original
    g_originalPresent = reinterpret_cast<PFN_Present>(vtable[PRESENT_VTABLE_INDEX]);

    // Patch the vtable to point to our hook
    DWORD oldProtect = 0;
    VirtualProtect(&vtable[PRESENT_VTABLE_INDEX], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    vtable[PRESENT_VTABLE_INDEX] = reinterpret_cast<void*>(&hookedPresent);
    VirtualProtect(&vtable[PRESENT_VTABLE_INDEX], sizeof(void*), oldProtect, &oldProtect);

    OutputDebugStringA("[Bronco] SwapChain::Present() hooked successfully\n");
}

} // namespace bronco::hook
