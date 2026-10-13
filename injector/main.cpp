#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

bool EnableDebugPrivilege() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    LUID luid;
    if (!LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &luid)) {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return result && (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
}

bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

DWORD FindProcessId(const std::wstring& processName) {
    DWORD pid = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return pid;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    // 1. Try targeted injection permissions first
    DWORD desiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
                          PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    HANDLE hProcess = OpenProcess(desiredAccess, FALSE, pid);

    // Fallback to PROCESS_ALL_ACCESS if needed
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    }

    if (!hProcess) {
        DWORD err = GetLastError();
        std::wcout << L"[-] Failed to open process (PID: " << pid << L"). Error code: " << err << L"\n";
        if (err == ERROR_ACCESS_DENIED) {
            std::wcout << L"    [提示] 存取被拒 (Error 5)！請以「系統管理員身分」執行 Injector.exe。\n";
            if (!IsRunAsAdmin()) {
                std::wcout << L"    [原因] 當前程式非管理員權限，無法存取已提權之 war3.exe。\n";
            }
        }
        return false;
    }

    size_t size = (dllPath.length() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        std::wcout << L"[-] VirtualAllocEx failed in target process. Error: " << GetLastError() << L"\n";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), size, NULL)) {
        std::wcout << L"[-] WriteProcessMemory failed. Error: " << GetLastError() << L"\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPTHREAD_START_ROUTINE pfnLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW"));

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnLoadLibrary, remoteMem, 0, NULL);
    if (!hThread) {
        std::wcout << L"[-] CreateRemoteThread failed. Error: " << GetLastError() << L"\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        std::wcout << L"[-] LoadLibraryW in target process returned NULL. Injection might have failed.\n";
        return false;
    }

    std::wcout << L"[+] JassHook.dll injected successfully into PID " << pid << L"! Remote Module: 0x" << std::hex << exitCode << L"\n";
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========================================\n";
    std::cout << " Warcraft III JassHook Injector v2.0\n";
    std::cout << "========================================\n";

    if (!EnableDebugPrivilege()) {
        std::cout << "[*] SeDebugPrivilege could not be enabled (might need Administrator).\n";
    } else {
        std::cout << "[+] SeDebugPrivilege enabled successfully.\n";
    }

    if (IsRunAsAdmin()) {
        std::cout << "[+] Running with Administrator privileges.\n";
    } else {
        std::cout << "[!] WARNING: Not running as Administrator. If injection fails with Error 5, please Run as Administrator.\n";
    }

    wchar_t currentDir[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, currentDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(currentDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    std::wstring dllPath = std::wstring(currentDir) + L"\\JassHook.dll";
    DWORD attr = GetFileAttributesW(dllPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        dllPath = L"JassHook.dll";
    }

    std::cout << "Searching for war3.exe...\n";
    DWORD pid = 0;
    while (!pid) {
        pid = FindProcessId(L"war3.exe");
        if (!pid) {
            pid = FindProcessId(L"War3.exe");
        }
        if (!pid) {
            std::cout << "war3.exe not found. Waiting 2 seconds...\n";
            Sleep(2000);
        }
    }

    std::cout << "Found war3.exe! Process ID: " << pid << "\n";
    if (InjectDll(pid, dllPath)) {
        std::cout << "[+] Injection completed successfully!\n";
    } else {
        std::cout << "[-] Injection failed!\n";
    }

    std::cout << "Press Enter to exit...\n";
    std::cin.get();
    return 0;
}
