#include "proxy/d3d11_proxy.h"
#include "hook/present_hook.h"
#include "config/config.h"

#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        // Load configuration
        bronco::Config::instance().load();

        // Initialize the D3D11 proxy (loads real d3d11.dll from System32)
        if (!bronco::proxy::initialize(hModule))
        {
            return FALSE;
        }

        // Install the Present() hook
        bronco::hook::install();
        break;

    case DLL_PROCESS_DETACH:
        // Clean up in reverse order
        bronco::hook::uninstall();
        bronco::proxy::shutdown();
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }

    return TRUE;
}
