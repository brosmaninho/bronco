#include "proxy/d3d11_proxy.h"
#include "hook/present_hook.h"

#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        // DllMain must be minimal - only initialize the proxy (load real d3d11.dll
        // from System32 and resolve the 3 main function pointers + forwarded exports).
        // The Present hook is installed lazily from proxied_D3D11CreateDevice or
        // proxied_D3D11CreateDeviceAndSwapChain when the game creates a device.
        if (!bronco::proxy::initialize(hModule))
        {
            return FALSE;
        }
        break;

    case DLL_PROCESS_DETACH:
        bronco::hook::uninstall();
        bronco::proxy::shutdown();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
