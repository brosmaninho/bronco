#pragma once

#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>

namespace bronco::hook {

/// Uninstall the Present() hook and clean up (called from DllMain on detach).
void uninstall();

/// Hook a specific swap chain's vtable to intercept Present() and ResizeBuffers().
/// Called when a D3D11 device is created (from proxied_D3D11CreateDevice via dummy
/// SwapChain, or from proxied_D3D11CreateDeviceAndSwapChain with the real one).
/// Only hooks once - subsequent calls are no-ops.
void hookSwapChain(IDXGISwapChain* swapChain);

} // namespace bronco::hook
