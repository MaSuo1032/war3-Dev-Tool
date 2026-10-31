#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cwchar>
#include <string>

// Enable SeDebugPrivilege for current process
inline bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(0);
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD error = GetLastError();
    CloseHandle(token);
    return ok && error == 0;
}

// Find Warcraft III process ID
inline DWORD FindProcessId() {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    
    DWORD pid = 0;
    DWORD launcherPid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (lstrcmpiW(pe.szExeFile, L"war3.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
            if (lstrcmpiW(pe.szExeFile, L"Warcraft III.exe") == 0) launcherPid = pe.th32ProcessID;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    // The small Warcraft III.exe launcher often exists before the actual game.
    // Only use it as a fallback if it really hosts Game.dll.
    if (!pid && launcherPid) {
        HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, launcherPid);
        if (modules != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            if (Module32FirstW(modules, &module)) do {
                if (lstrcmpiW(module.szModule, L"Game.dll") == 0) {
                    pid = launcherPid;
                    break;
                }
            } while (Module32NextW(modules, &module));
            CloseHandle(modules);
        }
    }
    return pid;
}

// Check if a DLL is already injected in target process
inline bool IsDllInjected(DWORD pid, const std::wstring& dllName) {
    if (pid == 0) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (lstrcmpiW(me.szModule, dllName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Inject DLL into target process via CreateRemoteThread
inline bool InjectDLL(DWORD processID, const std::wstring& dllPath, std::wstring& outErr) {
    outErr.clear();
    DWORD attributes = GetFileAttributesW(dllPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        outErr = L"找不到 DLL: " + dllPath;
        return false;
    }
    EnableDebugPrivilege();
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        DWORD err = GetLastError();
        outErr = L"OpenProcess 失敗 (錯誤碼 " + std::to_wstring(err) + L")，請確認是否具備系統管理員身分。";
        return false;
    }

    BOOL selfWow64 = FALSE, targetWow64 = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &selfWow64) ||
        !IsWow64Process(hProcess, &targetWow64) || selfWow64 != targetWow64) {
        outErr = L"注入器與目標程序的位元架構不相容；請使用 Win32 (x86) 建置。";
        CloseHandle(hProcess);
        return false;
    }

    // Resolve the exported function relative to its owning remote module. The
    // LoadLibraryW export may be forwarded from kernel32 to KernelBase.
    auto loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    uintptr_t remoteOwner = 0;
    wchar_t ownerPath[MAX_PATH]{};
    if (loader && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(loader), &owner)) {
        GetModuleFileNameW(owner, ownerPath, MAX_PATH);
        const wchar_t* ownerName = wcsrchr(ownerPath, L'\\');
        ownerName = ownerName ? ownerName + 1 : ownerPath;
        HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
        if (modules != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            if (Module32FirstW(modules, &module)) do {
                if (lstrcmpiW(module.szModule, ownerName) == 0) {
                    remoteOwner = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                    break;
                }
            } while (Module32NextW(modules, &module));
            CloseHandle(modules);
        }
    }
    if (!remoteOwner) {
        outErr = L"無法定位目標程序的 LoadLibraryW；目標可能尚未初始化或已結束。";
        CloseHandle(hProcess);
        return false;
    }
    auto remoteLoader = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner +
        reinterpret_cast<uintptr_t>(loader) - reinterpret_cast<uintptr_t>(owner));

    const SIZE_T pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* loc = VirtualAllocEx(hProcess, 0, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        DWORD err = GetLastError();
        outErr = L"VirtualAllocEx 記憶體配置失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        CloseHandle(hProcess);
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, loc, dllPath.c_str(), pathBytes, &written) || written != pathBytes) {
        DWORD err = GetLastError();
        outErr = L"WriteProcessMemory 失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, remoteLoader, loc, 0, 0);
    if (!hThread) {
        DWORD err = GetLastError();
        outErr = L"CreateRemoteThread 遠端線程創建失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    DWORD waitResult = WaitForSingleObject(hThread, 10000);
    DWORD loadedModule = 0;
    bool loaded = waitResult == WAIT_OBJECT_0 && GetExitCodeThread(hThread, &loadedModule) && loadedModule != 0;
    if (loaded) {
        // An exception code is also a nonzero thread exit code. Confirm that the
        // returned HMODULE really belongs to a loaded module in the target.
        loaded = false;
        HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
        if (modules != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W module{};
            module.dwSize = sizeof(module);
            if (Module32FirstW(modules, &module)) do {
                if (reinterpret_cast<uintptr_t>(module.modBaseAddr) == loadedModule) {
                    loaded = true;
                    break;
                }
            } while (Module32NextW(modules, &module));
            CloseHandle(modules);
        }
    }
    if (waitResult == WAIT_OBJECT_0) {
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
    }
    // On a timeout the loader may still be reading the path. Keep that small
    // allocation alive until the target exits instead of freeing it underneath it.
    if (!loaded) {
        if (waitResult == WAIT_TIMEOUT) {
            outErr = L"DLL 載入超時，無法確認成功；請查看 dztrace.log 與遊戲狀態。";
        } else {
            outErr = L"目標程序未成功載入 DLL；請檢查 x86 架構、相依元件與 DLL 初始化錯誤。";
        }
    }
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return loaded;
}
