#include <windows.h>
#include <iostream>
#include <string>
#include "ProcessInjector.h"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const bool once = argc == 2 && std::string(argv[1]) == "--once";
    if (argc > 1 && !once) {
        std::cerr << "Usage: Injector.exe [--once]\n";
        return 2;
    }
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return 1;
    std::wstring dllPath(path, length);
    dllPath.erase(dllPath.find_last_of(L"\\/") + 1);
    dllPath += L"JassHook.dll";
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[-] JassHook.dll is missing next to Injector.exe.\n";
        return 1;
    }

    std::cout << "War3DevTool Injector (x86)\n";
    DWORD pid = FindProcessId();
    if (!pid && !once) std::cout << "Waiting for Warcraft III...\n";
    while (!pid && !once) {
        Sleep(250);
        pid = FindProcessId();
    }
    if (!pid) {
        std::cerr << "[-] Warcraft III is not running.\n";
        return 1;
    }
    if (IsDllInjected(pid, L"JassHook.dll")) {
        std::cout << "[+] JassHook.dll is already loaded. Check dztrace.log for core status.\n";
        return 0;
    }
    std::wstring error;
    if (!InjectDLL(pid, dllPath, error)) {
        int size = WideCharToMultiByte(CP_UTF8, 0, error.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, error.c_str(), -1, &utf8[0], size, nullptr, nullptr);
        std::cerr << "[-] " << utf8.c_str() << '\n';
        return 1;
    }
    std::cout << "[+] DLL load confirmed. Check dztrace.log for initialization and IPC readiness.\n";
    return 0;
}
