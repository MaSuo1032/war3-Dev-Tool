#include <windows.h>
#include "Common/Config.hpp"
#include "Common/Logger.hpp"
#include "Game/GameDefines.hpp"
#include "Game/Selection.hpp"
#include "Game/AntiDefeat.hpp"
#include "Jass/NativeRegistry.hpp"
#include "Jass/NativeInvoker.hpp"
#include "Modules/MallSpoofer.hpp"
#include "Modules/SaveManager.hpp"
#include "IPC/PipeServer.hpp"

namespace War3Dev {

static WNDPROC s_origWndProc = nullptr;
static HWND s_gameWnd = NULL;

struct EnumWindowData {
    DWORD pid;
    HWND hWnd;
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumWindowData* data = reinterpret_cast<EnumWindowData*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->pid) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "Warcraft III") == 0 || strcmp(className, "OsWindow") == 0 || strstr(className, "Warcraft") != NULL) {
            data->hWnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_WAR3_KILL_UNIT) {
        Selection::KillSelectedUnit();
        return 0;
    }

    if (uMsg == WM_WAR3_SAVE_SERVER) {
        SavePayload* payload = reinterpret_cast<SavePayload*>(lParam);
        if (payload) {
            SaveManager::Get().ExecuteManualSave(payload);
            delete payload;
        }
        return 0;
    }

    if (uMsg == WM_WAR3_INVOKE_NATIVE) {
        InvokePayload* payload = reinterpret_cast<InvokePayload*>(lParam);
        if (payload) {
            NativeInvoker::Execute(payload);
            delete payload;
        }
        return 0;
    }

    return CallWindowProcA(s_origWndProc, hWnd, uMsg, wParam, lParam);
}

static DWORD WINAPI MainThread(LPVOID lpParam) {
    Logger::Get().Initialize();

    LOG("========================================================\n");
    LOG(" Warcraft III JASS Dev Tool v2.0 (Modern Modular Core)\n");
    LOG("========================================================\n");

    // Wait for Game.dll
    HMODULE hGame = GetModuleHandleA("Game.dll");
    while (!hGame) {
        Sleep(100);
        hGame = GetModuleHandleA("Game.dll");
    }

    // Initialize Native Registry & RegisterNative Hook
    NativeRegistry::Get().Initialize(hGame);

    HMODULE hPlugin = GetModuleHandleA("War3Plugin.dll");
    NativeRegistry::Get().PrepopulateFromModules(hGame, hPlugin);

    // Initialize Anti-Defeat
    AntiDefeat::Get().Initialize();

    // Initialize Modules if DzAPI is available
    if (hPlugin) {
        MallSpoofer::Get().Initialize();
        SaveManager::Get().Initialize();
    }

    // Locate and hook Game Window
    EnumWindowData edata = { GetCurrentProcessId(), NULL };
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&edata));
    if (edata.hWnd) {
        s_gameWnd = edata.hWnd;
        s_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongA(edata.hWnd, GWL_WNDPROC, reinterpret_cast<LONG>(HookedWndProc)));
        LOG("[Main] Warcraft III Window hooked successfully (HWND: 0x%p)\n", s_gameWnd);
    } else {
        LOG("[-] Main: Could not locate Warcraft III window!\n");
    }

    // Start Named Pipe Server
    PipeServer::Get().Start(s_gameWnd);
    LOG("[Main] Named Pipe Server online. Ready for client connections.\n");

    return 0;
}

} // namespace War3Dev

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        War3Dev::Config::Get().Initialize(hModule);
        CreateThread(NULL, 0, War3Dev::MainThread, hModule, 0, NULL);
    }
    return TRUE;
}
