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

#define IDT_TIMER_STATUS      1

// Global GUI Handles & States
HWND g_hWnd = NULL;
HWND g_hEditLog = NULL;
HWND g_hEditCmd = NULL;
HWND g_hEditVal = NULL;
HWND g_hEditPid = NULL;
HWND g_hEditKey = NULL;
HWND g_hEditLevel = NULL;
HWND g_hEditPercent = NULL;
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

bool SendPipeCommand(const std::string& cmd) {
    if (!ConnectPipe()) {
        return false;
    }
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipeCs);
        return false;
    }
    DWORD bw;
    BOOL ok = WriteFile(g_hPipe, cmd.c_str(), (DWORD)cmd.length(), &bw, NULL);
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
            if (SendPipeCommand("MALL:2")) {
                AppendLog(L"[商城] 已切換為模式 2: 全開模式 (所有非黑名單道具一律解鎖)");
            }
            break;

        case IDC_BTN_MALL_WHITE:
            if (SendPipeCommand("MALL:1")) {
                AppendLog(L"[商城] 已切換為模式 1: 白名單模式 (僅解鎖 mall_spoof.txt 中的道具)");
            }
            break;

        case IDC_BTN_MALL_OFF:
            if (SendPipeCommand("MALL:0")) {
                AppendLog(L"[商城] 已切換為模式 0: 關閉偽造 (恢復遊戲原始狀態)");
            }
            break;

        case IDC_BTN_VIP_ALL:
            if (SendPipeCommand("VIP:2")) {
                AppendLog(L"[VIP] 已啟用平台至尊VIP (VIP:2，特權/光環/翅膀/存檔全開)");
            }
            break;

        case IDC_BTN_ANTIOVER: {
            g_antiOverEnabled = !g_antiOverEnabled;
            std::string payload = g_antiOverEnabled ? "ANTIOVER:1" : "ANTIOVER:0";
            if (SendPipeCommand(payload)) {
                if (g_antiOverEnabled) {
                    AppendLog(L"[防護] 已開啟防失敗/防結算保護 (ANTIOVER:1)");
                    SetWindowTextW(g_hBtnAntiOver, L"🛡️ 防失敗保護 (目前: 開)");
                } else {
                    AppendLog(L"[防護] 已關閉防失敗/防結算保護 (ANTIOVER:0)");
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

        case IDC_BTN_SET_LEVEL: {
            char lvlBuf[32];
            GetWindowTextA(g_hEditLevel, lvlBuf, sizeof(lvlBuf));
            if (strlen(lvlBuf) > 0) {
                std::string payload = std::string("LEVEL:") + lvlBuf;
                if (SendPipeCommand(payload)) {
                    AppendLog(L"[等級] 已設定偽造地圖等級為: " + Utf8ToWide(lvlBuf));
                }
            }
            break;
        }

        case IDC_BTN_SET_PERCENT: {
            char pctBuf[32];
            GetWindowTextA(g_hEditPercent, pctBuf, sizeof(pctBuf));
            if (strlen(pctBuf) > 0) {
                std::string payload = std::string("PERCENT:") + pctBuf;
                SendPipeCommand(payload);
                SendPipeCommand("MALL:3");
                AppendLog(L"[商城] 已啟用模式 3 (隨機百分比)，機率設為: " + Utf8ToWide(pctBuf) + L"%");
            }
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
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
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
        L"Warcraft III JassHook 開發者控制台 v2.0 (GUI 整合增強版)",
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

    // ==========================================
    // 1. 遊戲偵測與注入管理 (Injection Manager)
    // ==========================================
    CreateCtrl(L"BUTTON", L"【遊戲進程與注入管理】", BS_GROUPBOX, 15, 10, 790, 95, IDC_GRP_INJECT);
    g_hLblWar3 = CreateCtrl(L"STATIC", L"遊戲進程: 偵測中...", SS_LEFT, 30, 32, 330, 20, IDC_LBL_WAR3_STATUS);
    g_hLblPipe = CreateCtrl(L"STATIC", L"IPC 連線: 尚未連線", SS_LEFT, 30, 56, 330, 20, IDC_LBL_PIPE_STATUS);
    CreateCtrl(L"STATIC", L"註: 本程式已具備管理員權限，可直接穿透 KK 平台防護", SS_LEFT, 30, 78, 380, 18, 0);

    g_hChkAutoInject = CreateCtrl(L"BUTTON", L"啟動時自動注入", BS_AUTOCHECKBOX, 380, 30, 140, 24, IDC_CHK_AUTO_INJECT);
    SendMessage(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

    CreateCtrl(L"BUTTON", L"⚡ 一鍵注入", BS_PUSHBUTTON, 530, 26, 120, 32, IDC_BTN_INJECT);
    CreateCtrl(L"BUTTON", L"🔄 重新連線", BS_PUSHBUTTON, 660, 26, 125, 32, IDC_BTN_RECONNECT);

    // ==========================================
    // 2. 存檔管理與批量回寫 (Save Manager)
    // ==========================================
    CreateCtrl(L"BUTTON", L"【存檔管理與批量回寫】", BS_GROUPBOX, 15, 115, 790, 105, IDC_GRP_SAVE);
    CreateCtrl(L"BUTTON", L"📝 產生並開啟存檔面板 (!panel)", BS_PUSHBUTTON, 30, 140, 230, 30, IDC_BTN_PANEL);
    CreateCtrl(L"BUTTON", L"💾 批量回寫存檔 (!saveall)", BS_PUSHBUTTON, 270, 140, 200, 30, IDC_BTN_SAVEALL);
    CreateCtrl(L"STATIC", L"(先點 !panel 在記事本修改數值並保存，再點 !saveall 回寫)", SS_LEFT, 480, 147, 310, 20, 0);

    CreateCtrl(L"STATIC", L"手動存檔:", SS_LEFT, 30, 183, 70, 20, IDC_LBL_MANUAL_SAVE);
    g_hEditPid = CreateCtrl(L"EDIT", L"1048602", WS_BORDER | ES_AUTOHSCROLL, 105, 180, 90, 24, IDC_EDIT_PID);
    g_hEditKey = CreateCtrl(L"EDIT", L"存檔Key (如: IWJCS)", WS_BORDER | ES_AUTOHSCROLL, 205, 180, 160, 24, IDC_EDIT_KEY);
    g_hEditVal = CreateCtrl(L"EDIT", L"新數值Value", WS_BORDER | ES_AUTOHSCROLL, 375, 180, 250, 24, IDC_EDIT_VAL);
    CreateCtrl(L"BUTTON", L"發送單筆存檔", BS_PUSHBUTTON, 635, 178, 150, 28, IDC_BTN_SAVESINGLE);

    // ==========================================
    // 3. 商城與平台特權 (Mall & Platform VIP)
    // ==========================================
    CreateCtrl(L"BUTTON", L"【商城與平台特權 (DzAPI 逆向增強)】", BS_GROUPBOX, 15, 230, 790, 90, IDC_GRP_MALL);
    CreateCtrl(L"BUTTON", L"🛒 商城全開 (!mall 2)", BS_PUSHBUTTON, 30, 253, 140, 28, IDC_BTN_MALL_ALL);
    CreateCtrl(L"BUTTON", L"📋 商城白名單 (!mall 1)", BS_PUSHBUTTON, 178, 253, 145, 28, IDC_BTN_MALL_WHITE);
    CreateCtrl(L"BUTTON", L"👑 平台至尊VIP (!vip 2)", BS_PUSHBUTTON, 330, 253, 150, 28, IDC_BTN_VIP_ALL);
    CreateCtrl(L"BUTTON", L"❌ 關閉商城 (!mall 0)", BS_PUSHBUTTON, 488, 253, 140, 28, IDC_BTN_MALL_OFF);
    g_hBtnAntiOver = CreateCtrl(L"BUTTON", L"🛡️ 防失敗保護 (目前: 開)", BS_PUSHBUTTON, 635, 253, 150, 28, IDC_BTN_ANTIOVER);

    CreateCtrl(L"STATIC", L"地圖等級:", SS_LEFT, 30, 290, 70, 20, IDC_LBL_LEVEL);
    g_hEditLevel = CreateCtrl(L"EDIT", L"99", WS_BORDER | ES_NUMBER, 105, 288, 60, 24, IDC_EDIT_LEVEL);
    CreateCtrl(L"BUTTON", L"設定等級", BS_PUSHBUTTON, 175, 287, 85, 26, IDC_BTN_SET_LEVEL);

    CreateCtrl(L"STATIC", L"商城隨機百分比 (%):", SS_LEFT, 285, 290, 140, 20, IDC_LBL_PERCENT);
    g_hEditPercent = CreateCtrl(L"EDIT", L"30", WS_BORDER | ES_NUMBER, 430, 288, 55, 24, IDC_EDIT_PERCENT);
    CreateCtrl(L"BUTTON", L"設定百分比 (模式3)", BS_PUSHBUTTON, 495, 287, 140, 26, IDC_BTN_SET_PERCENT);

    // ==========================================
    // 4. 英雄與選取單位增強 (瞬移 / 無敵 / 屬性)
    // ==========================================
    CreateCtrl(L"BUTTON", L"【英雄與選取單位增強 (支援遊戲內熱鍵 Ctrl+M / Ctrl+G / Ctrl+H / Ctrl+K)】", BS_GROUPBOX, 15, 328, 790, 106, IDC_GRP_HERO);
    // 第一行: 瞬移、無敵、滿血、秒殺
    CreateCtrl(L"BUTTON", L"🌀 瞬移至滑鼠 (Ctrl+M)", BS_PUSHBUTTON, 30, 355, 180, 30, IDC_BTN_TELEPORT);
    CreateCtrl(L"BUTTON", L"🛡️ 設為無敵 (Ctrl+G)", BS_PUSHBUTTON, 220, 355, 175, 30, IDC_BTN_GOD);
    CreateCtrl(L"BUTTON", L"❤️ 生命全滿 (Ctrl+H)", BS_PUSHBUTTON, 405, 355, 185, 30, IDC_BTN_HEAL);
    CreateCtrl(L"BUTTON", L"💀 秒殺目標 (Ctrl+K)", BS_PUSHBUTTON, 600, 355, 185, 30, IDC_BTN_KILL);
    // 第二行: 技能點、屬性、經驗
    CreateCtrl(L"BUTTON", L"🌟 技能點 +10", BS_PUSHBUTTON, 30, 393, 180, 28, IDC_BTN_ADD_SP10);
    CreateCtrl(L"BUTTON", L"🌟 技能點 +50", BS_PUSHBUTTON, 220, 393, 175, 28, IDC_BTN_ADD_SP50);
    CreateCtrl(L"BUTTON", L"💪 全屬性 +500", BS_PUSHBUTTON, 405, 393, 185, 28, IDC_BTN_ADD_STATS);
    CreateCtrl(L"BUTTON", L"📖 經驗 +10000", BS_PUSHBUTTON, 600, 393, 185, 28, IDC_BTN_ADD_XP);

    // ==========================================
    // 5. 自訂指令輸入 (Command Input)
    // ==========================================
    CreateCtrl(L"BUTTON", L"【自訂指令與 Native 呼叫】", BS_GROUPBOX, 15, 444, 790, 68, IDC_GRP_CMD);
    g_hEditCmd = CreateCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 30, 468, 630, 28, IDC_EDIT_CMD);
    CreateCtrl(L"BUTTON", L"發送指令 (Enter)", BS_PUSHBUTTON, 670, 467, 120, 30, IDC_BTN_SEND_CMD);

    // ==========================================
    // 6. 即時系統日誌與 JASS / DzAPI 即時監控串流
    // ==========================================
    CreateCtrl(L"BUTTON", L"【即時系統日誌與 JASS / DzAPI 監控串流】", BS_GROUPBOX, 15, 520, 790, 325, IDC_GRP_LOG);
    g_hEditLog = CreateCtrl(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                            30, 545, 760, 255, IDC_EDIT_LOG);

    CreateCtrl(L"BUTTON", L"🧹 清除日誌", BS_PUSHBUTTON, 30, 807, 100, 28, IDC_BTN_CLEAR_LOG);
    CreateCtrl(L"BUTTON", L"📋 複製日誌", BS_PUSHBUTTON, 138, 807, 100, 28, IDC_BTN_COPY_LOG);
    g_hChkAutoScroll = CreateCtrl(L"BUTTON", L"自動滾動", BS_AUTOCHECKBOX, 248, 809, 85, 24, IDC_CHK_AUTO_SCROLL);
    SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

    CreateCtrl(L"BUTTON", L"📄 dztrace.log", BS_PUSHBUTTON, 343, 807, 135, 28, IDC_BTN_OPEN_TRACE);
    CreateCtrl(L"BUTTON", L"📂 local_save.txt", BS_PUSHBUTTON, 488, 807, 145, 28, IDC_BTN_OPEN_SAVE);
    CreateCtrl(L"BUTTON", L"📝 save_panel.txt", BS_PUSHBUTTON, 643, 807, 147, 28, IDC_BTN_OPEN_PANEL);

    // Subclass Edit Controls for Enter key
    g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);
    g_oldEditValProc = (WNDPROC)SetWindowLongPtr(g_hEditVal, GWLP_WNDPROC, (LONG_PTR)EditValSubclassProc);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    AppendLog(L"=== Warcraft III JassHook 開發者輔助控制台 v2.0 (增強版) 啟動完成 ===");
    AppendLog(L"[*] 系統已具備 SeDebugPrivilege 管理員除錯權限。");
    AppendLog(L"[*] 記憶體瞬移與無敵已就緒: 瞬移至滑鼠 (Ctrl+M)、無敵神模式 (Ctrl+G)、滿血 (Ctrl+H)、秒殺 (Ctrl+K)。");
    AppendLog(L"[*] DzAPI 即時監控串流已啟用: 平台特權、商城偽造、伺服器存檔讀寫均即時串流於此。");
    AppendLog(L"[*] 支援指令: !tp, !god, !heal, !kill, !sp <點數>, !stats <屬性>, !xp <經驗>, !vip <0-2>, !panel, !saveall, !mall <0-3>");

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
