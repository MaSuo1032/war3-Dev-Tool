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
#define IDC_EDIT_QUICKSAVE        1028
#define IDC_BTN_QUICKSAVE         1029
#define IDC_BTN_PASTE_SAVE        1034

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

#define IDC_TAB_MONITOR           1050
#define IDC_LIST_SAVE_MONITOR     1051
#define IDC_BTN_COPY_SAVE_ITEM    1052
#define IDC_BTN_FILL_SAVE_INPUT   1053
#define IDC_BTN_CLEAR_SAVE_LIST   1054
#define IDC_BTN_RELOAD_SAVE       1055

#define IDT_TIMER_STATUS          1
#define WM_USER_UPDATE_SAVE_LIST  (WM_USER + 101)

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
HWND g_hEditQuickSave = NULL;
HWND g_hEditCmd = NULL;
HWND g_hEditLog = NULL;
HWND g_hChkAutoScroll = NULL;

HWND g_hTabMonitor = NULL;
HWND g_hListSaveMonitor = NULL;
HWND g_hBtnCopySaveItem = NULL;
HWND g_hBtnFillSaveInput = NULL;
HWND g_hBtnClearSaveList = NULL;
HWND g_hBtnReloadSave = NULL;
HWND g_hBtnClearLog = NULL;
HWND g_hBtnCopyLog = NULL;
HWND g_hBtnOpenTrace = NULL;

HFONT g_hFont = NULL;
HFONT g_hFontBold = NULL;
HFONT g_hFontConsole = NULL;
HBRUSH g_hLogBrush = NULL;

WNDPROC g_oldEditCmdProc = NULL;
WNDPROC g_oldEditPercentProc = NULL;
WNDPROC g_oldEditQuickSaveProc = NULL;

HANDLE g_hPipe = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_pipeCs;

DWORD g_lastDetectedPid = 0;
bool g_isDllInjected = false;
bool g_updatingSlider = false;
inline void ListView_GetItemTextW_Helper(HWND hwnd, int iItem, int iSubItem, LPWSTR pszText, int cchTextMax) {
    LVITEMW lvi{};
    lvi.iSubItem = iSubItem;
    lvi.cchTextMax = cchTextMax;
    lvi.pszText = pszText;
    SendMessageW(hwnd, LVM_GETITEMTEXTW, (WPARAM)iItem, (LPARAM)&lvi);
}

inline void ListView_SetItemTextW_Helper(HWND hwnd, int iItem, int iSubItem, LPCWSTR pszText) {
    LVITEMW lvi{};
    lvi.iSubItem = iSubItem;
    lvi.pszText = (LPWSTR)pszText;
    SendMessageW(hwnd, LVM_SETITEMTEXTW, (WPARAM)iItem, (LPARAM)&lvi);
}

inline int ListView_InsertItemW_Helper(HWND hwnd, const LVITEMW* pitem) {
    return (int)SendMessageW(hwnd, LVM_INSERTITEMW, 0, (LPARAM)pitem);
}

inline int ListView_InsertColumnW_Helper(HWND hwnd, int iCol, const LVCOLUMNW* pcol) {
    return (int)SendMessageW(hwnd, LVM_INSERTCOLUMNW, (WPARAM)iCol, (LPARAM)pcol);
}

inline int TabCtrl_InsertItemW_Helper(HWND hwnd, int iItem, const TCITEMW* pitem) {
    return (int)SendMessageW(hwnd, TCM_INSERTITEMW, (WPARAM)iItem, (LPARAM)pitem);
}

inline BOOL TabCtrl_SetItemW_Helper(HWND hwnd, int iItem, const TCITEMW* pitem) {
    return (BOOL)SendMessageW(hwnd, TCM_SETITEMW, (WPARAM)iItem, (LPARAM)pitem);
}

struct SaveRecord {
    std::wstring time;
    std::wstring action;
    std::wstring player;
    std::wstring key;
    std::wstring val;
};

struct SaveItemKey {
    std::wstring player;
    std::wstring key;
    bool operator<(const SaveItemKey& o) const {
        if (player != o.player) return player < o.player;
        return key < o.key;
    }
};

CRITICAL_SECTION g_saveMonitorCs;
std::vector<SaveRecord> g_pendingSaveRecords;
std::map<SaveItemKey, int> g_saveKeyToIndex;

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

// Named Pipe Communication (Inbound only to game, purely non-blocking)
bool ConnectPipe() {
    EnterCriticalSection(&g_pipeCs);
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipeCs);
        return true;
    }
    g_hPipe = CreateFileA(
        "\\\\.\\pipe\\War3JassDevTool",
        GENERIC_WRITE,
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

// Flexible parser supporting all known save formats:
// 1. 1048602:Key=Val
// 2. Key=Val (auto-assigns defPid)
// 3. !save 1048602 Key Val or !save Key Val
// 4. SAVE:1048602:Key:Val or SAVE:1048602:Key=Val
// 5. Player 1048602 SAVED/LOADED: Key = Val
// 6. [SaveServerValue] Player: 1048602 | Key: "Key" | Value: "Val"
// 7. Space-separated: 1048602 Key Val
bool ParseSaveLine(const std::string& rawLine, const std::string& defPid, std::string& outPid, std::string& outKey, std::string& outVal) {
    std::string line = rawLine;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
    if (line.empty() || line[0] == '#' || line[0] == ';') return false;

    // 1. Strip !save or !SAVE prefix
    if (line.rfind("!save ", 0) == 0 || line.rfind("!SAVE ", 0) == 0) {
        line = line.substr(6);
        while (!line.empty() && line.front() == ' ') line.erase(line.begin());
    }

    // 2. Strip SAVE: or save: prefix
    if (line.rfind("SAVE:", 0) == 0 || line.rfind("save:", 0) == 0) {
        line = line.substr(5);
    }

    // 3. Parse dztrace format: [SaveServerValue] / [GetServerValue] Player: 1048602 | Key: "xxx" | Value...: "yyy"
    if (line.find("Player:") != std::string::npos && line.find("Key:") != std::string::npos) {
        size_t pPos = line.find("Player:");
        size_t kPos = line.find("Key:");
        size_t vPos = line.find("Value");
        if (pPos != std::string::npos && kPos != std::string::npos && vPos != std::string::npos) {
            size_t pNumStart = line.find_first_of("0123456789", pPos);
            if (pNumStart != std::string::npos) {
                size_t pNumEnd = line.find_first_not_of("0123456789", pNumStart);
                outPid = line.substr(pNumStart, (pNumEnd == std::string::npos ? line.length() : pNumEnd) - pNumStart);
            }
            size_t q1 = line.find('"', kPos);
            size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos) {
                outKey = line.substr(q1 + 1, q2 - q1 - 1);
            }
            size_t q3 = line.find('"', vPos);
            size_t q4 = (q3 != std::string::npos) ? line.find('"', q3 + 1) : std::string::npos;
            if (q3 != std::string::npos && q4 != std::string::npos) {
                outVal = line.substr(q3 + 1, q4 - q3 - 1);
            }
            if (!outKey.empty() && !outVal.empty()) {
                if (outPid.empty()) outPid = defPid;
                return true;
            }
        }
    }

    // 4. Parse local_save format: Player 1048602 SAVED: Key = Val or LOADED: Key = Val
    if (line.find("Player ") == 0 && (line.find(" SAVED: ") != std::string::npos || line.find(" LOADED: ") != std::string::npos || line.find(" SET_STAT: ") != std::string::npos)) {
        size_t sp1 = 7;
        size_t sp2 = line.find(' ', sp1);
        if (sp2 != std::string::npos) {
            outPid = line.substr(sp1, sp2 - sp1);
            size_t colon = line.find(": ", sp2);
            size_t eq = line.find(" = ", colon);
            if (colon != std::string::npos && eq != std::string::npos) {
                outKey = line.substr(colon + 2, eq - (colon + 2));
                outVal = line.substr(eq + 3);
                return true;
            }
        }
    }

    // Helper trimmer
    auto trimStr = [](std::string& s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    };

    // 5. Standard Player:Key=Value (save_panel format)
    size_t colon = line.find(':');
    size_t eq = line.find('=');
    if (colon != std::string::npos && eq != std::string::npos && colon < eq) {
        outPid = line.substr(0, colon);
        outKey = line.substr(colon + 1, eq - colon - 1);
        outVal = line.substr(eq + 1);
        trimStr(outPid);
        trimStr(outKey);
        trimStr(outVal);
        if (!outKey.empty() && !outVal.empty()) {
            if (outPid.empty()) outPid = defPid;
            return true;
        }
    }

    // 5b. Colon only: Player:Key:Val
    if (colon != std::string::npos) {
        size_t colon2 = line.find(':', colon + 1);
        if (colon2 != std::string::npos) {
            outPid = line.substr(0, colon);
            outKey = line.substr(colon + 1, colon2 - colon - 1);
            outVal = line.substr(colon2 + 1);
            trimStr(outPid);
            trimStr(outKey);
            trimStr(outVal);
            if (!outKey.empty() && !outVal.empty()) {
                if (outPid.empty()) outPid = defPid;
                return true;
            }
        }
    }

    // 6. Key=Value (without Player ID)
    if (eq != std::string::npos) {
        outPid = defPid;
        outKey = line.substr(0, eq);
        outVal = line.substr(eq + 1);
        trimStr(outKey);
        trimStr(outVal);
        if (!outKey.empty() && !outVal.empty()) {
            return true;
        }
    }

    // 7. Space-separated tokens: Player Key Val (3 tokens) or Key Val (2 tokens)
    std::stringstream tss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (tss >> tok) tokens.push_back(tok);

    if (tokens.size() >= 3) {
        outPid = tokens[0];
        outKey = tokens[1];
        outVal = tokens[2];
        for (size_t i = 3; i < tokens.size(); i++) {
            outVal += " " + tokens[i];
        }
        return true;
    } else if (tokens.size() == 2) {
        outPid = defPid;
        outKey = tokens[0];
        outVal = tokens[1];
        return true;
    }

    return false;
}

bool g_runLogTailer = true;
HANDLE g_hLogTailerThread = NULL;

// Non-blocking log tailer from dztrace.log (Completely decoupled from the game process)
DWORD WINAPI LogTailerThread(LPVOID lpParam) {
    std::string exeDir = GetExeDir();
    std::string logPath = exeDir + "dztrace.log";
    std::streampos lastPos = 0;

    {
        std::ifstream initFile(logPath, std::ios::in | std::ios::binary);
        if (initFile.is_open()) {
            initFile.seekg(0, std::ios::end);
            lastPos = initFile.tellg();
            if (lastPos < 4096) {
                lastPos = 0;
            }
        }
    }

    while (g_runLogTailer) {
        std::ifstream file(logPath, std::ios::in | std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            std::streampos curSize = file.tellg();
            if (curSize < lastPos) {
                lastPos = 0;
            }
            if (curSize > lastPos) {
                file.seekg(lastPos);
                size_t bytesToRead = (size_t)(curSize - lastPos);
                std::string newChunk;
                newChunk.resize(bytesToRead);
                file.read(&newChunk[0], bytesToRead);
                lastPos = curSize;

                std::stringstream ss(newChunk);
                std::string line;
                while (std::getline(ss, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    if (!line.empty()) {
                        AppendLog(Utf8ToWide(line));

                        // Detect Save Monitor events
                        if (line.find("[GetServerValue]") != std::string::npos ||
                            line.find("[SaveServerValue]") != std::string::npos ||
                            line.find("[Stat_SetStat]") != std::string::npos ||
                            line.find("LOADED:") != std::string::npos ||
                            line.find("SAVED:") != std::string::npos) {

                            std::string defPid = "1048602";
                            if (g_hEditPid) {
                                char pBuf[64];
                                GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
                                if (strlen(pBuf) > 0) defPid = pBuf;
                            }
                            std::string pid, key, val;
                            if (ParseSaveLine(line, defPid, pid, key, val)) {
                                std::string actionStr = "存檔";
                                if (line.find("GetServerValue") != std::string::npos || line.find("LOADED:") != std::string::npos) {
                                    actionStr = "讀取 (GET)";
                                } else if (line.find("SaveServerValue") != std::string::npos || line.find("SAVED:") != std::string::npos) {
                                    actionStr = "寫入 (SAVE)";
                                } else if (line.find("Stat_SetStat") != std::string::npos) {
                                    actionStr = "統計 (STAT)";
                                }

                                auto now = std::chrono::system_clock::now();
                                auto in_time_t = std::chrono::system_clock::to_time_t(now);
                                struct tm tmbuf;
                                localtime_s(&tmbuf, &in_time_t);
                                wchar_t timeBuf[32];
                                wcsftime(timeBuf, sizeof(timeBuf) / sizeof(wchar_t), L"%H:%M:%S", &tmbuf);

                                SaveRecord rec;
                                rec.time = timeBuf;
                                rec.action = Utf8ToWide(actionStr);
                                rec.player = Utf8ToWide(pid);
                                rec.key = Utf8ToWide(key);
                                rec.val = Utf8ToWide(val);

                                EnterCriticalSection(&g_saveMonitorCs);
                                g_pendingSaveRecords.push_back(rec);
                                LeaveCriticalSection(&g_saveMonitorCs);

                                PostMessage(g_hWnd, WM_USER_UPDATE_SAVE_LIST, 0, 0);
                            }
                        }
                    }
                }
            }
            file.close();
        }
        Sleep(150);
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

// Execute Quick Save (Parses any save format and supports multiline batch)
void ExecuteQuickSave(const std::string& rawText) {
    if (rawText.empty()) {
        MessageBoxW(g_hWnd, L"請輸入存檔指令或貼上存檔格式內容！", L"提示", MB_ICONWARNING);
        return;
    }

    std::string defPid = "1048602";
    if (g_hEditPid) {
        char pBuf[64];
        GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
        if (strlen(pBuf) > 0) defPid = pBuf;
    }

    std::stringstream ss(rawText);
    std::string line;
    int successCount = 0;
    int failCount = 0;

    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        std::string pid, key, val;
        if (ParseSaveLine(line, defPid, pid, key, val)) {
            std::string payload = "SAVE:" + pid + ":" + key + ":" + val;
            if (SendPipeCommand(payload)) {
                successCount++;
                AppendLog(L"[存檔寫入] 發送成功: " + Utf8ToWide(pid) + L":" + Utf8ToWide(key) + L"=" + Utf8ToWide(val));

                // Also update Save Monitor immediately
                auto now = std::chrono::system_clock::now();
                auto in_time_t = std::chrono::system_clock::to_time_t(now);
                struct tm tmbuf;
                localtime_s(&tmbuf, &in_time_t);
                wchar_t timeBuf[32];
                wcsftime(timeBuf, sizeof(timeBuf) / sizeof(wchar_t), L"%H:%M:%S", &tmbuf);

                SaveRecord rec;
                rec.time = timeBuf;
                rec.action = L"手動寫入 (SET)";
                rec.player = Utf8ToWide(pid);
                rec.key = Utf8ToWide(key);
                rec.val = Utf8ToWide(val);

                EnterCriticalSection(&g_saveMonitorCs);
                g_pendingSaveRecords.push_back(rec);
                LeaveCriticalSection(&g_saveMonitorCs);
                PostMessage(g_hWnd, WM_USER_UPDATE_SAVE_LIST, 0, 0);

                Sleep(15);
            } else {
                failCount++;
                AppendLog(L"[-] 發送存檔失敗: " + Utf8ToWide(payload) + L" (魔獸尚未連線)");
            }
        } else {
            failCount++;
            AppendLog(L"[-] 無法解析存檔格式: " + Utf8ToWide(line));
        }
    }

    if (successCount > 0) {
        std::wstring msg = L"成功發送 " + std::to_wstring(successCount) + L" 筆存檔寫入指令！";
        if (failCount > 0) msg += L"\n(無法發送 " + std::to_wstring(failCount) + L" 筆)";
        MessageBoxW(g_hWnd, msg.c_str(), L"寫入結果", MB_ICONINFORMATION);
    } else if (failCount > 0) {
        MessageBoxW(g_hWnd, L"發送存檔指令失敗，請確認格式或確認魔獸是否已注入連線！", L"錯誤", MB_ICONERROR);
    }
}

// Paste directly from clipboard and execute save
void PasteAndExecuteQuickSave() {
    if (!OpenClipboard(g_hWnd)) {
        MessageBoxW(g_hWnd, L"無法開啟系統剪貼簿！", L"錯誤", MB_ICONERROR);
        return;
    }
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        MessageBoxW(g_hWnd, L"剪貼簿中無文字內容！", L"提示", MB_ICONINFORMATION);
        return;
    }
    wchar_t* pText = (wchar_t*)GlobalLock(hData);
    if (!pText) {
        CloseClipboard();
        return;
    }
    std::wstring wstr(pText);
    GlobalUnlock(hData);
    CloseClipboard();

    std::string str = WideToUtf8(wstr);
    if (g_hEditQuickSave) {
        SetWindowTextW(g_hEditQuickSave, wstr.c_str());
    }
    ExecuteQuickSave(str);
}

// Fill selected item from Save Monitor listview into the Quick Save input box
void FillSelectedSaveToInput() {
    if (!g_hListSaveMonitor) return;
    int sel = ListView_GetNextItem(g_hListSaveMonitor, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(g_hWnd, L"請先在存檔監控清單中選取任一筆項目！", L"提示", MB_ICONINFORMATION);
        return;
    }
    wchar_t pidBuf[64], keyBuf[256], valBuf[512];
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 2, pidBuf, sizeof(pidBuf) / sizeof(wchar_t));
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 3, keyBuf, sizeof(keyBuf) / sizeof(wchar_t));
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 4, valBuf, sizeof(valBuf) / sizeof(wchar_t));

    if (g_hEditPid && wcslen(pidBuf) > 0) {
        SetWindowTextW(g_hEditPid, pidBuf);
    }
    if (g_hEditKey) {
        SetWindowTextW(g_hEditKey, keyBuf);
    }
    if (g_hEditVal) {
        SetWindowTextW(g_hEditVal, valBuf);
    }

    std::wstring line = std::wstring(pidBuf) + L":" + keyBuf + L"=" + valBuf;
    if (g_hEditQuickSave) {
        SetWindowTextW(g_hEditQuickSave, line.c_str());
        SetFocus(g_hEditQuickSave);
        size_t eqPos = line.find(L'=');
        if (eqPos != std::wstring::npos) {
            SendMessage(g_hEditQuickSave, EM_SETSEL, (WPARAM)(eqPos + 1), (LPARAM)line.length());
        }
    }
    AppendLog(L"[+] 已自動填入存檔格式: " + line);
}

// Copy selected item from Save Monitor listview to clipboard in Player:Key=Val format
void CopySelectedSaveItem() {
    if (!g_hListSaveMonitor) return;
    int sel = ListView_GetNextItem(g_hListSaveMonitor, -1, LVNI_SELECTED);
    if (sel == -1) {
        MessageBoxW(g_hWnd, L"請先在存檔監控清單中選取要複製的項目！", L"提示", MB_ICONINFORMATION);
        return;
    }
    wchar_t pidBuf[64], keyBuf[256], valBuf[512];
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 2, pidBuf, sizeof(pidBuf) / sizeof(wchar_t));
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 3, keyBuf, sizeof(keyBuf) / sizeof(wchar_t));
    ListView_GetItemTextW_Helper(g_hListSaveMonitor, sel, 4, valBuf, sizeof(valBuf) / sizeof(wchar_t));

    std::wstring line = std::wstring(pidBuf) + L":" + keyBuf + L"=" + valBuf;
    if (OpenClipboard(g_hWnd)) {
        EmptyClipboard();
        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (line.length() + 1) * sizeof(wchar_t));
        if (hGlob) {
            memcpy(GlobalLock(hGlob), line.c_str(), (line.length() + 1) * sizeof(wchar_t));
            GlobalUnlock(hGlob);
            SetClipboardData(CF_UNICODETEXT, hGlob);
        }
        CloseClipboard();
        AppendLog(L"[+] 已複製存檔格式到剪貼簿: " + line);
    }
}

// Batch update Save Monitor ListView on GUI thread
void OnUpdateSaveList() {
    std::vector<SaveRecord> pending;
    EnterCriticalSection(&g_saveMonitorCs);
    pending.swap(g_pendingSaveRecords);
    LeaveCriticalSection(&g_saveMonitorCs);

    if (pending.empty() || !g_hListSaveMonitor) return;

    SendMessage(g_hListSaveMonitor, WM_SETREDRAW, FALSE, 0);

    for (const auto& rec : pending) {
        SaveItemKey sk{ rec.player, rec.key };
        auto it = g_saveKeyToIndex.find(sk);
        if (it != g_saveKeyToIndex.end()) {
            int idx = it->second;
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, idx, 0, rec.time.c_str());
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, idx, 1, rec.action.c_str());
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, idx, 4, rec.val.c_str());
        } else {
            int rowIdx = ListView_GetItemCount(g_hListSaveMonitor);
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = rowIdx;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)rec.time.c_str();
            ListView_InsertItemW_Helper(g_hListSaveMonitor, &lvi);

            ListView_SetItemTextW_Helper(g_hListSaveMonitor, rowIdx, 1, rec.action.c_str());
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, rowIdx, 2, rec.player.c_str());
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, rowIdx, 3, rec.key.c_str());
            ListView_SetItemTextW_Helper(g_hListSaveMonitor, rowIdx, 4, rec.val.c_str());

            g_saveKeyToIndex[sk] = rowIdx;
        }
    }

    SendMessage(g_hListSaveMonitor, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListSaveMonitor, NULL, TRUE);

    if (g_hTabMonitor) {
        TCITEMW tci{};
        tci.mask = TCIF_TEXT;
        std::wstring tabTitle = L"💾 存檔即時監控 (" + std::to_wstring(ListView_GetItemCount(g_hListSaveMonitor)) + L" 筆)";
        tci.pszText = (LPWSTR)tabTitle.c_str();
        TabCtrl_SetItemW_Helper(g_hTabMonitor, 0, &tci);
    }
}

// Load existing save variables from local_save.txt
void LoadExistingSavesFromFile() {
    std::string exeDir = GetExeDir();
    std::string saveFile = exeDir + "local_save.txt";
    std::ifstream infile(saveFile);
    if (!infile.is_open()) {
        infile.open("local_save.txt");
        if (!infile.is_open()) return;
    }

    std::string line;
    std::string defPid = "1048602";
    if (g_hEditPid) {
        char pBuf[64];
        GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
        if (strlen(pBuf) > 0) defPid = pBuf;
    }
    std::string detectedPid = "";

    std::vector<SaveRecord> recs;
    while (std::getline(infile, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;

        std::string pid, key, val;
        if (ParseSaveLine(line, defPid, pid, key, val)) {
            if (detectedPid.empty() && !pid.empty()) detectedPid = pid;

            std::string actionStr = "讀取 (GET)";
            if (line.find("SAVED:") != std::string::npos || line.find("SaveServerValue") != std::string::npos) {
                actionStr = "寫入 (SAVE)";
            } else if (line.find("SET_STAT:") != std::string::npos || line.find("Stat_SetStat") != std::string::npos) {
                actionStr = "統計 (STAT)";
            }

            SaveRecord rec;
            rec.time = L"--:--:--";
            rec.action = Utf8ToWide(actionStr);
            rec.player = Utf8ToWide(pid);
            rec.key = Utf8ToWide(key);
            rec.val = Utf8ToWide(val);
            recs.push_back(rec);
        }
    }
    infile.close();

    if (!detectedPid.empty() && g_hEditPid) {
        SetWindowTextA(g_hEditPid, detectedPid.c_str());
    }

    if (!recs.empty()) {
        EnterCriticalSection(&g_saveMonitorCs);
        for (const auto& r : recs) {
            g_pendingSaveRecords.push_back(r);
        }
        LeaveCriticalSection(&g_saveMonitorCs);
        PostMessage(g_hWnd, WM_USER_UPDATE_SAVE_LIST, 0, 0);
        AppendLog(L"[存檔監控] 已從 local_save.txt 載入 " + std::to_wstring(recs.size()) + L" 筆歷史存檔紀錄");
    }
}

// Toggle visibility between Save Monitor and All Logs tabs
void SwitchMonitorTab() {
    if (!g_hTabMonitor) return;
    int curTab = TabCtrl_GetCurSel(g_hTabMonitor);
    if (curTab == 0) {
        ShowWindow(g_hListSaveMonitor, SW_SHOW);
        ShowWindow(g_hBtnFillSaveInput, SW_SHOW);
        ShowWindow(g_hBtnCopySaveItem, SW_SHOW);
        ShowWindow(g_hBtnReloadSave, SW_SHOW);
        ShowWindow(g_hBtnClearSaveList, SW_SHOW);

        ShowWindow(g_hEditLog, SW_HIDE);
        ShowWindow(g_hChkAutoScroll, SW_HIDE);
        ShowWindow(g_hBtnClearLog, SW_HIDE);
        ShowWindow(g_hBtnCopyLog, SW_HIDE);
        ShowWindow(g_hBtnOpenTrace, SW_HIDE);
    } else {
        ShowWindow(g_hListSaveMonitor, SW_HIDE);
        ShowWindow(g_hBtnFillSaveInput, SW_HIDE);
        ShowWindow(g_hBtnCopySaveItem, SW_HIDE);
        ShowWindow(g_hBtnReloadSave, SW_HIDE);
        ShowWindow(g_hBtnClearSaveList, SW_HIDE);

        ShowWindow(g_hEditLog, SW_SHOW);
        ShowWindow(g_hChkAutoScroll, SW_SHOW);
        ShowWindow(g_hBtnClearLog, SW_SHOW);
        ShowWindow(g_hBtnCopyLog, SW_SHOW);
        ShowWindow(g_hBtnOpenTrace, SW_SHOW);
    }
}

// Send Single Save Command (Backward compatible or called by UI)
void SendSingleSave() {
    char pBuf[64], kBuf[256], vBuf[512];
    GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
    GetWindowTextA(g_hEditKey, kBuf, sizeof(kBuf));
    GetWindowTextA(g_hEditVal, vBuf, sizeof(vBuf));

    if (strlen(kBuf) > 0) {
        std::string raw = std::string(kBuf);
        if (strlen(vBuf) > 0) raw += "=" + std::string(vBuf);
        ExecuteQuickSave(raw);
        return;
    }

    if (g_hEditQuickSave) {
        char qBuf[2048];
        GetWindowTextA(g_hEditQuickSave, qBuf, sizeof(qBuf));
        if (strlen(qBuf) > 0) {
            ExecuteQuickSave(qBuf);
            return;
        }
    }

    AppendLog(L"[-] 手動存檔失敗: 請輸入存檔內容或完整填寫 Key 與 Value！");
    MessageBoxW(g_hWnd, L"請在存檔輸入框填寫或貼上存檔格式！\n例如: 1048602:ItemName=999 或 ItemName=999", L"提示", MB_ICONWARNING);
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
    else if (c.rfind("!save", 0) == 0 || c.rfind("SAVE:", 0) == 0 || c.find('=') != std::string::npos || (c.find(':') != std::string::npos && c.find(' ') == std::string::npos)) {
        ExecuteQuickSave(c);
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

// Subclass QuickSave EditBox for Enter key support
LRESULT CALLBACK EditQuickSaveSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_CHAR && wParam == VK_RETURN) {
        wchar_t buf[2048];
        GetWindowTextW(hWnd, buf, sizeof(buf) / sizeof(wchar_t));
        std::string cmd = WideToUtf8(buf);
        ExecuteQuickSave(cmd);
        return 0;
    }
    return CallWindowProc(g_oldEditQuickSaveProc, hWnd, uMsg, wParam, lParam);
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
        icex.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
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

        // 3. Group 3: 伺服器存檔管理與即時寫入 (Save Panel & Direct Write)
        HWND hGrp3 = CreateWindowW(L"BUTTON", L"【伺服器存檔管理與即時寫入 (支援直接複製貼上 Save 格式)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 225, 850, 120, hWnd, (HMENU)IDC_GRP_SAVE, NULL, NULL);
        SendMessage(hGrp3, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPanel = CreateWindowW(L"BUTTON", L"📝 產生並開啟存檔面板 (!panel)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 245, 255, 30, hWnd, (HMENU)IDC_BTN_PANEL, NULL, NULL);
        SendMessage(hBtnPanel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnSaveAll = CreateWindowW(L"BUTTON", L"⚡ 批量覆寫所有存檔 (!saveall)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            295, 245, 255, 30, hWnd, (HMENU)IDC_BTN_SAVEALL, NULL, NULL);
        SendMessage(hBtnSaveAll, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnOpenLocal = CreateWindowW(L"BUTTON", L"📂 開啟存檔日誌 (local_save.txt)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            560, 245, 290, 30, hWnd, (HMENU)IDC_BTN_OPEN_LOCALSAVE, NULL, NULL);
        SendMessage(hBtnOpenLocal, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblPid = CreateWindowW(L"STATIC", L"玩家ID:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 287, 50, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblPid, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditPid = CreateWindowW(L"EDIT", L"1048602", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            82, 284, 75, 25, hWnd, (HMENU)IDC_EDIT_PID, NULL, NULL);
        SendMessage(g_hEditPid, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblQuick = CreateWindowW(L"STATIC", L"快速存檔:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            165, 287, 65, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblQuick, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditQuickSave = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            232, 284, 380, 25, hWnd, (HMENU)IDC_EDIT_QUICKSAVE, NULL, NULL);
        SendMessage(g_hEditQuickSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        g_oldEditQuickSaveProc = (WNDPROC)SetWindowLongPtr(g_hEditQuickSave, GWLP_WNDPROC, (LONG_PTR)EditQuickSaveSubclassProc);

        HWND hBtnQuickSave = CreateWindowW(L"BUTTON", L"💾 寫入存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620, 283, 105, 27, hWnd, (HMENU)IDC_BTN_QUICKSAVE, NULL, NULL);
        SendMessage(hBtnQuickSave, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPasteSave = CreateWindowW(L"BUTTON", L"📋 貼上並寫入", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            735, 283, 115, 27, hWnd, (HMENU)IDC_BTN_PASTE_SAVE, NULL, NULL);
        SendMessage(hBtnPasteSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hLblHint = CreateWindowW(L"STATIC", L"💡 存檔格式支援: 玩家ID:Key=Value (例 1048602:HeroExp=9999)、Key=Value 或 !save 指令，支援多行批量貼上與 Enter 鍵發送", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 316, 820, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLblHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 4. Group 4: 原版 JassTool 快捷與終端指令
        HWND hGrp4 = CreateWindowW(L"BUTTON", L"【原版 JassTool 快捷與指令互動終端】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 350, 850, 60, hWnd, (HMENU)IDC_GRP_CMD, NULL, NULL);
        SendMessage(hGrp4, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnKill = CreateWindowW(L"BUTTON", L"💀 秒殺選中單位 (!kill)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 370, 175, 30, hWnd, (HMENU)IDC_BTN_KILL, NULL, NULL);
        SendMessage(hBtnKill, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hLblCmd = CreateWindowW(L"STATIC", L"Jass 指令:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            215, 374, 70, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditCmd = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            290, 371, 430, 26, hWnd, (HMENU)IDC_EDIT_CMD, NULL, NULL);
        SendMessage(g_hEditCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);

        HWND hBtnSend = CreateWindowW(L"BUTTON", L"發送指令", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            730, 369, 120, 30, hWnd, (HMENU)IDC_BTN_SEND_CMD, NULL, NULL);
        SendMessage(hBtnSend, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 5. Group 5: 即時監控中心 (Live Monitor Center)
        HWND hGrp5 = CreateWindowW(L"BUTTON", L"【即時監控中心 (Live Monitor)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 415, 850, 300, hWnd, (HMENU)IDC_GRP_LOG, NULL, NULL);
        SendMessage(hGrp5, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Tab Control for Monitor
        g_hTabMonitor = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            30, 435, 340, 28, hWnd, (HMENU)IDC_TAB_MONITOR, NULL, NULL);
        SendMessage(g_hTabMonitor, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        TCITEMW tci0{};
        tci0.mask = TCIF_TEXT;
        tci0.pszText = (LPWSTR)L"💾 存檔即時監控 (0 筆)";
        TabCtrl_InsertItemW_Helper(g_hTabMonitor, 0, &tci0);
        TCITEMW tci1{};
        tci1.mask = TCIF_TEXT;
        tci1.pszText = (LPWSTR)L"📜 全部串流日誌";
        TabCtrl_InsertItemW_Helper(g_hTabMonitor, 1, &tci1);

        // Save Monitor Toolbar Buttons (Tab 0)
        g_hBtnFillSaveInput = CreateWindowW(L"BUTTON", L"✏️ 填入輸入框", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            380, 435, 120, 26, hWnd, (HMENU)IDC_BTN_FILL_SAVE_INPUT, NULL, NULL);
        SendMessage(g_hBtnFillSaveInput, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnCopySaveItem = CreateWindowW(L"BUTTON", L"📋 複製格式 (P:K=V)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            508, 435, 145, 26, hWnd, (HMENU)IDC_BTN_COPY_SAVE_ITEM, NULL, NULL);
        SendMessage(g_hBtnCopySaveItem, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnReloadSave = CreateWindowW(L"BUTTON", L"🔄 重新讀取存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            660, 435, 115, 26, hWnd, (HMENU)IDC_BTN_RELOAD_SAVE, NULL, NULL);
        SendMessage(g_hBtnReloadSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnClearSaveList = CreateWindowW(L"BUTTON", L"清空清單", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            782, 435, 68, 26, hWnd, (HMENU)IDC_BTN_CLEAR_SAVE_LIST, NULL, NULL);
        SendMessage(g_hBtnClearSaveList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // General Log Toolbar Buttons (Tab 1, initially hidden)
        g_hChkAutoScroll = CreateWindowW(L"BUTTON", L"自動滾動", WS_CHILD | BS_AUTOCHECKBOX,
            420, 438, 90, 20, hWnd, (HMENU)IDC_CHK_AUTO_SCROLL, NULL, NULL);
        SendMessage(g_hChkAutoScroll, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

        g_hBtnClearLog = CreateWindowW(L"BUTTON", L"清空日誌", WS_CHILD | BS_PUSHBUTTON,
            520, 435, 95, 26, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, NULL, NULL);
        SendMessage(g_hBtnClearLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnCopyLog = CreateWindowW(L"BUTTON", L"複製日誌", WS_CHILD | BS_PUSHBUTTON,
            625, 435, 95, 26, hWnd, (HMENU)IDC_BTN_COPY_LOG, NULL, NULL);
        SendMessage(g_hBtnCopyLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnOpenTrace = CreateWindowW(L"BUTTON", L"開啟 dztrace.log", WS_CHILD | BS_PUSHBUTTON,
            730, 435, 120, 26, hWnd, (HMENU)IDC_BTN_OPEN_TRACE, NULL, NULL);
        SendMessage(g_hBtnOpenTrace, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Save Monitor ListView (Tab 0)
        g_hListSaveMonitor = CreateWindowW(WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_VSCROLL,
            30, 466, 820, 238, hWnd, (HMENU)IDC_LIST_SAVE_MONITOR, NULL, NULL);
        SendMessage(g_hListSaveMonitor, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        ListView_SetExtendedListViewStyle(g_hListSaveMonitor, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        
        lvc.iSubItem = 0; lvc.pszText = (LPWSTR)L"時間"; lvc.cx = 75;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 0, &lvc);

        lvc.iSubItem = 1; lvc.pszText = (LPWSTR)L"操作"; lvc.cx = 90;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 1, &lvc);

        lvc.iSubItem = 2; lvc.pszText = (LPWSTR)L"玩家 ID"; lvc.cx = 80;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 2, &lvc);

        lvc.iSubItem = 3; lvc.pszText = (LPWSTR)L"存檔變數 (Key)"; lvc.cx = 250;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 3, &lvc);

        lvc.iSubItem = 4; lvc.pszText = (LPWSTR)L"數值 (Value)"; lvc.cx = 305;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 4, &lvc);

        // All Logs EditBox (Tab 1, initially hidden)
        g_hEditLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            30, 466, 820, 238, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessage(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);

        // Ensure Tab 0 visibility is active
        SwitchMonitorTab();

        // Load existing saves from local_save.txt if available
        LoadExistingSavesFromFile();

        // Status Check Timer (Every 1000ms)
        SetTimer(hWnd, IDT_TIMER_STATUS, 1000, NULL);
        UpdateStatus();

        AppendLog(L"=== 魔獸爭霸 III JASS 開發者整合控制面板 v2.1 初始化完成 ===");
        AppendLog(L"[*] 支援一鍵注入、即時存檔監控、直接複製貼上 Save 格式、原版 JassTool 終端與商城比例黑名單");
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

        case IDC_BTN_QUICKSAVE: {
            wchar_t buf[2048];
            GetWindowTextW(g_hEditQuickSave, buf, sizeof(buf) / sizeof(wchar_t));
            std::string text = WideToUtf8(buf);
            ExecuteQuickSave(text);
            break;
        }

        case IDC_BTN_PASTE_SAVE:
            PasteAndExecuteQuickSave();
            break;

        case IDC_BTN_FILL_SAVE_INPUT:
            FillSelectedSaveToInput();
            break;

        case IDC_BTN_COPY_SAVE_ITEM:
            CopySelectedSaveItem();
            break;

        case IDC_BTN_RELOAD_SAVE:
            LoadExistingSavesFromFile();
            break;

        case IDC_BTN_CLEAR_SAVE_LIST:
            if (g_hListSaveMonitor) {
                ListView_DeleteAllItems(g_hListSaveMonitor);
                g_saveKeyToIndex.clear();
                if (g_hTabMonitor) {
                    TCITEMW tci{};
                    tci.mask = TCIF_TEXT;
                    tci.pszText = (LPWSTR)L"💾 存檔即時監控 (0 筆)";
                    TabCtrl_SetItemW_Helper(g_hTabMonitor, 0, &tci);
                }
                AppendLog(L"[+] 已清空存檔監控清單。");
            }
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

    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == IDC_LIST_SAVE_MONITOR) {
            if (pnmh->code == NM_DBLCLK) {
                FillSelectedSaveToInput();
            }
        } else if (pnmh->idFrom == IDC_TAB_MONITOR) {
            if (pnmh->code == TCN_SELCHANGE) {
                SwitchMonitorTab();
            }
        }
        break;
    }

    case WM_USER_UPDATE_SAVE_LIST: {
        OnUpdateSaveList();
        break;
    }

    case WM_DESTROY: {
        KillTimer(hWnd, IDT_TIMER_STATUS);
        g_runLogTailer = false;
        if (g_hLogTailerThread) {
            WaitForSingleObject(g_hLogTailerThread, 500);
            CloseHandle(g_hLogTailerThread);
            g_hLogTailerThread = NULL;
        }
        DisconnectPipe();
        if (g_hLogBrush) DeleteObject(g_hLogBrush);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontConsole) DeleteObject(g_hFontConsole);
        DeleteCriticalSection(&g_pipeCs);
        DeleteCriticalSection(&g_saveMonitorCs);
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
    InitializeCriticalSection(&g_saveMonitorCs);
    
    // Create non-blocking Log Tailer Thread
    g_hLogTailerThread = CreateThread(NULL, 0, LogTailerThread, NULL, 0, NULL);

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

    RECT rc = { 0, 0, 880, 750 };
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
