#include "present_hook.h"
#include "../overlay/imgui_overlay.h"
#include "../config/config.h"
#include "../pipeline/pipeline.h"

#include <atomic>
#include <mutex>

namespace bronco::hook {

namespace {
    // Original function pointers
    using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    PFN_Present g_originalPresent = nullptr;
    PFN_ResizeBuffers g_originalResizeBuffers = nullptr;

    // Hook state
    std::atomic<bool> g_hookInstalled{false};
    std::mutex g_hookMutex;

    // Device context saved from first Present call
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    bool g_overlayInitialized = false;

    // Forward declaration
    void cleanupDeviceObjects();

    // Our hooked ResizeBuffers - releases render target before resize
    HRESULT STDMETHODCALLTYPE hookedResizeBuffers(
        IDXGISwapChain* swapChain,
        UINT bufferCount,
        UINT width,
        UINT height,
        DXGI_FORMAT newFormat,
        UINT swapChainFlags)
    {
        // Must release the render target view before ResizeBuffers
        bronco::overlay::invalidateRenderTarget();

        // Call the original ResizeBuffers
        HRESULT hr = g_originalResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags);

        // Re-create render target after successful resize
        if (SUCCEEDED(hr))
        {
            OutputDebugStringA("[Bronco] SwapChain resized, render target invalidated\n");
        }

        return hr;
    }

    // Our hooked Present implementation
    HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        // Initialize overlay on first call (deferred from DllMain to avoid loader lock)
        if (!g_overlayInitialized)
        {
            // Load config on first Present (deferred from DllMain - Issue #8)
            bronco::Config::instance().ensureLoaded();

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

                    // Initialize the OCR-to-overlay pipeline
                    bronco::pipeline::instance().initialize(g_device, g_context, swapChain);

                    OutputDebugStringA("[Bronco] Overlay initialized on first Present()\n");
                }
                else
                {
                    // Initialization failed, release refs
                    cleanupDeviceObjects();
                }
            }
        }

        // Run the OCR pipeline (captures at configured interval, non-blocking)
        if (g_overlayInitialized)
        {
            bronco::pipeline::instance().onPresent(swapChain);
        }

        // Render our overlay before presenting
        if (g_overlayInitialized)
        {
            bronco::overlay::render(swapChain);
        }

        // Call the original Present
        return g_originalPresent(swapChain, syncInterval, flags);
    }

    void cleanupDeviceObjects()
    {
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

    // Shut down pipeline first (stops background OCR thread)
    bronco::pipeline::instance().shutdown();

    if (g_overlayInitialized)
    {
        bronco::overlay::shutdown();
        g_overlayInitialized = false;
    }

    cleanupDeviceObjects();

    g_hookInstalled.store(false);
    OutputDebugStringA("[Bronco] Present hook uninstalled\n");
}

void hookSwapChain(IDXGISwapChain* swapChain)
{
    if (!swapChain || g_originalPresent) return;

    std::lock_guard<std::mutex> lock(g_hookMutex);

    // Double-check after acquiring lock
    if (g_originalPresent) return;

    // Get the vtable of the swap chain
    void** vtable = *reinterpret_cast<void***>(swapChain);

    // Present is at index 8 in IDXGISwapChain vtable
    // (IUnknown: 0-2, IDXGIObject: 3-6, IDXGIDeviceSubObject: 7, Present: 8)
    constexpr int PRESENT_VTABLE_INDEX = 8;
    // ResizeBuffers is at index 13
    constexpr int RESIZE_BUFFERS_VTABLE_INDEX = 13;

    // Save originals before patching
    g_originalPresent = reinterpret_cast<PFN_Present>(vtable[PRESENT_VTABLE_INDEX]);
    g_originalResizeBuffers = reinterpret_cast<PFN_ResizeBuffers>(vtable[RESIZE_BUFFERS_VTABLE_INDEX]);

    // Patch Present vtable entry
    DWORD oldProtect = 0;
    BOOL vpResult = VirtualProtect(
        &vtable[PRESENT_VTABLE_INDEX], sizeof(void*),
        PAGE_EXECUTE_READWRITE, &oldProtect);

    if (!vpResult)
    {
        // VirtualProtect failed - another overlay or anti-cheat may have locked the page.
        // Abort hook to prevent undefined behavior.
        OutputDebugStringA("[Bronco] VirtualProtect failed for Present vtable slot - aborting hook\n");
        g_originalPresent = nullptr;
        g_originalResizeBuffers = nullptr;
        return;
    }

    vtable[PRESENT_VTABLE_INDEX] = reinterpret_cast<void*>(&hookedPresent);
    VirtualProtect(&vtable[PRESENT_VTABLE_INDEX], sizeof(void*), oldProtect, &oldProtect);

    // Patch ResizeBuffers vtable entry
    oldProtect = 0;
    vpResult = VirtualProtect(
        &vtable[RESIZE_BUFFERS_VTABLE_INDEX], sizeof(void*),
        PAGE_EXECUTE_READWRITE, &oldProtect);

    if (!vpResult)
    {
        OutputDebugStringA("[Bronco] VirtualProtect failed for ResizeBuffers vtable slot\n");
        // Present is already hooked so we continue, but ResizeBuffers won't be intercepted
    }
    else
    {
        vtable[RESIZE_BUFFERS_VTABLE_INDEX] = reinterpret_cast<void*>(&hookedResizeBuffers);
        VirtualProtect(&vtable[RESIZE_BUFFERS_VTABLE_INDEX], sizeof(void*), oldProtect, &oldProtect);
    }

    OutputDebugStringA("[Bronco] SwapChain::Present() and ResizeBuffers() hooked successfully\n");
}

} // namespace bronco::hook
