#include "acfbasetypes.h"

#if ACF_WIN

#include <windows.h>

static HANDLE hPlugInInstance = 0;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        hPlugInInstance = hModule;
        break;
    case DLL_PROCESS_DETACH:
        hPlugInInstance = 0;
        break;
    default:
        break;
    }
    return TRUE;
}

#endif
