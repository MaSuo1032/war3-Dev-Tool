#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

// [SUPPORTED MODDING] / [PROCESS MODIFICATION]

bool enableDebugPrivilege() {
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

DWORD FindProcessId() {
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (lstrcmpiW(pe.szExeFile, L"war3.exe") == 0 ||
                lstrcmpiW(pe.szExeFile, L"War3.exe") == 0 ||
                lstrcmpiW(pe.szExeFile, L"Warcraft III.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool InjectDLL(DWORD processID, const char* dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processID);
    if (!hProcess) {
        std::cerr << "[-] Failed to open process. Error: " << GetLastError() << std::endl;
        return false;
    }

    void* loc = VirtualAllocEx(hProcess, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        std::cerr << "[-] Failed to allocate memory in target process." << std::endl;
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, loc, dllPath, strlen(dllPath) + 1, 0)) {
        std::cerr << "[-] Failed to write memory." << std::endl;
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
    if (!hThread) {
        std::cerr << "[-] Failed to create remote thread." << std::endl;
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    std::cout << "[+] DLL injected successfully!" << std::endl;
    
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return true;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " War3JassDevTool - Watcher Injector" << std::endl;
    std::cout << "========================================" << std::endl;
    
    if (!enableDebugPrivilege()) {
        std::cout << "[WARN] SeDebugPrivilege unavailable. Run as Administrator if hook fails.\n";
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exeDir = exeDir.substr(0, lastSlash + 1);
    }
    
    std::string dllPathStr = exeDir + "JassHook.dll";
    const char* dllPath = dllPathStr.c_str();

    std::cout << "[*] Payload DLL: " << dllPath << std::endl;

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[-] Error: Payload DLL not found!" << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "[*] Waiting for game to start... (Please launch war3.exe via KK Platform)" << std::endl;

    DWORD pid = 0;
    while (true) {
        pid = FindProcessId();
        if (pid) {
            std::cout << "\n[+] Found process! PID: " << pid << std::endl;
            std::cout << "[*] Waiting 2 seconds for process initialization..." << std::endl;
            Sleep(2000); // Wait 2 seconds as requested by user
            if (InjectDLL(pid, dllPath)) {
                std::cout << "[+] Injection Complete. The background thread will intercept registrations." << std::endl;
            }
            break;
        }
        Sleep(5); // Loop very fast to catch early registration
    }

    std::cout << "[*] Done. You can close this window." << std::endl;
    Sleep(3000);
    return 0;
}
