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
#define IDC_GRP_INJECT        1000
#define IDC_LBL_WAR3_STATUS   1001
#define IDC_LBL_PIPE_STATUS   1002
#define IDC_CHK_AUTO_INJECT   1003
#define IDC_BTN_INJECT        1004
#define IDC_BTN_RECONNECT     1005

#define IDC_GRP_SAVE          1010
#define IDC_BTN_PANEL         1011
#define IDC_BTN_SAVEALL       1012
#define IDC_LBL_MANUAL_SAVE   1013
#define IDC_EDIT_PID          1014
#define IDC_EDIT_KEY          1015
#define IDC_EDIT_VAL          1016
#define IDC_BTN_SAVESINGLE    1017

#define IDC_GRP_MALL          1020
#define IDC_BTN_MALL_ALL      1021
#define IDC_BTN_MALL_WHITE    1022
#define IDC_BTN_MALL_OFF      1023
#define IDC_BTN_ANTIOVER      1024
#define IDC_BTN_VIP_ALL       1025
#define IDC_LBL_LEVEL         1026
#define IDC_EDIT_LEVEL        1027
#define IDC_BTN_SET_LEVEL     1028
#define IDC_LBL_PERCENT       1029
#define IDC_EDIT_PERCENT      1030
#define IDC_BTN_SET_PERCENT   1031

#define IDC_GRP_HERO          1035
#define IDC_BTN_HEAL          1036
#define IDC_BTN_KILL          1037
#define IDC_BTN_ADD_SP10      1038
#define IDC_BTN_ADD_SP50      1039
#define IDC_BTN_ADD_STATS     1040
#define IDC_BTN_ADD_XP        1041
#define IDC_BTN_TELEPORT      1042
#define IDC_BTN_GOD           1043
#define IDC_EDIT_CUSTOM_SP    1044
#define IDC_BTN_SET_CUSTOM_SP 1045
#define IDC_EDIT_CUSTOM_STATS 1046
#define IDC_BTN_SET_CUSTOM_STATS 1047
#define IDC_BTN_ADD_STATS2000 1048
#define IDC_EDIT_CUSTOM_XP    1049
#define IDC_BTN_SET_CUSTOM_XP 1050
#define IDC_BTN_ADD_XP50000   1053

#define IDC_GRP_CMD           1050
#define IDC_EDIT_CMD          1051
#define IDC_BTN_SEND_CMD      1052

#define IDC_GRP_LOG           1060
#define IDC_EDIT_LOG          1061
#define IDC_BTN_CLEAR_LOG     1062
#define IDC_BTN_OPEN_TRACE    1063
#define IDC_BTN_OPEN_SAVE     1064
#define IDC_BTN_OPEN_PANEL    1065
#define IDC_BTN_COPY_LOG      1066
#define IDC_CHK_AUTO_SCROLL   1067

// Tab 1 Combat & Map
#define IDC_BTN_SPEED_1X      1070
#define IDC_BTN_SPEED_2X      1071
#define IDC_BTN_SPEED_5X      1072
#define IDC_BTN_SPEED_10X     1073
#define IDC_BTN_FOG_OFF       1074
#define IDC_BTN_FOG_ON        1075
#define IDC_EDIT_GOLD         1076
#define IDC_BTN_SET_GOLD      1077
#define IDC_BTN_QUICK_GOLD    1078
#define IDC_EDIT_LUMBER       1079
#define IDC_BTN_SET_LUMBER    1080
#define IDC_BTN_QUICK_LUMBER  1081

#define IDC_MAIN_TAB          2000
#define IDT_TIMER_STATUS      1

// Global GUI Handles & States
HWND g_hWnd = NULL;
HWND g_hTab = NULL;
HWND g_hEditLog = NULL;
HWND g_hEditCmd = NULL;
HWND g_hEditVal = NULL;
HWND g_hEditPid = NULL;
HWND g_hEditKey = NULL;
HWND g_hEditLevel = NULL;
HWND g_hEditPercent = NULL;
HWND g_hEditCustomSP = NULL;
HWND g_hEditCustomStats = NULL;
HWND g_hEditCustomXP = NULL;
HWND g_hEditGold = NULL;
HWND g_hEditLumber = NULL;

HWND g_hLblWar3 = NULL;
HWND g_hLblPipe = NULL;
HWND g_hChkAutoInject = NULL;
HWND g_hChkAutoScroll = NULL;
HWND g_hBtnAntiOver = NULL;

HFONT g_hFont = NULL;
HFONT g_hFontBold = NULL;
HBRUSH g_hLogBrush = NULL;

WNDPROC g_oldEditCmdProc = NULL;
WNDPROC g_oldEditValProc = NULL;

HANDLE g_hPipe = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_pipeCs;
HANDLE g_hPipeReaderThread = NULL;
bool g_runPipeReader = true;
std::string g_pipeLogBuffer = "";

DWORD g_lastDetectedPid = 0;
bool g_isDllInjected = false;
bool g_antiOverEnabled = true;

// Tab Management
std::vector<HWND> g_tabPages[4];

void AddTabCtrl(int tabIdx, HWND hCtrl) {
    if (tabIdx >= 0 && tabIdx < 4 && hCtrl) {
        g_tabPages[tabIdx].push_back(hCtrl);
    }
}

void SwitchTab(int curSel) {
    for (int t = 0; t < 4; ++t) {
        int showCmd = (t == curSel) ? SW_SHOW : SW_HIDE;
        for (HWND hCtrl : g_tabPages[t]) {
            ShowWindow(hCtrl, showCmd);
        }
    }
}

// Utility: Append to log window with timestamp and optional auto-scroll
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
        SendMessageW(g_hEditLog, EM_SCROLLCARET, 0, 0);
    }
}

// Utility: Copy log text to Windows Clipboard
void CopyLogToClipboard() {
    if (!g_hEditLog) return;
    int len = GetWindowTextLengthW(g_hEditLog);
    if (len == 0) return;
    
    std::wstring text(len + 1, L'\0');
    GetWindowTextW(g_hEditLog, &text[0], len + 1);
    
    if (OpenClipboard(g_hWnd)) {
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (hGlob) {
            void* pMem = GlobalLock(hGlob);
            if (pMem) {
                memcpy(pMem, text.c_str(), (len + 1) * sizeof(wchar_t));
                GlobalUnlock(hGlob);
                SetClipboardData(CF_UNICODETEXT, hGlob);
            }
        }
        CloseClipboard();
        AppendLog(L"[系統] 已將日誌內容成功複製至剪貼簿！");
    }
}

// Utility: Get directory of current executable
std::string GetExeDir() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        exeDir = exeDir.substr(0, lastSlash + 1);
    }
    return exeDir;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], sizeNeeded);
    return wstr;
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
    std::string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], sizeNeeded, NULL, NULL);
    return str;
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

bool IsDllInjected(DWORD pid, const wchar_t* dllName) {
    if (pid == 0) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (lstrcmpiW(me.szModule, dllName) == 0) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool InjectDLL(DWORD processID, const std::string& dllPath, std::wstring& errOut) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processID);
    if (!hProcess) {
        errOut = L"無法開啟目標進程 (OpenProcess 失敗，錯誤碼: " + std::to_wstring(GetLastError()) + L")";
        return false;
    }

    void* loc = VirtualAllocEx(hProcess, 0, MAX_PATH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!loc) {
        errOut = L"無法在目標進程分配記憶體 (VirtualAllocEx 失敗)";
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, loc, dllPath.c_str(), dllPath.length() + 1, 0)) {
        errOut = L"寫入 DLL 路徑失敗 (WriteProcessMemory 失敗)";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, loc, 0, 0);
    if (!hThread) {
        errOut = L"建立遠端線程失敗 (CreateRemoteThread 失敗，錯誤碼: " + std::to_wstring(GetLastError()) + L")";
        VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, 3000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, loc, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

// Named Pipe IPC Communication with CriticalSection Thread Safety
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

void SaveConfigValue(const wchar_t* key, int value) {
    std::string dir = GetExeDir();
    std::wstring iniPath = Utf8ToWide(dir) + L"JassHookConfig.ini";
    wchar_t valBuf[32];
    swprintf_s(valBuf, 32, L"%d", value);
    WritePrivateProfileStringW(L"Spoof", key, valBuf, iniPath.c_str());
}

int LoadConfigValue(const wchar_t* key, int defaultVal) {
    std::string dir = GetExeDir();
    std::wstring iniPath = Utf8ToWide(dir) + L"JassHookConfig.ini";
    return GetPrivateProfileIntW(L"Spoof", key, defaultVal, iniPath.c_str());
}

bool SendPipeCommand(const std::string& cmd) {
    if (!ConnectPipe()) {
        return false;
    }
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipeCs);
        return false;
    }
    std::string formattedCmd = cmd;
    if (formattedCmd.empty() || formattedCmd.back() != '\n') {
        formattedCmd += "\n";
    }
    DWORD bw;
    BOOL ok = WriteFile(g_hPipe, formattedCmd.c_str(), (DWORD)formattedCmd.length(), &bw, NULL);
    if (!ok) {
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&g_pipeCs);
        return false;
    }
    LeaveCriticalSection(&g_pipeCs);
    return true;
}

// Background Live Monitor Streaming Thread
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
            Sleep(35);
        }
    }
    return 0;
}

// Execute Inject Action
void DoInject() {
    DWORD pid = FindProcessId();
    if (pid == 0) {
        AppendLog(L"[-] 注入失敗: 未偵測到 war3.exe，請先開啟遊戲地圖！");
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
            AppendLog(L"[-] 注入失敗: 找不到 JassHook.dll！請確認檔案位於程式同目錄下。");
            MessageBoxW(g_hWnd, L"找不到 JassHook.dll 檔案！", L"錯誤", MB_ICONERROR);
            return;
        }
    }

    AppendLog(L"[*] 正在注入 JassHook.dll 至進程 PID: " + std::to_wstring(pid) + L" ...");
    std::wstring err;
    if (InjectDLL(pid, dllPath, err)) {
        AppendLog(L"[+] 恭喜！JassHook.dll 注入成功！");
        g_isDllInjected = true;
        Sleep(200);
        if (ConnectPipe()) {
            AppendLog(L"[IPC] 已成功連線至魔獸爭霸 IPC 管道！");
        }
    } else {
        AppendLog(L"[-] 注入失敗: " + err);
        MessageBoxW(g_hWnd, (L"注入失敗: " + err + L"\n請確認是否以「系統管理員身分」執行本程式！").c_str(), L"錯誤", MB_ICONERROR);
    }
}

// Generate Save Panel & Open in Notepad
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
        size_t pPos = line.find("Player: ");
        size_t kPos = line.find("Key: \"");
        size_t vPos = line.find("Value: \"");
        if (pPos != std::string::npos && kPos != std::string::npos && vPos != std::string::npos) {
            std::string playerStr = line.substr(pPos + 8, line.find(" |", pPos) - (pPos + 8));
            size_t kEnd = line.find("\"", kPos + 6);
            std::string keyStr = line.substr(kPos + 6, kEnd - (kPos + 6));
            size_t vEnd = line.rfind("\"");
            std::string valStr = line.substr(vPos + 8, vEnd - (vPos + 8));
            uniqueKeys[{playerStr, keyStr}] = valStr;
            if (detectedPid.empty()) detectedPid = playerStr;
        }
    }
    infile.close();

    if (uniqueKeys.empty()) {
        AppendLog(L"[-] local_save.txt 尚未記錄任何存檔變數。");
        MessageBoxW(g_hWnd, L"local_save.txt 尚未有數據，請在遊戲中載入存檔。", L"提示", MB_ICONINFORMATION);
        return;
    }

    std::string panelPath = exeDir + "save_panel.txt";
    std::ofstream outfile(panelPath);
    outfile << "# ========================================================\n";
    outfile << "# 魔獸爭霸 RPG 存檔修改面板 (War3DevTool v2.0)\n";
    outfile << "# 格式: [玩家ID] [存檔變數Key] [新數值Value]\n";
    outfile << "# 修改完畢後存檔 (Ctrl+S)，然後回到工具點選「批量回寫存檔」\n";
    outfile << "# ========================================================\n\n";
    for (const auto& item : uniqueKeys) {
        outfile << item.first.first << " " << item.first.second << " " << item.second << "\n";
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
        std::stringstream ss(line);
        std::string p, k;
        if (ss >> p >> k) {
            std::string v;
            std::getline(ss, v);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (v.empty()) continue;
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

// Send Single Save
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
        AppendLog(L"[IPC] 已發送單筆存檔: " + Utf8ToWide(payload));
    } else {
        AppendLog(L"[-] 發送存檔失敗，管道未連線！");
    }
}

// Process Command Input
void ProcessCustomCommand(const std::string& inputCmd) {
    if (inputCmd.empty()) return;

    std::string cmd = inputCmd;
    if (cmd == "!kill" || cmd == "!killselected") {
        cmd = "KILL";
    }
    else if (cmd == "!heal") {
        cmd = "HEAL";
    }
    else if (cmd == "!tp" || cmd == "!blink" || cmd == "!teleport") {
        cmd = "TELEPORT";
    }
    else if (cmd == "!god" || cmd == "!invul" || cmd == "!wudi") {
        cmd = "GOD";
    }
    else if (cmd.rfind("!sp ", 0) == 0) {
        cmd = "ADD_SP:" + cmd.substr(4);
    }
    else if (cmd.rfind("!stats ", 0) == 0) {
        cmd = "ADD_STATS:" + cmd.substr(7);
    }
    else if (cmd.rfind("!xp ", 0) == 0) {
        cmd = "ADD_XP:" + cmd.substr(4);
    }
    else if (cmd.rfind("!vip ", 0) == 0) {
        cmd = "VIP:" + cmd.substr(5);
    }
    else if (cmd == "!panel") {
        GenerateAndOpenPanel();
        return;
    }
    else if (cmd == "!saveall") {
        SaveAllFromPanel();
        return;
    }
    else if (cmd.rfind("!mall ", 0) == 0) {
        cmd = "MALL:" + cmd.substr(6);
    }
    else if (cmd.rfind("!percent ", 0) == 0) {
        cmd = "PERCENT:" + cmd.substr(9);
    }
    else if (cmd.rfind("!level ", 0) == 0) {
        cmd = "LEVEL:" + cmd.substr(7);
    }
    else if (cmd.rfind("!antiover ", 0) == 0) {
        cmd = "ANTIOVER:" + cmd.substr(10);
    }
    else if (cmd.rfind("!gold ", 0) == 0) {
        cmd = "GOLD:" + cmd.substr(6);
    }
    else if (cmd.rfind("!lumber ", 0) == 0) {
        cmd = "LUMBER:" + cmd.substr(8);
    }
    else if (cmd.rfind("!fog ", 0) == 0) {
        cmd = "FOG:" + cmd.substr(5);
    }
    else if (cmd.rfind("!speed ", 0) == 0) {
        cmd = "SPEED:" + cmd.substr(7);
    }
    else if (cmd.rfind("!save ", 0) == 0) {
        std::string rest = cmd.substr(6);
        std::stringstream ss(rest);
        std::string p, k;
        if (ss >> p >> k) {
            std::string v;
            std::getline(ss, v);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (!v.empty()) {
                cmd = "SAVE:" + p + ":" + k + ":" + v;
            } else {
                AppendLog(L"[-] 格式錯誤: 必須提供數值！格式: !save <PlayerID> <Key> <Value>");
                return;
            }
        } else {
            AppendLog(L"[-] 格式錯誤: !save <PlayerID> <Key> <Value>");
            return;
        }
    }
    else if (cmd[0] == '!') {
        size_t sp = cmd.find(' ');
        if (sp != std::string::npos) {
            cmd = "NATIVE:" + cmd.substr(1, sp - 1) + ":" + cmd.substr(sp + 1);
        } else {
            cmd = "NATIVE:" + cmd.substr(1) + ":";
        }
    }

    if (SendPipeCommand(cmd)) {
        AppendLog(L"[IPC] 已發送指令: " + Utf8ToWide(cmd));
    } else {
        AppendLog(L"[-] 發送失敗: IPC Named Pipe 未連線 (請確認 JassHook.dll 已注入)。");
    }
}

// Subclass Edit Controls for Enter Key
LRESULT CALLBACK EditCmdSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_SEND_CMD, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProc(g_oldEditCmdProc, hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK EditValSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(g_hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_SAVESINGLE, BN_CLICKED), 0);
        return 0;
    }
    return CallWindowProc(g_oldEditValProc, hWnd, uMsg, wParam, lParam);
}

// Window Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        SetTimer(hWnd, IDT_TIMER_STATUS, 1000, NULL);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        if (hwndStatic == g_hEditLog) {
            SetTextColor(hdcStatic, RGB(0, 230, 118)); // Bright Hacker/Console Green
            SetBkColor(hdcStatic, RGB(22, 24, 28));
            return (INT_PTR)g_hLogBrush;
        }
        SetBkMode(hdcStatic, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_TIMER: {
        if (wParam == IDT_TIMER_STATUS) {
            DWORD pid = FindProcessId();
            g_lastDetectedPid = pid;
            if (pid != 0) {
                g_isDllInjected = IsDllInjected(pid, L"JassHook.dll");
                wchar_t buf[128];
                swprintf_s(buf, L"遊戲進程: 已偵測到 war3.exe (PID: %lu) %s", pid, g_isDllInjected ? L"【已注入】" : L"【未注入】");
                SetWindowTextW(g_hLblWar3, buf);

                if (!g_isDllInjected && SendMessage(g_hChkAutoInject, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    DoInject();
                }
            } else {
                g_isDllInjected = false;
                SetWindowTextW(g_hLblWar3, L"遊戲進程: 未偵測到 war3.exe (請啟動遊戲)");
                DisconnectPipe();
            }

            if (ConnectPipe()) {
                SetWindowTextW(g_hLblPipe, L"IPC 連線: 已連線 (\\.\\pipe\\War3JassDevTool 正常通訊)");
            } else {
                SetWindowTextW(g_hLblPipe, L"IPC 連線: 尚未連線 (請注入 JassHook.dll)");
            }
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh && pnmh->idFrom == IDC_MAIN_TAB && pnmh->code == TCN_SELCHANGE) {
            int curSel = TabCtrl_GetCurSel(g_hTab);
            SwitchTab(curSel);
        }
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case IDC_BTN_INJECT:
            DoInject();
            break;

        case IDC_BTN_RECONNECT:
            DisconnectPipe();
            if (ConnectPipe()) {
                AppendLog(L"[IPC] 重新連線成功！");
            } else {
                AppendLog(L"[-] 重新連線失敗，Pipe 不存在或已被佔用。");
            }
            break;

        case IDC_BTN_PANEL:
            GenerateAndOpenPanel();
            break;

        case IDC_BTN_SAVEALL:
            SaveAllFromPanel();
            break;

        case IDC_BTN_SAVESINGLE:
            SendSingleSave();
            break;

        case IDC_BTN_MALL_ALL:
            SaveConfigValue(L"MallMode", 2);
            if (SendPipeCommand("MALL:2")) {
                AppendLog(L"[商城] 已切換為模式 2: 全開模式 (所有非黑名單道具一律解鎖，已寫入 INI)");
            }
            break;

        case IDC_BTN_MALL_WHITE:
            SaveConfigValue(L"MallMode", 1);
            if (SendPipeCommand("MALL:1")) {
                AppendLog(L"[商城] 已切換為模式 1: 白名單模式 (僅解鎖 mall_spoof.txt 中的道具，已寫入 INI)");
            }
            break;

        case IDC_BTN_MALL_OFF:
            SaveConfigValue(L"MallMode", 0);
            if (SendPipeCommand("MALL:0")) {
                AppendLog(L"[商城] 已切換為模式 0: 關閉偽造 (恢復遊戲原始狀態，已寫入 INI)");
            }
            break;

        case IDC_BTN_VIP_ALL:
            SaveConfigValue(L"PlatformVIP", 2);
            if (SendPipeCommand("VIP:2")) {
                AppendLog(L"[VIP] 已啟用平台至尊VIP (VIP:2，特權/光環/翅膀/存檔全開，已寫入 INI)");
            }
            break;

        case IDC_BTN_ANTIOVER: {
            g_antiOverEnabled = !g_antiOverEnabled;
            SaveConfigValue(L"PreventGameOver", g_antiOverEnabled ? 1 : 0);
            std::string payload = g_antiOverEnabled ? "ANTIOVER:1" : "ANTIOVER:0";
            if (SendPipeCommand(payload)) {
                if (g_antiOverEnabled) {
                    AppendLog(L"[防護] 已開啟防失敗/防結算保護 (ANTIOVER:1，已寫入 INI)");
                    SetWindowTextW(g_hBtnAntiOver, L"🛡️ 防失敗保護 (目前: 開)");
                } else {
                    AppendLog(L"[防護] 已關閉防失敗/防結算保護 (ANTIOVER:0，已寫入 INI)");
                    SetWindowTextW(g_hBtnAntiOver, L"🛡️ 防失敗保護 (目前: 關)");
                }
            }
            break;
        }

        case IDC_BTN_HEAL:
            if (SendPipeCommand("HEAL")) {
                AppendLog(L"[英雄] 已補滿目標單位的生命值 (HEAL)");
            }
            break;

        case IDC_BTN_TELEPORT:
            if (SendPipeCommand("TELEPORT")) {
                AppendLog(L"[英雄] 瞬移選中單位至滑鼠地形座標 (TELEPORT / Ctrl+M)");
            }
            break;

        case IDC_BTN_GOD:
            if (SendPipeCommand("GOD")) {
                AppendLog(L"[英雄] 已將選中單位設為官方無敵 (God Mode / Ctrl+G，生命值鎖定10億)");
            }
            break;

        case IDC_BTN_KILL:
            if (SendPipeCommand("KILL")) {
                AppendLog(L"[動作] 已秒殺選取/滑鼠懸停目標單位 (KILL)！");
            }
            break;

        case IDC_BTN_ADD_SP10:
            if (SendPipeCommand("ADD_SP:10")) {
                AppendLog(L"[英雄] 已為目標英雄增加 10 點技能點 (ADD_SP:10)");
            }
            break;

        case IDC_BTN_ADD_SP50:
            if (SendPipeCommand("ADD_SP:50")) {
                AppendLog(L"[英雄] 已為目標英雄增加 50 點技能點 (ADD_SP:50)");
            }
            break;

        case IDC_BTN_ADD_STATS:
            if (SendPipeCommand("ADD_STATS:500")) {
                AppendLog(L"[英雄] 已為目標英雄增加 500 點力量與敏捷 (ADD_STATS:500)");
            }
            break;

        case IDC_BTN_ADD_XP:
            if (SendPipeCommand("ADD_XP:10000")) {
                AppendLog(L"[英雄] 已為目標英雄增加 10000 點經驗值 (ADD_XP:10000)");
            }
            break;

        case IDC_BTN_SET_CUSTOM_SP: {
            char buf[32];
            GetWindowTextA(g_hEditCustomSP, buf, sizeof(buf));
            if (strlen(buf) > 0) {
                SendPipeCommand(std::string("ADD_SP:") + buf);
                AppendLog(L"[英雄] 已為目標英雄增加 " + Utf8ToWide(buf) + L" 點技能點！");
            }
            break;
        }

        case IDC_BTN_ADD_STATS2000:
            if (SendPipeCommand("ADD_STATS:2000")) {
                AppendLog(L"[英雄] 已為目標英雄增加 2000 點力量與敏捷 (ADD_STATS:2000)");
            }
            break;

        case IDC_BTN_SET_CUSTOM_STATS: {
            char buf[32];
            GetWindowTextA(g_hEditCustomStats, buf, sizeof(buf));
            if (strlen(buf) > 0) {
                SendPipeCommand(std::string("ADD_STATS:") + buf);
                AppendLog(L"[英雄] 已為目標英雄增加 " + Utf8ToWide(buf) + L" 點全屬性！");
            }
            break;
        }

        case IDC_BTN_ADD_XP50000:
            if (SendPipeCommand("ADD_XP:50000")) {
                AppendLog(L"[英雄] 已為目標英雄增加 50000 點經驗值 (ADD_XP:50000)");
            }
            break;

        case IDC_BTN_SET_CUSTOM_XP: {
            char buf[32];
            GetWindowTextA(g_hEditCustomXP, buf, sizeof(buf));
            if (strlen(buf) > 0) {
                SendPipeCommand(std::string("ADD_XP:") + buf);
                AppendLog(L"[英雄] 已為目標英雄增加 " + Utf8ToWide(buf) + L" 點經驗值！");
            }
            break;
        }

        case IDC_BTN_SPEED_1X:
            if (SendPipeCommand("SPEED:1")) AppendLog(L"[齒輪] 遊戲倍速恢復為 1.0x 正常速度");
            break;
        case IDC_BTN_SPEED_2X:
            if (SendPipeCommand("SPEED:2")) AppendLog(L"[齒輪] 遊戲倍速設定為 2.0x 快速");
            break;
        case IDC_BTN_SPEED_5X:
            if (SendPipeCommand("SPEED:5")) AppendLog(L"[齒輪] 遊戲倍速設定為 5.0x 極速");
            break;
        case IDC_BTN_SPEED_10X:
            if (SendPipeCommand("SPEED:10")) AppendLog(L"[齒輪] 遊戲倍速設定為 10.0x 飛速");
            break;

        case IDC_BTN_FOG_OFF:
            if (SendPipeCommand("FOG:0")) AppendLog(L"[視野] 已開啟全圖視野 (消除黑色迷霧與戰爭迷霧)！");
            break;
        case IDC_BTN_FOG_ON:
            if (SendPipeCommand("FOG:1")) AppendLog(L"[視野] 已恢復預設地圖戰爭迷霧。");
            break;

        case IDC_BTN_SET_GOLD: {
            char buf[32];
            GetWindowTextA(g_hEditGold, buf, sizeof(buf));
            if (strlen(buf) > 0) {
                SendPipeCommand(std::string("GOLD:") + buf);
                AppendLog(L"[資源] 已增加金幣: +" + Utf8ToWide(buf));
            }
            break;
        }
        case IDC_BTN_QUICK_GOLD:
            if (SendPipeCommand("GOLD:100000")) AppendLog(L"[資源] 快速增加 100,000 金幣！");
            break;

        case IDC_BTN_SET_LUMBER: {
            char buf[32];
            GetWindowTextA(g_hEditLumber, buf, sizeof(buf));
            if (strlen(buf) > 0) {
                SendPipeCommand(std::string("LUMBER:") + buf);
                AppendLog(L"[資源] 已增加木材: +" + Utf8ToWide(buf));
            }
            break;
        }
        case IDC_BTN_QUICK_LUMBER:
            if (SendPipeCommand("LUMBER:100000")) AppendLog(L"[資源] 快速增加 100,000 木材！");
            break;

        case IDC_BTN_SET_LEVEL: {
            char lvlBuf[32];
            GetWindowTextA(g_hEditLevel, lvlBuf, sizeof(lvlBuf));
            int lvl = atoi(lvlBuf);
            if (lvl <= 0) lvl = 1;
            SaveConfigValue(L"MapLevel", lvl);
            std::string payload = std::string("LEVEL:") + std::to_string(lvl);
            if (SendPipeCommand(payload)) {
                AppendLog(L"[等級] 已設定偽造地圖等級為: " + std::to_wstring(lvl) + L" (已寫入 INI)");
            }
            break;
        }

        case IDC_BTN_SET_PERCENT: {
            char pctBuf[32];
            GetWindowTextA(g_hEditPercent, pctBuf, sizeof(pctBuf));
            int pct = atoi(pctBuf);
            if (pct < 1) pct = 1;
            if (pct > 100) pct = 100;
            SaveConfigValue(L"MallPercent", pct);
            SaveConfigValue(L"MallMode", 3);
            std::string payload = std::string("PERCENT:") + std::to_string(pct);
            SendPipeCommand(payload);
            SendPipeCommand("MALL:3");
            AppendLog(L"[商城] 已啟用模式 3 (隨機百分比)，機率設為: " + std::to_wstring(pct) + L"% (已寫入 INI)");
            break;
        }

        case IDC_BTN_SEND_CMD: {
            wchar_t wCmdBuf[512];
            GetWindowTextW(g_hEditCmd, wCmdBuf, sizeof(wCmdBuf) / sizeof(wchar_t));
            std::string cmd = WideToUtf8(wCmdBuf);
            if (!cmd.empty()) {
                ProcessCustomCommand(cmd);
                SetWindowTextW(g_hEditCmd, L"");
                SetFocus(g_hEditCmd);
            }
            break;
        }

        case IDC_BTN_CLEAR_LOG:
            SetWindowTextW(g_hEditLog, L"");
            AppendLog(L"[*] 日誌已清除。");
            break;

        case IDC_BTN_COPY_LOG:
            CopyLogToClipboard();
            break;

        case IDC_BTN_OPEN_TRACE: {
            std::string p = GetExeDir() + "dztrace.log";
            ShellExecuteA(hWnd, "open", "notepad.exe", p.c_str(), NULL, SW_SHOWNORMAL);
            break;
        }

        case IDC_BTN_OPEN_SAVE: {
            std::string p = GetExeDir() + "local_save.txt";
            ShellExecuteA(hWnd, "open", "notepad.exe", p.c_str(), NULL, SW_SHOWNORMAL);
            break;
        }

        case IDC_BTN_OPEN_PANEL: {
            std::string p = GetExeDir() + "save_panel.txt";
            ShellExecuteA(hWnd, "open", "notepad.exe", p.c_str(), NULL, SW_SHOWNORMAL);
            break;
        }
        }
        break;
    }

    case WM_DESTROY:
        g_runPipeReader = false;
        KillTimer(hWnd, IDT_TIMER_STATUS);
        DisconnectPipe();
        if (g_hPipeReaderThread) {
            WaitForSingleObject(g_hPipeReaderThread, 300);
            CloseHandle(g_hPipeReaderThread);
            g_hPipeReaderThread = NULL;
        }
        DeleteCriticalSection(&g_pipeCs);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hLogBrush) DeleteObject(g_hLogBrush);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Entry Point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetProcessDPIAware();
    EnableDebugPrivilege();
    InitializeCriticalSection(&g_pipeCs);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = L"War3DevToolUI_Class";
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExW(&wcex);

    int winWidth = 840;
    int winHeight = 900;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winWidth) / 2;
    int posY = (screenH - winHeight) / 2;

    g_hWnd = CreateWindowExW(
        0, L"War3DevToolUI_Class",
        L"Warcraft III JassHook 開發者控制台 v2.0 (Tab 模組化增強版)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winWidth, winHeight,
        NULL, NULL, hInstance, NULL);

    if (!g_hWnd) return 0;

    g_hFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");

    g_hFontBold = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");

    g_hLogBrush = CreateSolidBrush(RGB(22, 24, 28));

    auto CreateCtrl = [&](LPCWSTR className, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id) -> HWND {
        HWND hCtrl = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                    x, y, w, h, g_hWnd, (HMENU)(INT_PTR)id, hInstance, NULL);
        SendMessage(hCtrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        return hCtrl;
    };

    auto CreateTabCtrl = [&](int tabIdx, LPCWSTR className, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id) -> HWND {
        HWND hCtrl = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                    x, y, w, h, g_hWnd, (HMENU)(INT_PTR)id, hInstance, NULL);
        SendMessage(hCtrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        AddTabCtrl(tabIdx, hCtrl);
        return hCtrl;
    };

    // =========================================================================
    // 常駐頂部: 遊戲偵測與注入管理 (Injection Manager - Always Visible)
    // =========================================================================
    CreateCtrl(L"BUTTON", L"【遊戲進程與注入管理】", BS_GROUPBOX, 15, 10, 790, 95, IDC_GRP_INJECT);
    g_hLblWar3 = CreateCtrl(L"STATIC", L"遊戲進程: 偵測中...", SS_LEFT, 30, 32, 330, 20, IDC_LBL_WAR3_STATUS);
    g_hLblPipe = CreateCtrl(L"STATIC", L"IPC 連線: 尚未連線", SS_LEFT, 30, 56, 330, 20, IDC_LBL_PIPE_STATUS);
    CreateCtrl(L"STATIC", L"註: 本程式已具備管理員權限，可直接穿透 KK 平台防護", SS_LEFT, 30, 78, 380, 18, 0);

    g_hChkAutoInject = CreateCtrl(L"BUTTON", L"啟動時自動注入", BS_AUTOCHECKBOX, 380, 30, 140, 24, IDC_CHK_AUTO_INJECT);
    SendMessage(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

    CreateCtrl(L"BUTTON", L"⚡ 一鍵注入", BS_PUSHBUTTON, 530, 26, 120, 32, IDC_BTN_INJECT);
    CreateCtrl(L"BUTTON", L"🔄 重新連線", BS_PUSHBUTTON, 660, 26, 125, 32, IDC_BTN_RECONNECT);

    // =========================================================================
    // 主 Tab 分頁標籤控制項 (Tab Control)
    // =========================================================================
    g_hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        15, 115, 790, 735, g_hWnd, (HMENU)(INT_PTR)IDC_MAIN_TAB, hInstance, NULL);
    SendMessage(g_hTab, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    TCITEMW tie{};
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)L"🦸 英雄與動作 (Hero)";
    TabCtrl_InsertItem(g_hTab, 0, &tie);
    tie.pszText = (LPWSTR)L"⚔️ 戰鬥與地圖 (Combat & Map)";
    TabCtrl_InsertItem(g_hTab, 1, &tie);
    tie.pszText = (LPWSTR)L"💎 平台與存檔 (Platform & Save)";
    TabCtrl_InsertItem(g_hTab, 2, &tie);
    tie.pszText = (LPWSTR)L"📡 即時監控 (Live Monitor)";
    TabCtrl_InsertItem(g_hTab, 3, &tie);

    // =========================================================================
    // 🏷️ TAB 0: 【英雄與動作】 (Hero & Unit Actions)
    // =========================================================================
    // 1. 常用戰鬥與控制
    CreateTabCtrl(0, L"BUTTON", L"【常用英雄控制 (支援遊戲內熱鍵 Ctrl+M / Ctrl+G / Ctrl+H / Ctrl+K)】", BS_GROUPBOX, 30, 155, 760, 105, 0);
    CreateTabCtrl(0, L"BUTTON", L"🌀 瞬移至滑鼠 (Ctrl+M)", BS_PUSHBUTTON, 45, 185, 170, 36, IDC_BTN_TELEPORT);
    CreateTabCtrl(0, L"BUTTON", L"🛡️ 設為無敵 (Ctrl+G)", BS_PUSHBUTTON, 225, 185, 170, 36, IDC_BTN_GOD);
    CreateTabCtrl(0, L"BUTTON", L"❤️ 生命全滿 (Ctrl+H)", BS_PUSHBUTTON, 405, 185, 170, 36, IDC_BTN_HEAL);
    CreateTabCtrl(0, L"BUTTON", L"💀 秒殺目標 (Ctrl+K)", BS_PUSHBUTTON, 585, 185, 185, 36, IDC_BTN_KILL);
    CreateTabCtrl(0, L"STATIC", L"提示: 選取單位後點擊按鈕，或在遊戲中直接按鍵即可發動。瞬移直達滑鼠地表，無敵鎖定10億生命值。", SS_LEFT, 45, 230, 725, 20, 0);

    // 2. 英雄數值與進度增強
    CreateTabCtrl(0, L"BUTTON", L"【英雄數值與進度增強 (DzAPI 記憶體逆向)】", BS_GROUPBOX, 30, 270, 760, 180, 0);
    // 技能點
    CreateTabCtrl(0, L"BUTTON", L"🌟 技能點 +10", BS_PUSHBUTTON, 45, 300, 125, 30, IDC_BTN_ADD_SP10);
    CreateTabCtrl(0, L"BUTTON", L"🌟 技能點 +50", BS_PUSHBUTTON, 180, 300, 125, 30, IDC_BTN_ADD_SP50);
    CreateTabCtrl(0, L"STATIC", L"自訂點數:", SS_LEFT, 320, 306, 68, 20, 0);
    g_hEditCustomSP = CreateTabCtrl(0, L"EDIT", L"100", WS_BORDER | ES_NUMBER, 395, 303, 80, 24, IDC_EDIT_CUSTOM_SP);
    CreateTabCtrl(0, L"BUTTON", L"增加自訂技能點", BS_PUSHBUTTON, 485, 301, 150, 28, IDC_BTN_SET_CUSTOM_SP);
    // 屬性
    CreateTabCtrl(0, L"BUTTON", L"💪 全屬性 +500", BS_PUSHBUTTON, 45, 340, 125, 30, IDC_BTN_ADD_STATS);
    CreateTabCtrl(0, L"BUTTON", L"💪 全屬性 +2000", BS_PUSHBUTTON, 180, 340, 125, 30, IDC_BTN_ADD_STATS2000);
    CreateTabCtrl(0, L"STATIC", L"自訂屬性:", SS_LEFT, 320, 346, 68, 20, 0);
    g_hEditCustomStats = CreateTabCtrl(0, L"EDIT", L"5000", WS_BORDER | ES_NUMBER, 395, 343, 80, 24, IDC_EDIT_CUSTOM_STATS);
    CreateTabCtrl(0, L"BUTTON", L"增加自訂全屬性", BS_PUSHBUTTON, 485, 341, 150, 28, IDC_BTN_SET_CUSTOM_STATS);
    // 經驗
    CreateTabCtrl(0, L"BUTTON", L"📖 經驗 +10000", BS_PUSHBUTTON, 45, 380, 125, 30, IDC_BTN_ADD_XP);
    CreateTabCtrl(0, L"BUTTON", L"📖 經驗 +50000", BS_PUSHBUTTON, 180, 380, 125, 30, IDC_BTN_ADD_XP50000);
    CreateTabCtrl(0, L"STATIC", L"自訂經驗:", SS_LEFT, 320, 386, 68, 20, 0);
    g_hEditCustomXP = CreateTabCtrl(0, L"EDIT", L"100000", WS_BORDER | ES_NUMBER, 395, 383, 80, 24, IDC_EDIT_CUSTOM_XP);
    CreateTabCtrl(0, L"BUTTON", L"增加自訂經驗值", BS_PUSHBUTTON, 485, 381, 150, 28, IDC_BTN_SET_CUSTOM_XP);
    CreateTabCtrl(0, L"STATIC", L"註: 屬性修改會同步提升目標英雄的力量與敏捷，技能點與經驗值即時生效不降級。", SS_LEFT, 45, 420, 725, 20, 0);

    // 3. 遊戲內熱鍵指引與操作說明
    CreateTabCtrl(0, L"BUTTON", L"【遊戲內熱鍵指引與底層技術說明】", BS_GROUPBOX, 30, 460, 760, 240, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ [Ctrl + M] : 瞬移選中單位至滑鼠指向地表 (調用 Game.dll CWidget::SetCoord，穿越障礙物無延遲)", SS_LEFT, 45, 490, 725, 20, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ [Ctrl + G] : 將選中單位改為官方無敵 (調用 CGameState::SetUnitInvulnerable，並鎖定 10 億 HP)", SS_LEFT, 45, 520, 725, 20, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ [Ctrl + H] : 補滿選中/懸停單位生命值 (動態修復目標 HP 最大值)", SS_LEFT, 45, 550, 725, 20, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ [Ctrl + K] : 秒殺選中/懸停目標單位 (目標 HP 歸零)", SS_LEFT, 45, 580, 725, 20, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ 指令支援   : 在即時監控分頁可輸入 !tp, !god, !heal, !kill, !sp <點數>, !stats <數值>, !xp <經驗>", SS_LEFT, 45, 610, 725, 20, 0);
    CreateTabCtrl(0, L"STATIC", L"▶ 安全機制   : 所有操作均在魔獸爭霸主線程執行，並具有 SEH 異常保護，杜絕任何遊戲卡死閃退。", SS_LEFT, 45, 640, 725, 20, 0);

    // =========================================================================
    // 🏷️ TAB 1: 【戰鬥與地圖】 (Combat & Map)
    // =========================================================================
    // 1. 遊戲倍速齒輪
    CreateTabCtrl(1, L"BUTTON", L"【⏱️ 遊戲倍速齒輪 (跳過防守波次與計時等待)】", BS_GROUPBOX, 30, 155, 760, 105, 0);
    CreateTabCtrl(1, L"BUTTON", L"1.0x 正常速度", BS_PUSHBUTTON, 45, 185, 130, 36, IDC_BTN_SPEED_1X);
    CreateTabCtrl(1, L"BUTTON", L"⚡ 2.0x 快速", BS_PUSHBUTTON, 190, 185, 130, 36, IDC_BTN_SPEED_2X);
    CreateTabCtrl(1, L"BUTTON", L"🚀 5.0x 極速", BS_PUSHBUTTON, 335, 185, 130, 36, IDC_BTN_SPEED_5X);
    CreateTabCtrl(1, L"BUTTON", L"⏩ 10.0x 飛速", BS_PUSHBUTTON, 480, 185, 130, 36, IDC_BTN_SPEED_10X);
    CreateTabCtrl(1, L"STATIC", L"說明: 可直接調整魔獸爭霸遊戲運算幀率，快速推進波次與計時器，節省大量等待時間。", SS_LEFT, 45, 230, 725, 20, 0);

    // 2. 迷霧與全圖視野
    CreateTabCtrl(1, L"BUTTON", L"【👁️ 迷霧與全圖視野 (開圖透視)】", BS_GROUPBOX, 30, 270, 760, 105, 0);
    CreateTabCtrl(1, L"BUTTON", L"👁️ 開啟全圖視野 (消除黑色迷霧與戰爭迷霧)", BS_PUSHBUTTON, 45, 300, 340, 36, IDC_BTN_FOG_OFF);
    CreateTabCtrl(1, L"BUTTON", L"🔒 恢復預設迷霧 (開啟迷霧)", BS_PUSHBUTTON, 400, 300, 250, 36, IDC_BTN_FOG_ON);
    CreateTabCtrl(1, L"STATIC", L"說明: 透過底層調用 FogEnable 與 FogMaskEnable，全開地圖視野且不被地圖防作弊反制。", SS_LEFT, 45, 345, 725, 20, 0);

    // 3. 玩家資源修改
    CreateTabCtrl(1, L"BUTTON", L"【💰 玩家資源修改 (金幣與木材)】", BS_GROUPBOX, 30, 385, 760, 130, 0);
    CreateTabCtrl(1, L"STATIC", L"金幣 (Gold):", SS_LEFT, 45, 415, 90, 20, 0);
    g_hEditGold = CreateTabCtrl(1, L"EDIT", L"100000", WS_BORDER | ES_NUMBER, 140, 412, 100, 24, IDC_EDIT_GOLD);
    CreateTabCtrl(1, L"BUTTON", L"💰 獲得金幣", BS_PUSHBUTTON, 250, 410, 120, 28, IDC_BTN_SET_GOLD);
    CreateTabCtrl(1, L"BUTTON", L"+100,000", BS_PUSHBUTTON, 380, 410, 95, 28, IDC_BTN_QUICK_GOLD);

    CreateTabCtrl(1, L"STATIC", L"木材 (Lumber):", SS_LEFT, 45, 455, 90, 20, 0);
    g_hEditLumber = CreateTabCtrl(1, L"EDIT", L"100000", WS_BORDER | ES_NUMBER, 140, 452, 100, 24, IDC_EDIT_LUMBER);
    CreateTabCtrl(1, L"BUTTON", L"🌲 獲得木材", BS_PUSHBUTTON, 250, 450, 120, 28, IDC_BTN_SET_LUMBER);
    CreateTabCtrl(1, L"BUTTON", L"+100,000", BS_PUSHBUTTON, 380, 450, 95, 28, IDC_BTN_QUICK_LUMBER);
    CreateTabCtrl(1, L"STATIC", L"說明: 直接修改玩家結構體資源偏移，即時入帳可用於建造、升級與商店購買。", SS_LEFT, 45, 490, 725, 20, 0);

    // 4. 戰鬥擴充框架
    CreateTabCtrl(1, L"BUTTON", L"【⚡ 戰鬥機制與擴充說明】", BS_GROUPBOX, 30, 525, 760, 175, 0);
    CreateTabCtrl(1, L"STATIC", L"▶ 技能無冷卻 (0 CD) 與 無限法力 (100% Mana) 框架已預載，支援透過自訂指令擴充。", SS_LEFT, 45, 555, 725, 20, 0);
    CreateTabCtrl(1, L"STATIC", L"▶ 支援在控制台輸入指令: !speed <倍率>, !fog <0|1>, !gold <數值>, !lumber <數值>", SS_LEFT, 45, 585, 725, 20, 0);
    CreateTabCtrl(1, L"STATIC", L"▶ 透過 Command Registry 函數註冊表架構，未來新增任何功能均零衝突、即插即用！", SS_LEFT, 45, 615, 725, 20, 0);

    // =========================================================================
    // 🏷️ TAB 2: 【平台與存檔】 (Platform & Save)
    // =========================================================================
    // 1. 商城與平台特權
    CreateTabCtrl(2, L"BUTTON", L"【商城與平台特權 (DzAPI 逆向增強)】", BS_GROUPBOX, 30, 155, 760, 145, 0);
    CreateTabCtrl(2, L"BUTTON", L"🛒 商城全開 (!mall 2)", BS_PUSHBUTTON, 45, 185, 160, 32, IDC_BTN_MALL_ALL);
    CreateTabCtrl(2, L"BUTTON", L"📋 商城白名單 (!mall 1)", BS_PUSHBUTTON, 215, 185, 160, 32, IDC_BTN_MALL_WHITE);
    CreateTabCtrl(2, L"BUTTON", L"👑 平台至尊VIP (!vip 2)", BS_PUSHBUTTON, 385, 185, 160, 32, IDC_BTN_VIP_ALL);
    CreateTabCtrl(2, L"BUTTON", L"❌ 關閉商城 (!mall 0)", BS_PUSHBUTTON, 555, 185, 150, 32, IDC_BTN_MALL_OFF);

    g_hBtnAntiOver = CreateTabCtrl(2, L"BUTTON", L"🛡️ 防失敗保護 (目前: 開)", BS_PUSHBUTTON, 45, 228, 170, 30, IDC_BTN_ANTIOVER);
    int savedLevel = LoadConfigValue(L"MapLevel", 99);
    int savedPercent = LoadConfigValue(L"MallPercent", 30);
    int savedMallMode = LoadConfigValue(L"MallMode", 2);

    CreateTabCtrl(2, L"STATIC", L"地圖等級:", SS_LEFT, 230, 233, 65, 20, IDC_LBL_LEVEL);
    g_hEditLevel = CreateTabCtrl(2, L"EDIT", std::to_wstring(savedLevel).c_str(), WS_BORDER | ES_NUMBER, 298, 230, 55, 24, IDC_EDIT_LEVEL);
    CreateTabCtrl(2, L"BUTTON", L"設定等級", BS_PUSHBUTTON, 360, 228, 85, 28, IDC_BTN_SET_LEVEL);

    CreateTabCtrl(2, L"STATIC", L"商城隨機百分比 (%):", SS_LEFT, 460, 233, 130, 20, IDC_LBL_PERCENT);
    g_hEditPercent = CreateTabCtrl(2, L"EDIT", std::to_wstring(savedPercent).c_str(), WS_BORDER | ES_NUMBER, 595, 230, 50, 24, IDC_EDIT_PERCENT);
    CreateTabCtrl(2, L"BUTTON", L"設定百分比 (模式3)", BS_PUSHBUTTON, 652, 228, 125, 28, IDC_BTN_SET_PERCENT);
    CreateTabCtrl(2, L"STATIC", L"說明: 至尊VIP解鎖翅膀光環特權，防失敗保護攔截地圖非正常結算，商城支援全開或白名單過濾陷阱道具。", SS_LEFT, 45, 268, 725, 20, 0);

    // 2. 存檔管理與批量回寫
    CreateTabCtrl(2, L"BUTTON", L"【存檔管理與批量回寫 (Save Manager)】", BS_GROUPBOX, 30, 310, 760, 215, 0);
    CreateTabCtrl(2, L"BUTTON", L"📝 產生並開啟存檔面板 (!panel)", BS_PUSHBUTTON, 45, 340, 230, 34, IDC_BTN_PANEL);
    CreateTabCtrl(2, L"BUTTON", L"💾 批量回寫存檔 (!saveall)", BS_PUSHBUTTON, 290, 340, 200, 34, IDC_BTN_SAVEALL);
    CreateTabCtrl(2, L"STATIC", L"(在記事本修改數值保存後，點 !saveall 回寫至遊戲)", SS_LEFT, 500, 347, 280, 20, 0);

    CreateTabCtrl(2, L"STATIC", L"手動存檔:", SS_LEFT, 45, 390, 68, 20, IDC_LBL_MANUAL_SAVE);
    g_hEditPid = CreateTabCtrl(2, L"EDIT", L"1048602", WS_BORDER | ES_AUTOHSCROLL, 120, 387, 85, 24, IDC_EDIT_PID);
    g_hEditKey = CreateTabCtrl(2, L"EDIT", L"存檔Key (如: IWJCS)", WS_BORDER | ES_AUTOHSCROLL, 215, 387, 160, 24, IDC_EDIT_KEY);
    g_hEditVal = CreateTabCtrl(2, L"EDIT", L"新數值Value", WS_BORDER | ES_AUTOHSCROLL, 385, 387, 220, 24, IDC_EDIT_VAL);
    CreateTabCtrl(2, L"BUTTON", L"發送單筆存檔", BS_PUSHBUTTON, 615, 385, 130, 28, IDC_BTN_SAVESINGLE);

    CreateTabCtrl(2, L"BUTTON", L"📂 開啟 local_save.txt", BS_PUSHBUTTON, 45, 430, 180, 30, IDC_BTN_OPEN_SAVE);
    CreateTabCtrl(2, L"BUTTON", L"📝 開啟 save_panel.txt", BS_PUSHBUTTON, 240, 430, 180, 30, IDC_BTN_OPEN_PANEL);
    CreateTabCtrl(2, L"BUTTON", L"📄 開啟 dztrace.log", BS_PUSHBUTTON, 435, 430, 180, 30, IDC_BTN_OPEN_TRACE);
    CreateTabCtrl(2, L"STATIC", L"說明: 支援在遊戲內動態向伺服器寫入存檔數值，並自動偽造 JASS 字符串返回。", SS_LEFT, 45, 480, 725, 20, 0);

    // 3. 說明
    CreateTabCtrl(2, L"BUTTON", L"【平台存檔與反作弊機制說明】", BS_GROUPBOX, 30, 535, 760, 165, 0);
    CreateTabCtrl(2, L"STATIC", L"▶ 每次進入遊戲載入存檔時，工具會自動擷取伺服器下發的存檔變數並記錄到 local_save.txt。", SS_LEFT, 45, 565, 725, 20, 0);
    CreateTabCtrl(2, L"STATIC", L"▶ 點選「產生並開啟存檔面板」可將所有偵測到的 Key 格式化輸出到記事本，供您任意修改數值。", SS_LEFT, 45, 595, 725, 20, 0);
    CreateTabCtrl(2, L"STATIC", L"▶ 修改完成後在記事本儲存，點選「批量回寫存檔」即可透過 Named Pipe 一鍵同步進遊戲！", SS_LEFT, 45, 625, 725, 20, 0);

    // =========================================================================
    // 🏷️ TAB 3: 【即時監控】 (Live Monitor)
    // =========================================================================
    // 1. 自訂指令輸入
    CreateTabCtrl(3, L"BUTTON", L"【自訂指令與 Native 動態呼叫】", BS_GROUPBOX, 30, 155, 760, 68, IDC_GRP_CMD);
    g_hEditCmd = CreateTabCtrl(3, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 45, 180, 595, 28, IDC_EDIT_CMD);
    CreateTabCtrl(3, L"BUTTON", L"發送指令 (Enter)", BS_PUSHBUTTON, 650, 179, 125, 30, IDC_BTN_SEND_CMD);

    // 2. 即時系統日誌與串流
    CreateTabCtrl(3, L"BUTTON", L"【即時系統日誌與 JASS / DzAPI 即時監控串流】", BS_GROUPBOX, 30, 230, 760, 480, IDC_GRP_LOG);
    g_hEditLog = CreateTabCtrl(3, L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            45, 255, 730, 405, IDC_EDIT_LOG);

    CreateTabCtrl(3, L"BUTTON", L"🧹 清除日誌", BS_PUSHBUTTON, 45, 672, 100, 28, IDC_BTN_CLEAR_LOG);
    CreateTabCtrl(3, L"BUTTON", L"📋 複製日誌", BS_PUSHBUTTON, 155, 672, 100, 28, IDC_BTN_COPY_LOG);
    g_hChkAutoScroll = CreateTabCtrl(3, L"BUTTON", L"自動滾動", BS_AUTOCHECKBOX, 265, 674, 85, 24, IDC_CHK_AUTO_SCROLL);
    SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

    CreateTabCtrl(3, L"BUTTON", L"📄 開啟 dztrace.log", BS_PUSHBUTTON, 360, 672, 135, 28, IDC_BTN_OPEN_TRACE);
    CreateTabCtrl(3, L"BUTTON", L"📂 local_save.txt", BS_PUSHBUTTON, 505, 672, 135, 28, IDC_BTN_OPEN_SAVE);
    CreateTabCtrl(3, L"BUTTON", L"📝 save_panel.txt", BS_PUSHBUTTON, 650, 672, 125, 28, IDC_BTN_OPEN_PANEL);

    // Subclass Edit Controls for Enter key
    g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);
    g_oldEditValProc = (WNDPROC)SetWindowLongPtr(g_hEditVal, GWLP_WNDPROC, (LONG_PTR)EditValSubclassProc);

    // 預設顯示 Tab 0 (英雄與動作)
    SwitchTab(0);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    AppendLog(L"=== Warcraft III JassHook 開發者輔助控制台 v2.0 (Tab 增強版) 啟動完成 ===");
    AppendLog(L"[*] 系統已具備 SeDebugPrivilege 管理員除錯權限。");
    AppendLog(L"[*] 已載入 JassHookConfig.ini: 商城模式=" + std::to_wstring(savedMallMode) + L", 隨機百分比=" + std::to_wstring(savedPercent) + L"%, 地圖等級=" + std::to_wstring(savedLevel));
    AppendLog(L"[*] 已升級為 Tab 分頁架構: 【英雄與動作】、【戰鬥與地圖】、【平台與存檔】、【即時監控】。");
    AppendLog(L"[*] 記憶體瞬移與無敵已就緒: 瞬移至滑鼠 (Ctrl+M)、無敵神模式 (Ctrl+G)、滿血 (Ctrl+H)、秒殺 (Ctrl+K)。");
    AppendLog(L"[*] 戰鬥與地圖輔助已啟用: 遊戲倍速齒輪 (1x~10x)、全圖開霧、資源一鍵修改 (金幣/木材)。");
    AppendLog(L"[*] DzAPI 即時監控串流已啟用: 平台特權、商城偽造、伺服器存檔讀寫均即時串流於此。");

    // 啟動背景 Named Pipe 日誌串流監聽線程
    g_hPipeReaderThread = CreateThread(NULL, 0, PipeLogReaderThread, NULL, 0, NULL);

    SendMessage(g_hWnd, WM_TIMER, IDT_TIMER_STATUS, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
