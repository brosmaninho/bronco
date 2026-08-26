#pragma once

#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>

namespace bronco::hook {

/// Install the Present() hook (called from DllMain on attach).
void install();

/// Uninstall the Present() hook (called from DllMain on detach).
void uninstall();

/// Hook a specific swap chain's vtable to intercept Present().
/// Called when D3D11CreateDeviceAndSwapChain succeeds.
void hookSwapChain(IDXGISwapChain* swapChain);

} // namespace bronco::hook
