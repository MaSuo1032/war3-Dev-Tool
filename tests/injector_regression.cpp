#include "ProcessInjector.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--target") == 0) {
        Sleep(30000);
        return 0;
    }
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring dir = exe;
    dir.erase(dir.find_last_of(L"\\/") + 1);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --target";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(exe, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, dir.c_str(), &startup, &child)) return 1;
    Sleep(100);
    int failed = 0;
    auto check = [&](bool passed, const char* name) {
        printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
        if (!passed) ++failed;
    };
    std::wstring error;
    check(!InjectDLL(child.dwProcessId, dir + L"missing.dll", error) && !error.empty(), "missing DLL fails");
    check(!InjectDLL(0, dir + L"InjectorPayload.dll", error) && !error.empty(), "invalid PID fails");
    check(!InjectDLL(child.dwProcessId, dir + L"InjectorReject.dll", error) && !error.empty(), "DllMain rejection is not success");
    check(InjectDLL(child.dwProcessId, dir + L"InjectorPayload.dll", error) && error.empty(), "remote LoadLibraryW succeeds");
    check(IsDllInjected(child.dwProcessId, L"InjectorPayload.dll"), "remote module is actually present");
    // Exercise a path containing non-ANSI characters as well as the normal path.
    std::wstring unicodePath = dir + L"payload_\x6e2c\x8a66.dll";
    if (CopyFileW((dir + L"InjectorPayload.dll").c_str(), unicodePath.c_str(), FALSE)) {
        check(InjectDLL(child.dwProcessId, unicodePath, error), "Unicode DLL path succeeds");
    } else check(false, "create Unicode fixture");
    TerminateProcess(child.hProcess, 0);
    WaitForSingleObject(child.hProcess, 5000);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    DeleteFileW(unicodePath.c_str());
    return failed ? 1 : 0;
}
