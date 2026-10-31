#include <windows.h>

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
#ifdef REJECT_PAYLOAD
    if (reason == DLL_PROCESS_ATTACH) return FALSE;
#endif
    return TRUE;
}
