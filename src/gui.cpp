#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Control Identifiers
#define IDC_GRP_INJECT            1000
#define IDC_LBL_WAR3_STATUS       1001
#define IDC_LBL_HOOK_STATUS       1002
#define IDC_LBL_PIPE_STATUS       1003
#define IDC_CHK_AUTO_INJECT       1004
#define IDC_BTN_INJECT            1005
#define IDC_BTN_LAUNCH_CLI        1006
#define IDC_BTN_RECONNECT         1007

#define IDC_GRP_MALL              1010
#define IDC_CMB_MALL_MODE         1011
#define IDC_SLIDER_PERCENT        1012
#define IDC_EDIT_PERCENT          1013
#define IDC_BTN_APPLY_MALL        1014
#define IDC_BTN_EDIT_BLACKLIST    1015
#define IDC_BTN_EDIT_WHITELIST    1016
#define IDC_BTN_RELOAD_LISTS      1017

#define IDC_GRP_SAVE              1020
#define IDC_BTN_PANEL             1021
#define IDC_BTN_SAVEALL           1022
#define IDC_BTN_OPEN_LOCALSAVE    1023
#define IDC_EDIT_PID              1024
#define IDC_EDIT_KEY              1025
#define IDC_EDIT_VAL              1026
#define IDC_BTN_SAVE_SINGLE       1027

#define IDC_GRP_CMD               1030
#define IDC_BTN_KILL              1031
#define IDC_EDIT_CMD              1032
#define IDC_BTN_SEND_CMD          1033

#define IDC_GRP_LOG               1040
#define IDC_EDIT_LOG              1041
#define IDC_CHK_AUTO_SCROLL       1042
#define IDC_BTN_CLEAR_LOG         1043
#define IDC_BTN_COPY_LOG          1044
#define IDC_BTN_OPEN_TRACE        1045

#define IDT_TIMER_STATUS          1

// Global GUI Handles
HWND g_hWnd = NULL;
HWND g_hLblWar3 = NULL;
HWND g_hLblHook = NULL;
HWND g_hLblPipe = NULL;
HWND g_hChkAutoInject = NULL;
HWND g_hCmbMallMode = NULL;
HWND g_hSliderPercent = NULL;
HWND g_hEditPercent = NULL;
HWND g_hEditPid = NULL;
HWND g_hEditKey = NULL;
HWND g_hEditVal = NULL;
HWND g_hEditCmd = NULL;
HWND g_hEditLog = NULL;
HWND g_hChkAutoScroll = NULL;

HFONT g_hFont = NULL;
HFONT g_hFontBold = NULL;
HFONT g_hFontConsole = NULL;
HBRUSH g_hLogBrush = NULL;

WNDPROC g_oldEditCmdProc = NULL;
WNDPROC g_oldEditPercentProc = NULL;

HANDLE g_hPipe = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_pipeCs;
HANDLE g_hPipeReaderThread = NULL;
bool g_runPipeReader = true;
std::string g_pipeLogBuffer = "";

DWORD g_lastDetectedPid = 0;
bool g_isDllInjected = false;
bool g_updatingSlider = false;

// Conversion helper
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

std::string GetExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s = path;
    size_t last = s.find_last_of("\\/");
    if (last != std::string::npos) return s.substr(0, last + 1);
    return ".\\";
}

// Append log with timestamp
void AppendLog(const std::wstring& msg) {
    if (!g_hEditLog) return;
    
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    wchar_t timeStr[32];
    wcsftime(timeStr, sizeof(timeStr) / sizeof(wchar_t), L"[%H:%M:%S] ", &buf);
    
    std::wstring line = timeStr + msg + L"\r\n";
    
    int len = GetWindowTextLengthW(g_hEditLog);
    if (len > 300000) {
        SendMessageW(g_hEditLog, EM_SETSEL, 0, 80000);
        SendMessageW(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(g_hEditLog);
    }
    SendMessageW(g_hEditLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_hEditLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    
    bool autoScroll = true;
    if (g_hChkAutoScroll) {
        autoScroll = (SendMessage(g_hChkAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    if (autoScroll) {
        SendMessageW(g_hEditLog, WM_VSCROLL, SB_BOTTOM, 0);
    }
}

// Privilege & Process Detection
bool EnableDebugPrivilege() {
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

bool IsDllInjected(DWORD pid, const std::wstring& dllName) {
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

bool InjectDLL(DWORD processID, const std::string& dllPath, std::wstring& outErr) {
    EnableDebugPrivilege();
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processID);
    if (!hProcess) {
        DWORD err = GetLastError();
        outErr = L"OpenProcess 失敗 (錯誤碼 " + std::to_wstring(err) + L")，請確認是否具備系統管理員身分。";
        return false;
    }

    void* loc = VirtualAllocEx(hProcess, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        DWORD err = GetLastError();
        outErr = L"VirtualAllocEx 記憶體配置失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, loc, dllPath.c_str(), dllPath.length() + 1, 0)) {
        DWORD err = GetLastError();
        outErr = L"WriteProcessMemory 失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
    if (!hThread) {
        DWORD err = GetLastError();
        outErr = L"CreateRemoteThread 遠端線程創建失敗 (錯誤碼 " + std::to_wstring(err) + L")";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return true;
}

// Named Pipe Communication
bool ConnectPipe() {
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipeCs);
        return true;
    }
    g_hPipe = CreateFileA(
        "\\\\.\\pipe\\War3JassDevTool",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    bool ok = (g_hPipe != INVALID_HANDLE_VALUE);
    LeaveCriticalSection(&g_pipeCs);
    return ok;
}

void DisconnectPipe() {
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_pipeCs);
}

bool SendPipeCommand(const std::string& cmd) {
    if (!ConnectPipe()) return false;
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipeCs);
        return false;
    }
    std::string payload = cmd;
    if (payload.empty() || payload.back() != '\n') {
        payload += "\n";
    }
    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(g_hPipe, payload.c_str(), (DWORD)payload.length(), &bytesWritten, NULL);
    if (!ok) {
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_pipeCs);
    return ok != FALSE;
}

DWORD WINAPI PipeLogReaderThread(LPVOID lpParam) {
    while (g_runPipeReader) {
        bool hasData = false;
        DWORD bytesAvail = 0;

        EnterCriticalSection(&g_pipeCs);
        if (g_hPipe != INVALID_HANDLE_VALUE) {
            if (PeekNamedPipe(g_hPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
                hasData = true;
            }
        }
        LeaveCriticalSection(&g_pipeCs);

        if (hasData) {
            char chunk[4096];
            DWORD toRead = (bytesAvail < sizeof(chunk) - 1) ? bytesAvail : (sizeof(chunk) - 1);
            DWORD bytesRead = 0;

            EnterCriticalSection(&g_pipeCs);
            BOOL ok = (g_hPipe != INVALID_HANDLE_VALUE) && ReadFile(g_hPipe, chunk, toRead, &bytesRead, NULL);
            LeaveCriticalSection(&g_pipeCs);

            if (ok && bytesRead > 0) {
                chunk[bytesRead] = '\0';
                g_pipeLogBuffer.append(chunk, bytesRead);
                size_t nl;
                while ((nl = g_pipeLogBuffer.find('\n')) != std::string::npos) {
                    std::string line = g_pipeLogBuffer.substr(0, nl);
                    g_pipeLogBuffer.erase(0, nl + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        AppendLog(Utf8ToWide(line));
                    }
                }
            }
        } else {
            Sleep(40);
        }
    }
    return 0;
}

// Perform DLL Injection
void DoInject() {
    DWORD pid = FindProcessId();
    if (pid == 0) {
        AppendLog(L"[-] 注入失敗: 未偵測到 war3.exe，請先開啟遊戲並進入地圖！");
        MessageBoxW(g_hWnd, L"未偵測到 war3.exe！\n請先透過 KK 平台啟動遊戲並進入地圖。", L"提示", MB_ICONWARNING);
        return;
    }

    if (IsDllInjected(pid, L"JassHook.dll")) {
        AppendLog(L"[*] JassHook.dll 已存在於目標進程中，無需重複注入。");
        return;
    }

    std::string exeDir = GetExeDir();
    std::string dllPath = exeDir + "JassHook.dll";
    if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        dllPath = exeDir + "bin\\JassHook.dll";
        if (GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            AppendLog(L"[-] 注入失敗: 找不到 JassHook.dll 檔案！");
            MessageBoxW(g_hWnd, L"找不到 JassHook.dll 檔案！請確認放在同一目錄或 bin 目錄下。", L"錯誤", MB_ICONERROR);
            return;
        }
    }

    AppendLog(L"[*] 正在注入 JassHook.dll 至進程 PID: " + std::to_wstring(pid) + L" ...");
    std::wstring err;
    if (InjectDLL(pid, dllPath, err)) {
        AppendLog(L"[+] 恭喜！JassHook.dll 注入成功！");
        g_isDllInjected = true;
        Sleep(250);
        if (ConnectPipe()) {
            AppendLog(L"[IPC] 已成功連線至魔獸爭霸 IPC 管道！");
        }
    } else {
        AppendLog(L"[-] 注入失敗: " + err);
        MessageBoxW(g_hWnd, (L"注入失敗: " + err + L"\n請確認是否以「系統管理員身分」執行本程式！").c_str(), L"錯誤", MB_ICONERROR);
    }
}

// Generate Save Panel from local_save.txt & open in Notepad
void GenerateAndOpenPanel() {
    std::string exeDir = GetExeDir();
    std::string saveFile = exeDir + "local_save.txt";
    std::ifstream infile(saveFile);
    if (!infile.is_open()) {
        infile.open("local_save.txt");
        if (!infile.is_open()) {
            AppendLog(L"[-] 找不到 local_save.txt (請先在遊戲中載入存檔)");
            MessageBoxW(g_hWnd, L"找不到 local_save.txt\n請先在遊戲內觸發讀取存檔！", L"提示", MB_ICONWARNING);
            return;
        }
    }

    std::map<std::pair<std::string, std::string>, std::string> uniqueKeys;
    std::string line;
    std::string detectedPid = "";
    while (std::getline(infile, line)) {
        if (line.find("[GetServerValue]") != std::string::npos) {
            size_t pPos = line.find("Player: ");
            size_t kPos = line.find(" | Key: \"");
            size_t vPos = line.find("\" | Value(From Server): \"");
            if (pPos != std::string::npos && kPos != std::string::npos && vPos != std::string::npos) {
                std::string player = line.substr(pPos + 8, kPos - (pPos + 8));
                size_t kEnd = line.find("\"", kPos + 9);
                std::string key = line.substr(kPos + 9, kEnd - (kPos + 9));
                size_t vStart = vPos + 25;
                size_t vEnd = line.find("\"", vStart);
                std::string val = line.substr(vStart, vEnd - vStart);
                uniqueKeys[{player, key}] = val;
                if (detectedPid.empty()) detectedPid = player;
            }
        }
        else if (line.find("LOADED:") != std::string::npos) {
            size_t pPos = line.find("Player ");
            size_t lPos = line.find(" LOADED: ");
            if (pPos != std::string::npos && lPos != std::string::npos) {
                std::string player = line.substr(pPos + 7, lPos - (pPos + 7));
                size_t eqPos = line.find(" = ", lPos);
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(lPos + 9, eqPos - (lPos + 9));
                    std::string val = line.substr(eqPos + 3);
                    if (val == "(null/empty)") val = "1";
                    uniqueKeys[{player, key}] = val;
                    if (detectedPid.empty()) detectedPid = player;
                }
            }
        }
        else if (line.find("SAVED:") != std::string::npos) {
            size_t pPos = line.find("Player ");
            size_t lPos = line.find(" SAVED: ");
            if (pPos != std::string::npos && lPos != std::string::npos) {
                std::string player = line.substr(pPos + 7, lPos - (pPos + 7));
                size_t eqPos = line.find(" = ", lPos);
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(lPos + 8, eqPos - (lPos + 8));
                    std::string val = line.substr(eqPos + 3);
                    uniqueKeys[{player, key}] = val;
                    if (detectedPid.empty()) detectedPid = player;
                }
            }
        }
    }
    infile.close();

    if (uniqueKeys.empty()) {
        AppendLog(L"[-] local_save.txt 尚未記錄任何存檔變數。");
        MessageBoxW(g_hWnd, L"local_save.txt 尚未有數據，請先在遊戲中載入存檔。", L"提示", MB_ICONINFORMATION);
        return;
    }

    std::string panelPath = exeDir + "save_panel.txt";
    std::ofstream outfile(panelPath);
    outfile << "# ========================================================\n";
    outfile << "# [War3 RPG 存檔修改面板 - Save Panel]\n";
    outfile << "# 格式: 玩家ID:存檔變數Key=目標數值 (例: 1048602:ItemName=1)\n";
    outfile << "# 修改完畢後儲存 (Ctrl+S)，然後回到本面板點擊「批量覆寫所有存檔」\n";
    outfile << "# ========================================================\n\n";
    for (const auto& item : uniqueKeys) {
        outfile << item.first.first << ":" << item.first.second << "=" << item.second << "\n";
    }
    outfile.close();

    if (!detectedPid.empty() && g_hEditPid) {
        SetWindowTextA(g_hEditPid, detectedPid.c_str());
    }

    AppendLog(L"[Panel] 已成功產生 save_panel.txt (共 " + std::to_wstring(uniqueKeys.size()) + L" 筆數據)，正在開啟記事本...");
    ShellExecuteA(g_hWnd, "open", "notepad.exe", panelPath.c_str(), NULL, SW_SHOWNORMAL);
}

// Batch Save All from save_panel.txt
void SaveAllFromPanel() {
    std::string exeDir = GetExeDir();
    std::string panelPath = exeDir + "save_panel.txt";
    std::ifstream pfile(panelPath);
    if (!pfile.is_open()) {
        pfile.open("save_panel.txt");
        if (!pfile.is_open()) {
            AppendLog(L"[-] 找不到 save_panel.txt，請先點擊「產生並開啟存檔面板」！");
            MessageBoxW(g_hWnd, L"找不到 save_panel.txt\n請先產生面板！", L"提示", MB_ICONWARNING);
            return;
        }
    }

    if (!ConnectPipe()) {
        AppendLog(L"[-] 無法連線至魔獸爭霸 IPC 管道，請先注入 JassHook.dll！");
        MessageBoxW(g_hWnd, L"未連線至遊戲！請先點選「一鍵注入」。", L"錯誤", MB_ICONERROR);
        return;
    }

    std::string line;
    int count = 0;
    while (std::getline(pfile, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t colon = line.find(':');
        size_t eq = line.find('=');
        if (colon != std::string::npos && eq != std::string::npos && eq > colon) {
            std::string p = line.substr(0, colon);
            std::string k = line.substr(colon + 1, eq - colon - 1);
            std::string v = line.substr(eq + 1);
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            
            std::string payload = "SAVE:" + p + ":" + k + ":" + v;
            if (SendPipeCommand(payload)) {
                count++;
                Sleep(20);
            }
        }
    }
    pfile.close();
    AppendLog(L"[+] 批量回寫完成！共發送 " + std::to_wstring(count) + L" 筆存檔指令至遊戲。");
    MessageBoxW(g_hWnd, (L"批量回寫完成！共送出 " + std::to_wstring(count) + L" 筆指令。").c_str(), L"成功", MB_ICONINFORMATION);
}

// Send Single Save Command
void SendSingleSave() {
    char pBuf[64], kBuf[256], vBuf[512];
    GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
    GetWindowTextA(g_hEditKey, kBuf, sizeof(kBuf));
    GetWindowTextA(g_hEditVal, vBuf, sizeof(vBuf));

    if (strlen(pBuf) == 0 || strlen(kBuf) == 0 || strlen(vBuf) == 0) {
        AppendLog(L"[-] 手動存檔失敗: PlayerID, Key 與 Value 均不可為空！");
        MessageBoxW(g_hWnd, L"請完整填寫 PlayerID、Key 與 Value！", L"提示", MB_ICONWARNING);
        return;
    }

    std::string payload = std::string("SAVE:") + pBuf + ":" + kBuf + ":" + vBuf;
    if (SendPipeCommand(payload)) {
        AppendLog(L"[+] 已發送單筆存檔: " + Utf8ToWide(payload));
    } else {
        AppendLog(L"[-] 發送存檔指令失敗，請確認魔獸連線狀態。");
        MessageBoxW(g_hWnd, L"發送失敗，請確認是否已注入遊戲！", L"錯誤", MB_ICONERROR);
    }
}

// Apply Mall Settings & Sync to Game
void ApplyMallSettings() {
    int curSel = (int)SendMessage(g_hCmbMallMode, CB_GETCURSEL, 0, 0);
    if (curSel == CB_ERR) curSel = 2;

    wchar_t pctBuf[16];
    GetWindowTextW(g_hEditPercent, pctBuf, 16);
    int pct = _wtoi(pctBuf);
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;

    std::string exeDir = GetExeDir();
    std::wstring iniPath = Utf8ToWide(exeDir) + L"JassHookConfig.ini";

    wchar_t valBuf[32];
    swprintf_s(valBuf, 32, L"%d", curSel);
    WritePrivateProfileStringW(L"Spoof", L"MallMode", valBuf, iniPath.c_str());

    swprintf_s(valBuf, 32, L"%d", pct);
    WritePrivateProfileStringW(L"Spoof", L"MallPercent", valBuf, iniPath.c_str());

    // Send live synchronization via Named Pipe
    if (ConnectPipe()) {
        SendPipeCommand("MALL:" + std::to_string(curSel));
        SendPipeCommand("PERCENT:" + std::to_string(pct));
        SendPipeCommand("RELOAD_LISTS");
        AppendLog(L"[設定] 已儲存設定並同步至遊戲！(商城模式: " + std::to_wstring(curSel) + L", 比例: " + std::to_wstring(pct) + L"%)");
    } else {
        AppendLog(L"[設定] 已儲存設定至 JassHookConfig.ini (魔獸未連線，將於下次啟動/注入時生效)");
    }
}

// Open or create file in Notepad
void OpenTextFile(const std::string& filename, const std::string& defaultContent) {
    std::string exeDir = GetExeDir();
    std::string fullPath = exeDir + filename;
    
    if (GetFileAttributesA(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream outfile(fullPath);
        outfile << defaultContent;
        outfile.close();
    }
    ShellExecuteA(g_hWnd, "open", "notepad.exe", fullPath.c_str(), NULL, SW_SHOWNORMAL);
}

// Process Command Input
void ProcessUserCommand(const std::string& cmd) {
    if (cmd.empty()) return;
    
    std::string c = cmd;
    while (!c.empty() && (c.back() == '\r' || c.back() == '\n' || c.back() == ' ')) c.pop_back();
    while (!c.empty() && c.front() == ' ') c.erase(c.begin());
    if (c.empty()) return;

    AppendLog(L"> " + Utf8ToWide(c));

    if (c == "!kill" || c == "!killselected") {
        if (SendPipeCommand("KILL")) {
            AppendLog(L"[+] 已發送秒殺指令 (KILL)");
        }
    }
    else if (c == "!panel") {
        GenerateAndOpenPanel();
    }
    else if (c == "!saveall") {
        SaveAllFromPanel();
    }
    else if (c.rfind("!save ", 0) == 0) {
        std::stringstream ss(c.substr(6));
        std::string p, k, v;
        if (ss >> p >> k >> v) {
            std::string payload = "SAVE:" + p + ":" + k + ":" + v;
            SendPipeCommand(payload);
            AppendLog(L"[+] 已發送存檔指令: " + Utf8ToWide(payload));
        } else {
            AppendLog(L"[-] 格式錯誤，正確格式: !save <PlayerID> <Key> <Value>");
        }
    }
    else if (c.rfind("!mall ", 0) == 0) {
        int m = atoi(c.substr(6).c_str());
        SendMessage(g_hCmbMallMode, CB_SETCURSEL, (WPARAM)m, 0);
        ApplyMallSettings();
    }
    else if (c.rfind("!percent ", 0) == 0) {
        int p = atoi(c.substr(9).c_str());
        SetWindowTextW(g_hEditPercent, std::to_wstring(p).c_str());
        SendMessage(g_hSliderPercent, TBM_SETPOS, TRUE, (LPARAM)p);
        ApplyMallSettings();
    }
    else if (c == "!reload") {
        SendPipeCommand("RELOAD_LISTS");
        AppendLog(L"[+] 已發送重新載入名單指令 (RELOAD_LISTS)");
    }
    else if (c[0] == '!') {
        std::string sub = c.substr(1);
        std::string formatted = "NATIVE:";
        std::stringstream ss(sub);
        std::string token;
        bool first = true;
        while (ss >> token) {
            if (!first) formatted += ":";
            formatted += token;
            first = false;
        }
        SendPipeCommand(formatted);
        AppendLog(L"[+] 已呼叫 Native: " + Utf8ToWide(formatted));
    }
    else {
        // Send directly
        SendPipeCommand(c);
        AppendLog(L"[+] 已發送原始指令: " + Utf8ToWide(c));
    }
}

// Subclass EditBox for Enter key support
LRESULT CALLBACK EditCmdSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CHAR && wParam == VK_RETURN) {
        wchar_t buf[512];
        GetWindowTextW(hWnd, buf, 512);
        std::string cmd = WideToUtf8(buf);
        SetWindowTextW(hWnd, L"");
        ProcessUserCommand(cmd);
        return 0;
    }
    return CallWindowProc(g_oldEditCmdProc, hWnd, uMsg, wParam, lParam);
}

// Subclass Percent EditBox to sync slider
LRESULT CALLBACK EditPercentSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT res = CallWindowProc(g_oldEditPercentProc, hWnd, uMsg, wParam, lParam);
    if (uMsg == WM_CHAR || uMsg == WM_KEYUP) {
        if (!g_updatingSlider && g_hSliderPercent) {
            wchar_t buf[16];
            GetWindowTextW(hWnd, buf, 16);
            int val = _wtoi(buf);
            if (val >= 1 && val <= 100) {
                g_updatingSlider = true;
                SendMessage(g_hSliderPercent, TBM_SETPOS, TRUE, (LPARAM)val);
                g_updatingSlider = false;
            }
        }
    }
    return res;
}

// Periodic Status Check (War3 process, DLL injection, Pipe)
void UpdateStatus() {
    DWORD pid = FindProcessId();
    if (pid != 0) {
        SetWindowTextW(g_hLblWar3, (L"魔獸進程: ● 運行中 (PID: " + std::to_wstring(pid) + L")").c_str());
        
        bool injected = IsDllInjected(pid, L"JassHook.dll");
        if (injected) {
            SetWindowTextW(g_hLblHook, L"核心 DLL: ● 已注入 (JassHook.dll)");
            g_isDllInjected = true;
        } else {
            SetWindowTextW(g_hLblHook, L"核心 DLL: ○ 未注入");
            g_isDllInjected = false;
            
            // Check Auto-Inject
            if (SendMessage(g_hChkAutoInject, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                if (pid != g_lastDetectedPid) {
                    g_lastDetectedPid = pid;
                    AppendLog(L"[自動注入] 偵測到魔獸爭霸啟動，正在注入 JassHook.dll ...");
                    DoInject();
                }
            }
        }
    } else {
        SetWindowTextW(g_hLblWar3, L"魔獸進程: ○ 未偵測到 (請開啟遊戲)");
        SetWindowTextW(g_hLblHook, L"核心 DLL: ○ 未注入");
        g_isDllInjected = false;
        g_lastDetectedPid = 0;
    }

    bool pipeOk = ConnectPipe();
    if (pipeOk) {
        SetWindowTextW(g_hLblPipe, L"IPC 管道: ● 已連線 (雙向串流中)");
    } else {
        SetWindowTextW(g_hLblPipe, L"IPC 管道: ○ 未連線");
    }
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(icex);
        icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
        InitCommonControlsEx(&icex);

        g_hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
        g_hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei");
        g_hFontConsole = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hLogBrush = CreateSolidBrush(RGB(28, 28, 28));

        // 1. Group 1: 系統狀態與注入工具
        HWND hGrp1 = CreateWindowW(L"BUTTON", L"【系統狀態與注入工具】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 10, 850, 95, hWnd, (HMENU)IDC_GRP_INJECT, NULL, NULL);
        SendMessage(hGrp1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hLblWar3 = CreateWindowW(L"STATIC", L"魔獸進程: ○ 檢測中...", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 32, 230, 20, hWnd, (HMENU)IDC_LBL_WAR3_STATUS, NULL, NULL);
        SendMessage(g_hLblWar3, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hLblHook = CreateWindowW(L"STATIC", L"核心 DLL: ○ 未注入", WS_CHILD | WS_VISIBLE | SS_LEFT,
            270, 32, 230, 20, hWnd, (HMENU)IDC_LBL_HOOK_STATUS, NULL, NULL);
        SendMessage(g_hLblHook, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hLblPipe = CreateWindowW(L"STATIC", L"IPC 管道: ○ 未連線", WS_CHILD | WS_VISIBLE | SS_LEFT,
            510, 32, 220, 20, hWnd, (HMENU)IDC_LBL_PIPE_STATUS, NULL, NULL);
        SendMessage(g_hLblPipe, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hChkAutoInject = CreateWindowW(L"BUTTON", L"遊戲啟動時自動注入", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            30, 64, 180, 24, hWnd, (HMENU)IDC_CHK_AUTO_INJECT, NULL, NULL);
        SendMessage(g_hChkAutoInject, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

        HWND hBtnInject = CreateWindowW(L"BUTTON", L"🚀 一鍵注入 JassHook", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            220, 60, 175, 32, hWnd, (HMENU)IDC_BTN_INJECT, NULL, NULL);
        SendMessage(hBtnInject, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnLaunchCli = CreateWindowW(L"BUTTON", L"💻 開啟 JassTool 終端 (CLI)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            405, 60, 230, 32, hWnd, (HMENU)IDC_BTN_LAUNCH_CLI, NULL, NULL);
        SendMessage(hBtnLaunchCli, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnReconnect = CreateWindowW(L"BUTTON", L"🔄 重新連線 IPC", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            645, 60, 200, 32, hWnd, (HMENU)IDC_BTN_RECONNECT, NULL, NULL);
        SendMessage(hBtnReconnect, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 2. Group 2: 商城模式、比例與黑名單
        HWND hGrp2 = CreateWindowW(L"BUTTON", L"【商城偽造模式、隨機比例與黑名單設定】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 115, 850, 105, hWnd, (HMENU)IDC_GRP_MALL, NULL, NULL);
        SendMessage(hGrp2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hLblMode = CreateWindowW(L"STATIC", L"商城偽造模式:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 140, 95, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblMode, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hCmbMallMode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            130, 136, 205, 160, hWnd, (HMENU)IDC_CMB_MALL_MODE, NULL, NULL);
        SendMessage(g_hCmbMallMode, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"0 - 關閉 (原廠數據)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"1 - 白名單 (mall_spoof)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"2 - 全開模式 (非黑名單道具)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"3 - 比例隨機模式 (自訂機率)");

        HWND hLblPct = CreateWindowW(L"STATIC", L"隨機比例:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            345, 140, 65, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblPct, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hSliderPercent = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            415, 136, 175, 28, hWnd, (HMENU)IDC_SLIDER_PERCENT, NULL, NULL);
        SendMessage(g_hSliderPercent, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendMessage(g_hSliderPercent, TBM_SETPAGESIZE, 0, 10);
        SendMessage(g_hSliderPercent, TBM_SETTICFREQ, 10, 0);

        g_hEditPercent = CreateWindowW(L"EDIT", L"80", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER | ES_NUMBER,
            595, 137, 45, 24, hWnd, (HMENU)IDC_EDIT_PERCENT, NULL, NULL);
        SendMessage(g_hEditPercent, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        g_oldEditPercentProc = (WNDPROC)SetWindowLongPtr(g_hEditPercent, GWLP_WNDPROC, (LONG_PTR)EditPercentSubclassProc);

        HWND hLblPctSign = CreateWindowW(L"STATIC", L"%", WS_CHILD | WS_VISIBLE | SS_LEFT,
            645, 140, 15, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblPctSign, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnApplyMall = CreateWindowW(L"BUTTON", L"💾 套用並同步設定", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            670, 134, 180, 30, hWnd, (HMENU)IDC_BTN_APPLY_MALL, NULL, NULL);
        SendMessage(hBtnApplyMall, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Mall List Buttons
        HWND hLblList = CreateWindowW(L"STATIC", L"名單管理:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 178, 70, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnBlacklist = CreateWindowW(L"BUTTON", L"🛡️ 編輯黑名單 (mall_blacklist.txt)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            110, 174, 260, 28, hWnd, (HMENU)IDC_BTN_EDIT_BLACKLIST, NULL, NULL);
        SendMessage(hBtnBlacklist, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnWhitelist = CreateWindowW(L"BUTTON", L"📜 編輯白名單 (mall_spoof.txt)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            380, 174, 240, 28, hWnd, (HMENU)IDC_BTN_EDIT_WHITELIST, NULL, NULL);
        SendMessage(hBtnWhitelist, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnReload = CreateWindowW(L"BUTTON", L"🔄 重新載入名單", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            630, 174, 220, 28, hWnd, (HMENU)IDC_BTN_RELOAD_LISTS, NULL, NULL);
        SendMessage(hBtnReload, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Load Initial Config from INI
        std::string exeDir = GetExeDir();
        std::wstring iniPath = Utf8ToWide(exeDir) + L"JassHookConfig.ini";
        int iniMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 2, iniPath.c_str());
        int iniPct = GetPrivateProfileIntW(L"Spoof", L"MallPercent", 80, iniPath.c_str());
        SendMessage(g_hCmbMallMode, CB_SETCURSEL, (WPARAM)iniMode, 0);
        SendMessage(g_hSliderPercent, TBM_SETPOS, TRUE, (LPARAM)iniPct);
        SetWindowTextW(g_hEditPercent, std::to_wstring(iniPct).c_str());

        // 3. Group 3: 伺服器存檔管理 (Save Panel)
        HWND hGrp3 = CreateWindowW(L"BUTTON", L"【伺服器存檔管理 (Save Panel - 原版核心整合)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 230, 850, 105, hWnd, (HMENU)IDC_GRP_SAVE, NULL, NULL);
        SendMessage(hGrp3, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPanel = CreateWindowW(L"BUTTON", L"📝 產生並開啟存檔面板 (!panel)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 250, 255, 32, hWnd, (HMENU)IDC_BTN_PANEL, NULL, NULL);
        SendMessage(hBtnPanel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnSaveAll = CreateWindowW(L"BUTTON", L"⚡ 批量覆寫所有存檔 (!saveall)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            295, 250, 255, 32, hWnd, (HMENU)IDC_BTN_SAVEALL, NULL, NULL);
        SendMessage(hBtnSaveAll, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnOpenLocal = CreateWindowW(L"BUTTON", L"📂 開啟存檔日誌 (local_save.txt)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            560, 250, 290, 32, hWnd, (HMENU)IDC_BTN_OPEN_LOCALSAVE, NULL, NULL);
        SendMessage(hBtnOpenLocal, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblQuick = CreateWindowW(L"STATIC", L"單筆寫入 (!save):", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 295, 125, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblQuick, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblPid = CreateWindowW(L"STATIC", L"Player:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            160, 295, 45, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblPid, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditPid = CreateWindowW(L"EDIT", L"1048602", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            210, 293, 85, 24, hWnd, (HMENU)IDC_EDIT_PID, NULL, NULL);
        SendMessage(g_hEditPid, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblKey = CreateWindowW(L"STATIC", L"Key:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            305, 295, 30, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblKey, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditKey = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            340, 293, 160, 24, hWnd, (HMENU)IDC_EDIT_KEY, NULL, NULL);
        SendMessage(g_hEditKey, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblVal = CreateWindowW(L"STATIC", L"Value:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            510, 295, 45, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblVal, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditVal = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            560, 293, 140, 24, hWnd, (HMENU)IDC_EDIT_VAL, NULL, NULL);
        SendMessage(g_hEditVal, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnSaveSingle = CreateWindowW(L"BUTTON", L"寫入存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            710, 291, 140, 28, hWnd, (HMENU)IDC_BTN_SAVE_SINGLE, NULL, NULL);
        SendMessage(hBtnSaveSingle, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 4. Group 4: 原版 JassTool 快捷與終端指令
        HWND hGrp4 = CreateWindowW(L"BUTTON", L"【原版 JassTool 快捷與指令互動終端】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 345, 850, 65, hWnd, (HMENU)IDC_GRP_CMD, NULL, NULL);
        SendMessage(hGrp4, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnKill = CreateWindowW(L"BUTTON", L"💀 秒殺選中單位 (!kill)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 365, 175, 32, hWnd, (HMENU)IDC_BTN_KILL, NULL, NULL);
        SendMessage(hBtnKill, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hLblCmd = CreateWindowW(L"STATIC", L"Jass 指令:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            215, 371, 70, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditCmd = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            290, 368, 430, 26, hWnd, (HMENU)IDC_EDIT_CMD, NULL, NULL);
        SendMessage(g_hEditCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);

        HWND hBtnSend = CreateWindowW(L"BUTTON", L"發送指令", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            730, 366, 120, 30, hWnd, (HMENU)IDC_BTN_SEND_CMD, NULL, NULL);
        SendMessage(hBtnSend, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 5. Group 5: 即時串流監控 (Live Monitor)
        HWND hGrp5 = CreateWindowW(L"BUTTON", L"【即時串流監控 (Live Monitor)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 420, 850, 270, hWnd, (HMENU)IDC_GRP_LOG, NULL, NULL);
        SendMessage(hGrp5, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hLblLog = CreateWindowW(L"STATIC", L"即時 JASS / DzAPI 串流日誌:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 440, 200, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hChkAutoScroll = CreateWindowW(L"BUTTON", L"自動滾動", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            420, 440, 100, 20, hWnd, (HMENU)IDC_CHK_AUTO_SCROLL, NULL, NULL);
        SendMessage(g_hChkAutoScroll, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

        HWND hBtnClear = CreateWindowW(L"BUTTON", L"清空日誌", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 437, 95, 24, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, NULL, NULL);
        SendMessage(hBtnClear, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnCopy = CreateWindowW(L"BUTTON", L"複製日誌", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            635, 437, 95, 24, hWnd, (HMENU)IDC_BTN_COPY_LOG, NULL, NULL);
        SendMessage(hBtnCopy, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnTrace = CreateWindowW(L"BUTTON", L"開啟 dztrace.log", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            740, 437, 110, 24, hWnd, (HMENU)IDC_BTN_OPEN_TRACE, NULL, NULL);
        SendMessage(hBtnTrace, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditLog = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            30, 465, 820, 215, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessage(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);

        // Status Check Timer (Every 1000ms)
        SetTimer(hWnd, IDT_TIMER_STATUS, 1000, NULL);
        UpdateStatus();

        AppendLog(L"=== 魔獸爭霸 III JASS 開發者整合控制面板 v2.1 初始化完成 ===");
        AppendLog(L"[*] 支援一鍵注入、原版 JassTool 終端、存檔面板 (!panel, !saveall) 與商城比例黑名單");
        break;
    }

    case WM_TIMER: {
        if (wParam == IDT_TIMER_STATUS) {
            UpdateStatus();
        }
        break;
    }

    case WM_HSCROLL: {
        if ((HWND)lParam == g_hSliderPercent) {
            int pos = (int)SendMessage(g_hSliderPercent, TBM_GETPOS, 0, 0);
            if (!g_updatingSlider) {
                g_updatingSlider = true;
                SetWindowTextW(g_hEditPercent, std::to_wstring(pos).c_str());
                g_updatingSlider = false;
            }
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        if (hwndStatic == g_hEditLog) {
            SetTextColor(hdcStatic, RGB(0, 255, 128));
            SetBkColor(hdcStatic, RGB(28, 28, 28));
            return (LRESULT)g_hLogBrush;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case IDC_BTN_INJECT:
            DoInject();
            break;

        case IDC_BTN_LAUNCH_CLI: {
            std::string exeDir = GetExeDir();
            std::string cliPath = exeDir + "War3JassDevTool.exe";
            if (GetFileAttributesA(cliPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                AppendLog(L"[-] 找不到 War3JassDevTool.exe 控制台執行檔！");
                MessageBoxW(hWnd, L"找不到 War3JassDevTool.exe！", L"錯誤", MB_ICONERROR);
            } else {
                AppendLog(L"[*] 正在啟動 War3JassDevTool 獨立控制台...");
                ShellExecuteA(hWnd, "open", cliPath.c_str(), NULL, exeDir.c_str(), SW_SHOWNORMAL);
            }
            break;
        }

        case IDC_BTN_RECONNECT:
            DisconnectPipe();
            if (ConnectPipe()) {
                AppendLog(L"[IPC] 重新連線成功！");
            } else {
                AppendLog(L"[-] 重新連線失敗，請確認 JassHook.dll 是否已注入魔獸。");
            }
            UpdateStatus();
            break;

        case IDC_BTN_APPLY_MALL:
            ApplyMallSettings();
            break;

        case IDC_BTN_EDIT_BLACKLIST: {
            std::string defaultContent = 
                "# ==========================================\n"
                "# 商城蜜罐 (Honeypot) 與陷阱道具黑名單\n"
                "# 符合下列名稱的道具一律強制返回 0 (未擁有)，防止觸發反作弊或封鎖\n"
                "# ==========================================\n"
                "cheat\ntest\ntrap\nBAN_CHECK\nTEST_MALL_ITEM\nHONEYPOT_DETECT\n"
                "DEBUG_MALL_TOKEN\nASDZDF\necn3gHnB95\nJ\nC\nD\nE\nF\nM\nP\nI\n";
            OpenTextFile("mall_blacklist.txt", defaultContent);
            break;
        }

        case IDC_BTN_EDIT_WHITELIST: {
            std::string defaultContent = 
                "# ==========================================\n"
                "# 商城白名單 (Mall Whitelist)\n"
                "# 每行一個商城道具代碼，在白名單模式下僅解鎖此清單內的項目\n"
                "# ==========================================\n";
            OpenTextFile("mall_spoof.txt", defaultContent);
            break;
        }

        case IDC_BTN_RELOAD_LISTS:
            if (ConnectPipe()) {
                SendPipeCommand("RELOAD_LISTS");
                AppendLog(L"[+] 已發送重新加載名單指令 (RELOAD_LISTS)");
            } else {
                AppendLog(L"[-] 尚未連線至遊戲，設定將於注入時生效。");
            }
            break;

        case IDC_BTN_PANEL:
            GenerateAndOpenPanel();
            break;

        case IDC_BTN_SAVEALL:
            SaveAllFromPanel();
            break;

        case IDC_BTN_OPEN_LOCALSAVE: {
            std::string exeDir = GetExeDir();
            std::string saveFile = exeDir + "local_save.txt";
            if (GetFileAttributesA(saveFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
                AppendLog(L"[-] 找不到 local_save.txt！");
            } else {
                ShellExecuteA(hWnd, "open", "notepad.exe", saveFile.c_str(), NULL, SW_SHOWNORMAL);
            }
            break;
        }

        case IDC_BTN_SAVE_SINGLE:
            SendSingleSave();
            break;

        case IDC_BTN_KILL:
            if (SendPipeCommand("KILL")) {
                AppendLog(L"[+] 已發送秒殺選中單位指令 (KILL)");
            } else {
                AppendLog(L"[-] 發送失敗，未連線至魔獸。");
            }
            break;

        case IDC_BTN_SEND_CMD: {
            wchar_t buf[512];
            GetWindowTextW(g_hEditCmd, buf, 512);
            std::string cmd = WideToUtf8(buf);
            SetWindowTextW(g_hEditCmd, L"");
            ProcessUserCommand(cmd);
            break;
        }

        case IDC_BTN_CLEAR_LOG:
            SetWindowTextW(g_hEditLog, L"");
            break;

        case IDC_BTN_COPY_LOG: {
            int len = GetWindowTextLengthW(g_hEditLog);
            if (len > 0) {
                std::vector<wchar_t> buf(len + 1);
                GetWindowTextW(g_hEditLog, buf.data(), len + 1);
                if (OpenClipboard(hWnd)) {
                    EmptyClipboard();
                    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
                    if (hGlob) {
                        memcpy(GlobalLock(hGlob), buf.data(), (len + 1) * sizeof(wchar_t));
                        GlobalUnlock(hGlob);
                        SetClipboardData(CF_UNICODETEXT, hGlob);
                    }
                    CloseClipboard();
                    AppendLog(L"[+] 日誌內容已複製到剪貼簿。");
                }
            }
            break;
        }

        case IDC_BTN_OPEN_TRACE: {
            std::string exeDir = GetExeDir();
            std::string tracePath = exeDir + "dztrace.log";
            if (GetFileAttributesA(tracePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                AppendLog(L"[-] 找不到 dztrace.log！");
            } else {
                ShellExecuteA(hWnd, "open", "notepad.exe", tracePath.c_str(), NULL, SW_SHOWNORMAL);
            }
            break;
        }
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, IDT_TIMER_STATUS);
        g_runPipeReader = false;
        DisconnectPipe();
        if (g_hLogBrush) DeleteObject(g_hLogBrush);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontConsole) DeleteObject(g_hFontConsole);
        DeleteCriticalSection(&g_pipeCs);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitializeCriticalSection(&g_pipeCs);
    
    // Create Pipe Log Reader Thread
    g_hPipeReaderThread = CreateThread(NULL, 0, PipeLogReaderThread, NULL, 0, NULL);

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = L"War3DevToolUIClass";
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        return 1;
    }

    RECT rc = { 0, 0, 880, 700 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    g_hWnd = CreateWindowW(
        L"War3DevToolUIClass",
        L"魔獸爭霸 III JASS 開發者整合控制面板 (War3DevTool v2.1)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd) {
        return 2;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
