#include "present_hook.h"
#include "../overlay/imgui_overlay.h"
#include "../config/config.h"
#include "../pipeline/pipeline.h"
#include "../log/logger.h"

#include <MinHook.h>
#include <mutex>
#include <string>

namespace bronco::hook {

namespace {
    // Original function pointer typedefs
    using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    // Trampoline pointers (provided by MinHook - call these to invoke the original)
    PFN_Present g_trampolinePresent = nullptr;
    PFN_ResizeBuffers g_trampolineResizeBuffers = nullptr;

    // Target function addresses (read from the VTable, used as hook targets)
    void* g_targetPresent = nullptr;
    void* g_targetResizeBuffers = nullptr;

    // Whether MinHook has been initialized
    bool g_minhookInitialized = false;

    // Hook mutex (guards hookSwapChain and uninstall)
    std::mutex g_hookMutex;

    // Device context saved from first Present call
    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    bool g_overlayInitialized = false;

    // Toggle chord (Right Ctrl + Space) debounce state for GetAsyncKeyState polling
    bool g_toggleChordWasPressed = false;

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

        // Invalidate the pipeline's staging texture so it is recreated with new dimensions
        bronco::pipeline::instance().invalidateStagingTexture();

        // Call the original ResizeBuffers via trampoline (null check for safety)
        if (!g_trampolineResizeBuffers)
            return E_FAIL;

        HRESULT hr = g_trampolineResizeBuffers(swapChain, bufferCount, width, height, newFormat, swapChainFlags);

        // Re-create render target after successful resize
        if (SUCCEEDED(hr))
        {
            bronco::log::info("hookedResizeBuffers: SwapChain resized, render target and staging texture invalidated");
        }

        return hr;
    }

    // Our hooked Present implementation
    HRESULT STDMETHODCALLTYPE hookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        // Initialize overlay on first call (deferred from DllMain to avoid loader lock)
        if (!g_overlayInitialized)
        {
            bronco::log::info("hookedPresent: called for first time");

            // Load config on first Present (deferred from DllMain)
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
                    bronco::log::info("hookedPresent: overlay::initialize succeeded");

                    // Initialize the OCR-to-overlay pipeline (non-blocking, failure is non-fatal)
                    if (bronco::pipeline::instance().initialize(g_device, g_context, swapChain))
                    {
                        bronco::log::info("hookedPresent: pipeline initialized OK");
                    }
                    else
                    {
                        bronco::log::error("hookedPresent: pipeline initialization failed (overlay continues without OCR)");
                    }
                }
                else
                {
                    bronco::log::error("hookedPresent: overlay::initialize failed");
                    // Initialization failed, release refs
                    cleanupDeviceObjects();
                }
            }
            else
            {
                bronco::log::error("hookedPresent: GetDevice from SwapChain failed");
            }
        }

        // Poll GetAsyncKeyState for the Right Ctrl + Space toggle chord (works
        // even if the WndProc hook fails). The chord requires BOTH keys down;
        // merely holding Right Ctrl (the drag-panel modifier) never toggles.
        if (g_overlayInitialized)
        {
            bool chordPressed =
                ((GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0) &&
                ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
            if (chordPressed && !g_toggleChordWasPressed)
            {
                // Rising edge: chord was not fully down last frame, is now.
                bronco::overlay::toggleVisibility();
                bronco::log::info("hookedPresent: toggle via Right Ctrl + Space");
            }
            g_toggleChordWasPressed = chordPressed;
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

        // Call the original Present via trampoline
        return g_trampolinePresent(swapChain, syncInterval, flags);
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

void uninstall()
{
    std::lock_guard<std::mutex> lock(g_hookMutex);

    // Disable all MinHook hooks and uninitialize
    if (g_minhookInitialized)
    {
        MH_DisableHook(MH_ALL_HOOKS);

        // Allow in-flight hooked calls to drain before tearing down state.
        // MH_DisableHook restores the prologue but does not fence threads that
        // are already past the hook entry point. This delay (same approach as
        // ReShade) gives those threads time to return before we destroy objects
        // they may still reference.
        Sleep(100);

        MH_Uninitialize();
        g_minhookInitialized = false;
    }

    // Shut down pipeline (stops background OCR thread)
    bronco::pipeline::instance().shutdown();

    if (g_overlayInitialized)
    {
        bronco::overlay::shutdown();
        g_overlayInitialized = false;
    }

    cleanupDeviceObjects();

    g_trampolinePresent = nullptr;
    g_trampolineResizeBuffers = nullptr;
    g_targetPresent = nullptr;
    g_targetResizeBuffers = nullptr;

    bronco::log::info("hook::uninstall: Present hook uninstalled (MinHook cleaned up)");
}

void hookSwapChain(IDXGISwapChain* swapChain)
{
    if (!swapChain || g_trampolinePresent) return;

    std::lock_guard<std::mutex> lock(g_hookMutex);

    // Double-check after acquiring lock
    if (g_trampolinePresent) return;

    // Get the vtable of the swap chain
    void** vtable = *reinterpret_cast<void***>(swapChain);

    // Present is at index 8 in IDXGISwapChain vtable
    // (IUnknown: 0-2, IDXGIObject: 3-6, IDXGIDeviceSubObject: 7, Present: 8)
    constexpr int PRESENT_VTABLE_INDEX = 8;
    // ResizeBuffers is at index 13
    constexpr int RESIZE_BUFFERS_VTABLE_INDEX = 13;

    // Read the target function addresses from the VTable
    g_targetPresent = vtable[PRESENT_VTABLE_INDEX];
    g_targetResizeBuffers = vtable[RESIZE_BUFFERS_VTABLE_INDEX];

    // Initialize MinHook
    if (!g_minhookInitialized)
    {
        MH_STATUS status = MH_Initialize();
        std::string msg = "hookSwapChain: MH_Initialize result: " + std::to_string(static_cast<int>(status));
        bronco::log::info(msg.c_str());

        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
        {
            bronco::log::error("hookSwapChain: MH_Initialize() failed - aborting hook");
            g_targetPresent = nullptr;
            g_targetResizeBuffers = nullptr;
            return;
        }
        g_minhookInitialized = true;
    }

    // Create inline hook for Present
    MH_STATUS status = MH_CreateHook(
        g_targetPresent,
        reinterpret_cast<LPVOID>(&hookedPresent),
        reinterpret_cast<LPVOID*>(&g_trampolinePresent));

    {
        std::string msg = "hookSwapChain: MH_CreateHook Present result: " + std::to_string(static_cast<int>(status));
        bronco::log::info(msg.c_str());
    }

    if (status != MH_OK)
    {
        bronco::log::error("hookSwapChain: MH_CreateHook failed for Present - aborting hook");
        g_targetPresent = nullptr;
        g_targetResizeBuffers = nullptr;
        return;
    }

    // Create inline hook for ResizeBuffers
    status = MH_CreateHook(
        g_targetResizeBuffers,
        reinterpret_cast<LPVOID>(&hookedResizeBuffers),
        reinterpret_cast<LPVOID*>(&g_trampolineResizeBuffers));

    {
        std::string msg = "hookSwapChain: MH_CreateHook ResizeBuffers result: " + std::to_string(static_cast<int>(status));
        bronco::log::info(msg.c_str());
    }

    if (status != MH_OK)
    {
        bronco::log::error("hookSwapChain: MH_CreateHook failed for ResizeBuffers - aborting entire hook");
        // Roll back the Present hook to avoid partial-hook state where the overlay
        // never sees resize events, leading to stale render targets on window resize.
        MH_RemoveHook(g_targetPresent);
        g_trampolinePresent = nullptr;
        g_trampolineResizeBuffers = nullptr;
        g_targetPresent = nullptr;
        g_targetResizeBuffers = nullptr;
        return;
    }

    // Enable the Present hook
    status = MH_EnableHook(g_targetPresent);
    {
        std::string msg = "hookSwapChain: MH_EnableHook Present result: " + std::to_string(static_cast<int>(status));
        bronco::log::info(msg.c_str());
    }

    if (status != MH_OK)
    {
        bronco::log::error("hookSwapChain: MH_EnableHook failed for Present - aborting");
        MH_RemoveHook(g_targetPresent);
        MH_RemoveHook(g_targetResizeBuffers);
        g_trampolinePresent = nullptr;
        g_trampolineResizeBuffers = nullptr;
        g_targetPresent = nullptr;
        g_targetResizeBuffers = nullptr;
        return;
    }

    // Enable the ResizeBuffers hook
    status = MH_EnableHook(g_targetResizeBuffers);
    {
        std::string msg = "hookSwapChain: MH_EnableHook ResizeBuffers result: " + std::to_string(static_cast<int>(status));
        bronco::log::info(msg.c_str());
    }

    if (status != MH_OK)
    {
        bronco::log::error("hookSwapChain: MH_EnableHook failed for ResizeBuffers - aborting entire hook");
        // Roll back everything to avoid partial-hook state
        MH_DisableHook(g_targetPresent);
        MH_RemoveHook(g_targetPresent);
        MH_RemoveHook(g_targetResizeBuffers);
        g_trampolinePresent = nullptr;
        g_trampolineResizeBuffers = nullptr;
        g_targetPresent = nullptr;
        g_targetResizeBuffers = nullptr;
        return;
    }

    bronco::log::info("hookSwapChain: Present() and ResizeBuffers() hooked via MinHook (inline hook)");
}

} // namespace bronco::hook
