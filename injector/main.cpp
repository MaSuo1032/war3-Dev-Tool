#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

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
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        std::wcout << L"[-] Failed to open process (PID: " << pid << L"). Error: " << GetLastError() << L"\n";
        return false;
    }

    size_t size = (dllPath.length() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        std::wcout << L"[-] VirtualAllocEx failed in target process.\n";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), size, NULL)) {
        std::wcout << L"[-] WriteProcessMemory failed.\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPTHREAD_START_ROUTINE pfnLoadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW"));

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, pfnLoadLibrary, remoteMem, 0, NULL);
    if (!hThread) {
        std::wcout << L"[-] CreateRemoteThread failed.\n";
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

    wchar_t currentDir[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, currentDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(currentDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';

    std::wstring dllPath = std::wstring(currentDir) + L"\\JassHook.dll";
    DWORD attr = GetFileAttributesW(dllPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        // Try looking in bin or current working directory
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
