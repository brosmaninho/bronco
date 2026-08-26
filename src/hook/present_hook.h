#pragma once

#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>

namespace bronco::hook {

/// Uninstall the Present() hook and clean up (called from DllMain on detach).
void uninstall();

/// Install inline hooks on IDXGISwapChain::Present() and ResizeBuffers() via MinHook.
/// Uses the given swap chain's VTable to locate the target function addresses, then
/// creates inline hooks (patching the function prologue) instead of VTable patching.
/// This is safe for all GPU drivers including AMD, which caches VTable pointers internally.
/// Called when a D3D11 device is created (from proxied_D3D11CreateDevice via dummy
/// SwapChain, or from proxied_D3D11CreateDeviceAndSwapChain with the real one).
/// Only hooks once - subsequent calls are no-ops.
void hookSwapChain(IDXGISwapChain* swapChain);

} // namespace bronco::hook
