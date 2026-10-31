#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <chrono>
#include <iomanip>

#include "GuiDefs.h"
#include "SaveParser.h"
#include "SaveCommand.h"
#include "SaveDiff.h"
#include "ProcessInjector.h"
#include "PipeClient.h"
#include "MapKeyExtractor.h"
#include "WebControllerServer.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

HWND g_hTabMain = NULL;
HWND g_hTabMonitor = NULL;
HWND g_hListSaveMonitor = NULL;
HWND g_hBtnCopySaveItem = NULL;
HWND g_hBtnFillSaveInput = NULL;
HWND g_hBtnClearSaveList = NULL;
HWND g_hBtnReloadSave = NULL;
HWND g_hBtnClearLog = NULL;
HWND g_hBtnCopyLog = NULL;
HWND g_hBtnOpenTrace = NULL;

// Quick Memory Action Buttons (Header Bar)
HWND g_hBtnMemHeal = NULL;
HWND g_hBtnMemGod = NULL;
HWND g_hBtnMemSpeed = NULL;
HWND g_hBtnMemStats = NULL;
HWND g_hBtnMemGold = NULL;
HWND g_hBtnMemLumber = NULL;
HWND g_hBtnOpenWebUi = NULL;

// Tab Controls Collections
std::vector<HWND> g_tab0Controls;
std::vector<HWND> g_tab1Controls;
std::vector<HWND> g_tab2Controls;
std::vector<HWND> g_tab3Controls;
std::vector<HWND> g_memControls;

HWND g_hLblUnitHeader = NULL;
HWND g_hBtnMemScan = NULL;
HWND g_hBtnMemScanRes = NULL;
HWND g_hBtnWebUiInTab = NULL;

HWND g_hEditUnitHp = NULL;
HWND g_hEditUnitMaxHp = NULL;
HWND g_hBtnSetUnitHp = NULL;
HWND g_hBtnHealInTab = NULL;

HWND g_hEditUnitMp = NULL;
HWND g_hEditUnitMaxMp = NULL;
HWND g_hBtnSetUnitMp = NULL;
HWND g_hBtnGodInTab = NULL;

HWND g_hEditUnitStr = NULL;
HWND g_hBtnAddUnitStr = NULL;
HWND g_hBtnSetUnitStr = NULL;

HWND g_hEditUnitAgi = NULL;
HWND g_hBtnAddUnitAgi = NULL;
HWND g_hBtnSetUnitAgi = NULL;

HWND g_hEditUnitInt = NULL;
HWND g_hBtnAddUnitInt = NULL;
HWND g_hBtnSetUnitInt = NULL;

HWND g_hEditUnitSpeed = NULL;
HWND g_hBtnMaxUnitSpeed = NULL;
HWND g_hBtnSetUnitSpeed = NULL;

HWND g_hEditUnitDef = NULL;
HWND g_hBtnSetUnitDef = NULL;

HWND g_hEditUnitExp = NULL;
HWND g_hBtnAddUnitExp = NULL;
HWND g_hBtnSetUnitExp = NULL;
HWND g_hBtnAllStats = NULL;
HWND g_hBtnMemBlink = NULL;

HWND g_hCmbResPlayer = NULL;
HWND g_hEditResGold = NULL;
HWND g_hBtnAddResGold = NULL;
HWND g_hBtnSetResGold = NULL;
HWND g_hEditResLumber = NULL;
HWND g_hBtnAddResLumber = NULL;
HWND g_hBtnSetResLumber = NULL;

HWND g_hLblItemSlot[6] = { NULL };
HWND g_hEditItemSlot[6] = { NULL };
HWND g_hBtnSetItemSlot[6] = { NULL };

HFONT g_hFont = NULL;
HFONT g_hFontBold = NULL;
HFONT g_hFontConsole = NULL;
HBRUSH g_hLogBrush = NULL;

WNDPROC g_oldEditCmdProc = NULL;
WNDPROC g_oldEditPercentProc = NULL;
WNDPROC g_oldEditQuickSaveProc = NULL;

DWORD g_lastDetectedPid = 0;
bool g_isDllInjected = false;
bool g_updatingSlider = false;

CRITICAL_SECTION g_saveMonitorCs;
std::vector<SaveRecord> g_pendingSaveRecords;
std::map<SaveItemKey, int> g_saveKeyToIndex;

bool g_runLogTailer = true;
HANDLE g_hLogTailerThread = NULL;
bool g_isBatchReading = false;
HANDLE g_hBatchReadThread = NULL;

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

    std::wstring exeDir = GetExeDirWide();
    std::wstring dllPath = exeDir + L"JassHook.dll";
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        dllPath = exeDir + L"bin\\JassHook.dll";
        if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            AppendLog(L"[-] 注入失敗: 找不到 JassHook.dll 檔案！");
            MessageBoxW(g_hWnd, L"找不到 JassHook.dll 檔案！請確認放在同一目錄或 bin 目錄下。", L"錯誤", MB_ICONERROR);
            return;
        }
    }

    AppendLog(L"[*] 正在注入 JassHook.dll 至進程 PID: " + std::to_wstring(pid) + L" ...");
    std::wstring err;
    if (InjectDLL(pid, dllPath, err)) {
        AppendLog(L"[+] JassHook.dll 已載入；核心初始化結果請見即時日誌。");
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
                    TrimSaveString(line);
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
        std::string p, k, v;
        if (ParseSaveLine(line, "1048602", p, k, v) && ValidateSaveCommand(p,k,v,true)) {
            if (k == v && !isdigit((unsigned char)k[0])) {
                continue;
            }
            uniqueKeys[{p, k}] = v;
            if (detectedPid.empty()) detectedPid = p;
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
        TrimSaveString(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        std::string p, k, v;
        if (ParseSaveLine(line, "1048602", p, k, v)) {
            std::string payload = "SAVE:" + p + ":" + k + ":" + v;
            if (SendPipeCommand(payload)) {
                count++;
                Sleep(20);
            }
        }
    }
    pfile.close();
    AppendLog(L"[存檔] 已送出 " + std::to_wstring(count) + L" 筆請求；請在日誌檢查 Native result，這不代表平台已持久化。");
    MessageBoxW(g_hWnd, (L"已送出 " + std::to_wstring(count) + L" 筆請求。請查看日誌的 Native result；平台持久化仍需重新載入確認。").c_str(), L"送出完成", MB_ICONINFORMATION);
}

// Background thread for querying all map keys from server
DWORD WINAPI BatchReadWorkerThread(LPVOID lpParam) {
    g_isBatchReading = true;
    std::string exeDir = GetExeDir();
    std::string keyPath = exeDir + "map_keys.txt";
    std::vector<std::string> keys;
    std::string sourceDesc = "map_keys.txt";

    // 1. Try map_keys.txt
    if (!LoadMapKeysFile(keyPath, keys)) {
        keyPath = "map_keys.txt";
        LoadMapKeysFile(keyPath, keys);
    }

    // 2. Fallback to discovered_keys.txt (automatically captured history)
    if (keys.empty()) {
        std::string discPath = exeDir + "discovered_keys.txt";
        if (LoadMapKeysFile(discPath, keys)) {
            sourceDesc = "discovered_keys.txt (動態學習庫)";
        } else {
            discPath = "discovered_keys.txt";
            if (LoadMapKeysFile(discPath, keys)) {
                sourceDesc = "discovered_keys.txt (動態學習庫)";
            }
        }
    }

    // 3. Fallback to live keys already in Save Monitor table
    if (keys.empty() && g_hListSaveMonitor) {
        int count = ListView_GetItemCount(g_hListSaveMonitor);
        std::set<std::string> uniqueKeys;
        for (int i = 0; i < count; ++i) {
            wchar_t kBuf[256];
            ListView_GetItemTextW_Helper(g_hListSaveMonitor, i, 3, kBuf, 256);
            std::string k = WideToUtf8(kBuf);
            if (!k.empty() && k != "(null)") uniqueKeys.insert(k);
        }
        for (const auto& k : uniqueKeys) keys.push_back(k);
        if (!keys.empty()) sourceDesc = "即時監控列表已捕捉項目";
    }

    if (!ConnectPipe()) {
        AppendLog(L"[-] 無法連線至魔獸爭霸 IPC 管道，請先啟動遊戲進入地圖並注入 JassHook.dll！");
        MessageBoxW(g_hWnd, L"未連線至魔獸爭霸！\n請先進入地圖並確認 JassHook.dll 已注入。", L"錯誤", MB_ICONERROR);
        g_isBatchReading = false;
        return 0;
    }

    std::string defPid = "1048602";
    if (g_hEditPid) {
        char pBuf[64];
        GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
        if (strlen(pBuf) > 0) defPid = pBuf;
    }

    // 4. If still empty, delegate to DLL's in-memory dynamic discovery via READ_ALL
    if (keys.empty()) {
        AppendLog(L"[*] 本地無靜態名單，已委託 JassHook 核心調用進程中動態學習到的全部變數進行遍歷讀取 (READ_ALL)...");
        SendPipeCommand("READ_ALL:" + defPid);
        MessageBoxW(g_hWnd, L"已透過核心動態學習庫發送遍歷讀取指令！\n\n數值將於數秒內陸續在「存檔即時監控」列表中呈現。\n(提示：只要進入魔獸地圖，系統會自動捕捉當前地圖的所有存檔變數，無須手動提供 map.j)", L"動態遍歷讀取進行中", MB_ICONINFORMATION);
        g_isBatchReading = false;
        return 0;
    }

    AppendLog(L"[*] 開始遍歷插件快取 [來源: " + Utf8ToWide(sourceDesc) + L"] (玩家 handle: " + Utf8ToWide(defPid) + L", 共 " + std::to_wstring(keys.size()) + L" 筆 Key)...");

    int count = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!g_isBatchReading) break;
        const std::string& k = keys[i];
        std::string cmd = "GET:" + defPid + ":" + k;
        if (ValidateSaveCommand(defPid,k,"",false) && SendPipeCommand(cmd)) {
            count++;
        }
        Sleep(15);
    }

    AppendLog(L"[讀取] 已送出 " + std::to_wstring(count) + L" 筆插件快取讀取請求。空值／缺少的 Key 不一定代表平台無資料。");
    MessageBoxW(g_hWnd, (L"已送出 " + std::to_wstring(count) + L" 筆插件快取讀取請求。\n[來源: " + Utf8ToWide(sourceDesc) + L"]\n數值會顯示於監控列表；若要查平台最新值，請在遊戲內重新載入資料。").c_str(), L"讀取請求完成", MB_ICONINFORMATION);
    g_isBatchReading = false;
    return 0;
}

void StartBatchRead() {
    if (g_isBatchReading) {
        MessageBoxW(g_hWnd, L"遍歷讀取正在進行中，請稍候...", L"提示", MB_ICONINFORMATION);
        return;
    }
    if (g_hBatchReadThread) {
        CloseHandle(g_hBatchReadThread);
        g_hBatchReadThread = NULL;
    }
    g_hBatchReadThread = CreateThread(NULL, 0, BatchReadWorkerThread, NULL, 0, NULL);
}

void ExtractKeysFromDialog() {
    wchar_t szFile[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"魔獸地圖與腳本檔 (*.j;*.w3x;*.txt)\0*.j;*.w3x;*.txt\0所有檔案 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"請選擇要提取存檔變數的 war3map.j 或地圖腳本";

    if (GetOpenFileNameW(&ofn)) {
        std::vector<std::string> keys;
        if (ExtractKeysFromFile(szFile, keys)) {
            std::string exeDir = GetExeDir();
            std::string outPath = exeDir + "map_keys.txt";
            std::ofstream outfile(outPath);
            outfile << "# ========================================================\n";
            outfile << "# [War3 RPG 存檔變數清單 - map_keys.txt]\n";
            outfile << "# 從 " << WideToUtf8(szFile) << " 自動提取\n";
            outfile << "# ========================================================\n\n";
            for (const auto& k : keys) {
                outfile << k << "\n";
            }
            outfile.close();

            AppendLog(L"[+] 成功從檔案提取 " + std::to_wstring(keys.size()) + L" 筆存檔變數，已儲存至 map_keys.txt！");
            MessageBoxW(g_hWnd, (L"成功提取 " + std::to_wstring(keys.size()) + L" 筆存檔變數！\n已更新至 map_keys.txt，現在可點擊「🔍 一鍵遍歷讀取存檔」。").c_str(), L"提取成功", MB_ICONINFORMATION);
        } else {
            AppendLog(L"[-] 未能從所選檔案中識別出任何 DzAPI 存檔變數。");
            MessageBoxW(g_hWnd, L"未能從檔案中識別出存檔變數！\n請確認選取的是包含 JASS 腳本的 war3map.j 或文字檔。", L"提示", MB_ICONWARNING);
        }
    }
}

// Execute Quick Save (Parses all save formats, updates UI fields, and sends via Pipe)
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
    std::string lastParsedPid, lastParsedKey, lastParsedVal;

    while (std::getline(ss, line)) {
        TrimSaveString(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        std::string pid, key, val;
        if (ParseSaveLine(line, defPid, pid, key, val) && ValidateSaveCommand(pid,key,val,true)) {
            lastParsedPid = pid;
            lastParsedKey = key;
            lastParsedVal = val;

            std::string payload = "SAVE:" + pid + ":" + key + ":" + val;
            if (SendPipeCommand(payload)) {
                successCount++;
                AppendLog(L"[存檔寫入] 已送出，待核心回報: " + Utf8ToWide(pid) + L":" + Utf8ToWide(key));

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

    // Auto-update the PID, Key, Val text boxes if single entry was executed
    if (successCount == 1 && !lastParsedKey.empty()) {
        if (g_hEditPid && !lastParsedPid.empty()) SetWindowTextA(g_hEditPid, lastParsedPid.c_str());
        if (g_hEditKey) SetWindowTextA(g_hEditKey, lastParsedKey.c_str());
        if (g_hEditVal) SetWindowTextA(g_hEditVal, lastParsedVal.c_str());
    }

    if (successCount > 0) {
        std::wstring msg = L"已送出 " + std::to_wstring(successCount) + L" 筆寫入請求；請查看日誌 Native result，並重新載入確認平台值。";
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
        TrimSaveString(line);
        if (line.empty()) continue;

        std::string pid, key, val;
        if (ParseSaveLine(line, defPid, pid, key, val)) {
            if (key == val && !isdigit((unsigned char)key[0])) {
                continue;
            }
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

// Snapshots capture the displayed last-observed state; they never write to the game.
std::wstring SnapshotCell(int row, int column) {
    for (int capacity = 256; capacity <= 1048576; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        LVITEMW item{}; item.iSubItem = column; item.pszText = buffer.data(); item.cchTextMax = capacity;
        int length = (int)SendMessageW(g_hListSaveMonitor, LVM_GETITEMTEXTW, row, (LPARAM)&item);
        if (length < capacity - 1) return std::wstring(buffer.data(), length);
    }
    return L"";
}
void SaveSnapshotOrCompare(bool compare) {
    OnUpdateSaveList();
    SaveDiff::Snapshot current;
    for (int i=0; i<ListView_GetItemCount(g_hListSaveMonitor); ++i) {
        auto player=WideToUtf8(SnapshotCell(i,2)), key=WideToUtf8(SnapshotCell(i,3));
        if (!player.empty() && !key.empty()) current[{player,key}]=WideToUtf8(SnapshotCell(i,4));
    }
    if (current.empty()) {
        MessageBoxW(g_hWnd,L"目前沒有存檔觀測資料。請先讀取存檔並等待列表更新。",L"存檔快照",MB_ICONINFORMATION); return;
    }
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=g_hWnd;
    dialog.lpstrFilter=L"存檔快照 (*.w3snapshot)\0*.w3snapshot\0所有檔案\0*.*\0";
    dialog.lpstrFile=path; dialog.nMaxFile=32768; dialog.lpstrDefExt=L"w3snapshot";
    dialog.lpstrTitle=compare ? L"選擇操作前的快照" : L"儲存目前存檔快照";
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(compare ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    if (!(compare ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog))) return;
    if (!compare) {
        std::ofstream out(path,std::ios::binary);
        SaveDiff::Write(out,current); out.flush();
        if (!out) { MessageBoxW(g_hWnd,L"無法寫入快照檔案。",L"錯誤",MB_ICONERROR); return; }
        AppendLog(L"[快照] 已保存 " + std::to_wstring(current.size()) + L" 筆：" + path); return;
    }
    std::ifstream in(path,std::ios::binary); SaveDiff::Snapshot before;
    if (!SaveDiff::Read(in,before)) { MessageBoxW(g_hWnd,L"快照格式不正確或檔案損毀。",L"錯誤",MB_ICONERROR); return; }
    std::wstring report=std::wstring(path)+L".diff-"+std::to_wstring(GetTickCount64())+L".html";
    std::ofstream out(report,std::ios::binary); out << SaveDiff::Report(before,current); out.flush();
    if (!out) { MessageBoxW(g_hWnd,L"無法寫入比較報告。",L"錯誤",MB_ICONERROR); return; }
    out.close();
    AppendLog(L"[快照] 差異報告："+report);
    ShellExecuteW(g_hWnd,L"open",report.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}

// Refresh selected unit attributes & player resources in Tab 1
void RefreshMemoryTab(bool force = false) {
    if (!g_hWnd) return;
    WebController::InitMemoryController();
    struct MemoryReadGuard {
        MemoryReadGuard() { EnterCriticalSection(&WebController::g_memCs); }
        ~MemoryReadGuard() { LeaveCriticalSection(&WebController::g_memCs); }
    } memoryReadGuard;
    HWND hFocus = GetFocus();

    // 1. Refresh Selected Unit
    UnitInfo info{};
    bool unitOk = WebController::ReadSelectedUnit(&info);
    if (unitOk) {
        wchar_t hdrBuf[256];
        swprintf_s(hdrBuf, 256, L"選中單位: 0x%08llX | 英雄屬性: 0x%08llX", 
            (unsigned long long)info.unit, (unsigned long long)info.heroAttributes);
        if (g_hLblUnitHeader) SetWindowTextW(g_hLblUnitHeader, hdrBuf);

        float hp = 0.0f, maxHp = 0.0f, mp = 0.0f, maxMp = 0.0f, speed = 0.0f, def = 0.0f;
        int32_t str = 0, agi = 0, intel = 0, exp = 0;

        if (info.hp.valid) WebController::g_memInstance.read(info.hp.address, &hp);
        if (info.maxHp.valid) WebController::g_memInstance.read(info.maxHp.address, &maxHp);
        if (info.mp.valid) WebController::g_memInstance.read(info.mp.address, &mp);
        if (info.maxMp.valid) WebController::g_memInstance.read(info.maxMp.address, &maxMp);
        if (info.strength.valid) WebController::g_memInstance.read(info.strength.address, &str);
        if (info.agility.valid) WebController::g_memInstance.read(info.agility.address, &agi);
        if (info.intelligence.valid) WebController::g_memInstance.read(info.intelligence.address, &intel);
        if (info.moveSpeed.valid) WebController::g_memInstance.read(info.moveSpeed.address, &speed);
        if (info.defense.valid) WebController::g_memInstance.read(info.defense.address, &def);
        if (info.experience.valid) WebController::g_memInstance.read(info.experience.address, &exp);

        auto setEdit = [&](HWND hEdit, const std::wstring& val) {
            if (hEdit && (force || hFocus != hEdit)) {
                SetWindowTextW(hEdit, val.c_str());
            }
        };

        wchar_t numBuf[64];
        swprintf_s(numBuf, 64, L"%.0f", hp); setEdit(g_hEditUnitHp, numBuf);
        swprintf_s(numBuf, 64, L"%.0f", maxHp); setEdit(g_hEditUnitMaxHp, numBuf);
        swprintf_s(numBuf, 64, L"%.0f", mp); setEdit(g_hEditUnitMp, numBuf);
        swprintf_s(numBuf, 64, L"%.0f", maxMp); setEdit(g_hEditUnitMaxMp, numBuf);
        swprintf_s(numBuf, 64, L"%d", str); setEdit(g_hEditUnitStr, numBuf);
        swprintf_s(numBuf, 64, L"%d", agi); setEdit(g_hEditUnitAgi, numBuf);
        swprintf_s(numBuf, 64, L"%d", intel); setEdit(g_hEditUnitInt, numBuf);
        swprintf_s(numBuf, 64, L"%.1f", speed); setEdit(g_hEditUnitSpeed, numBuf);
        swprintf_s(numBuf, 64, L"%.1f", def); setEdit(g_hEditUnitDef, numBuf);
        swprintf_s(numBuf, 64, L"%d", exp); setEdit(g_hEditUnitExp, numBuf);

        // Inventory Items
        for (int i = 0; i < 6; ++i) {
            bool foundItem = false;
            for (const auto& it : info.items) {
                if (it.slot == i) {
                    foundItem = true;
                    int32_t chg = 0;
                    WebController::g_memInstance.read(it.chargesAddress, &chg);
                    wchar_t slotLbl[64];
                    swprintf_s(slotLbl, 64, L"格%d(0x%X):", i + 1, (unsigned int)(it.currentItem & 0xFFFF));
                    if (g_hLblItemSlot[i]) SetWindowTextW(g_hLblItemSlot[i], slotLbl);
                    swprintf_s(numBuf, 64, L"%d", chg);
                    setEdit(g_hEditItemSlot[i], numBuf);
                    break;
                }
            }
            if (!foundItem) {
                wchar_t slotLbl[64];
                swprintf_s(slotLbl, 64, L"格%d [空]:", i + 1);
                if (g_hLblItemSlot[i]) SetWindowTextW(g_hLblItemSlot[i], slotLbl);
                setEdit(g_hEditItemSlot[i], L"0");
            }
        }
    } else {
        if (g_hLblUnitHeader) SetWindowTextW(g_hLblUnitHeader, L"未選取單位 (請在魔獸遊戲中點選英雄或單位)");
    }

    // 2. Refresh Player Resources
    int curPlayer = 1;
    if (g_hCmbResPlayer) {
        int sel = (int)SendMessage(g_hCmbResPlayer, CB_GETCURSEL, 0, 0);
        if (sel != CB_ERR) curPlayer = sel + 1;
    }

    int gold = 0, lumber = 0;
    if (WebController::ReadPlayerResources(curPlayer, &gold, &lumber)) {
        wchar_t rBuf[64];
        if (g_hEditResGold && (force || hFocus != g_hEditResGold)) {
            swprintf_s(rBuf, 64, L"%d", gold);
            SetWindowTextW(g_hEditResGold, rBuf);
        }
        if (g_hEditResLumber && (force || hFocus != g_hEditResLumber)) {
            swprintf_s(rBuf, 64, L"%d", lumber);
            SetWindowTextW(g_hEditResLumber, rBuf);
        }
    }
}

// Toggle visibility between Tab 0 (Save), Tab 1 (Memory), Tab 2 (Mall), and Tab 3 (Logs)
void SwitchMainTab() {
    HWND hTab = g_hTabMain ? g_hTabMain : g_hTabMonitor;
    if (!hTab) return;
    int curTab = TabCtrl_GetCurSel(hTab);

    for (HWND h : g_tab0Controls) ShowWindow(h, (curTab == 0) ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab1Controls) ShowWindow(h, (curTab == 1) ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab2Controls) ShowWindow(h, (curTab == 2) ? SW_SHOW : SW_HIDE);
    for (HWND h : g_tab3Controls) ShowWindow(h, (curTab == 3) ? SW_SHOW : SW_HIDE);

    if (curTab == 1) {
        RefreshMemoryTab(true);
    }
}

void SwitchMonitorTab() {
    SwitchMainTab();
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

// Process Command Input (Supports !commands and raw save formats)
void ProcessUserCommand(const std::string& cmd) {
    if (cmd.empty()) return;
    
    std::string c = cmd;
    TrimSaveString(c);
    if (c.empty()) return;

    AppendLog(L"> " + Utf8ToWide(c));

    std::string defPid = "1048602";
    if (g_hEditPid) {
        char pBuf[64];
        GetWindowTextA(g_hEditPid, pBuf, sizeof(pBuf));
        if (strlen(pBuf) > 0) defPid = pBuf;
    }

    std::string testPid, testKey, testVal;

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
    else if (c == "!readall" || c == "!getall") {
        StartBatchRead();
    }
    else if (c.rfind("!get ", 0) == 0 || c.rfind("!read ", 0) == 0) {
        std::string k = c.substr(c.find(' ') + 1);
        TrimSaveString(k);
        SendPipeCommand("GET:" + defPid + ":" + k);
        AppendLog(L"[+] 已發送單筆查詢請求 (GET): " + Utf8ToWide(k));
    }
    else if (c.rfind("!save", 0) == 0 || c.rfind("SAVE:", 0) == 0 ||
             (c.find("Player") != std::string::npos && c.find("Key") != std::string::npos) ||
             c.find('=') != std::string::npos ||
             ParseSaveLine(c, defPid, testPid, testKey, testVal)) {
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
    static DWORD pipePid = 0;
    if (pid != pipePid) {
        DisconnectPipe();
        pipePid = pid;
    }
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
        SetWindowTextW(g_hLblPipe, L"IPC 管道: ● 已連線 (指令傳送就緒)");
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

        auto addTab0 = [&](HWND h) {
            if (h) {
                SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_tab0Controls.push_back(h);
            }
            return h;
        };
        auto addTab1 = [&](HWND h) {
            if (h) {
                SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_tab1Controls.push_back(h);
                g_memControls.push_back(h);
            }
            return h;
        };
        auto addTab2 = [&](HWND h) {
            if (h) {
                SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_tab2Controls.push_back(h);
            }
            return h;
        };
        auto addTab3 = [&](HWND h) {
            if (h) {
                SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_tab3Controls.push_back(h);
            }
            return h;
        };

        // -------------------------------------------------------------
        // Global Header: 系統狀態 & 核心快捷控制 (常駐於頂部)
        // -------------------------------------------------------------
        HWND hGrpHeader = CreateWindowW(L"BUTTON", L"【魔獸爭霸 III 系統狀態 & 核心快捷控制】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 8, 900, 96, hWnd, (HMENU)IDC_GRP_INJECT, NULL, NULL);
        SendMessage(hGrpHeader, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Status Row (y: 28)
        g_hLblWar3 = CreateWindowW(L"STATIC", L"魔獸進程: ○ 檢測中...", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 28, 215, 20, hWnd, (HMENU)IDC_LBL_WAR3_STATUS, NULL, NULL);
        SendMessage(g_hLblWar3, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hLblHook = CreateWindowW(L"STATIC", L"核心 DLL: ○ 未注入", WS_CHILD | WS_VISIBLE | SS_LEFT,
            250, 28, 210, 20, hWnd, (HMENU)IDC_LBL_HOOK_STATUS, NULL, NULL);
        SendMessage(g_hLblHook, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hLblPipe = CreateWindowW(L"STATIC", L"IPC 管道: ○ 未連線", WS_CHILD | WS_VISIBLE | SS_LEFT,
            465, 28, 210, 20, hWnd, (HMENU)IDC_LBL_PIPE_STATUS, NULL, NULL);
        SendMessage(g_hLblPipe, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hChkAutoInject = CreateWindowW(L"BUTTON", L"遊戲啟動時自動注入", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            685, 26, 215, 24, hWnd, (HMENU)IDC_CHK_AUTO_INJECT, NULL, NULL);
        SendMessage(g_hChkAutoInject, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

        // Quick Cheats Row (y: 58, h: 34)
        HWND hBtnInject = CreateWindowW(L"BUTTON", L"🚀 一鍵注入", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            28, 58, 100, 34, hWnd, (HMENU)IDC_BTN_INJECT, NULL, NULL);
        SendMessage(hBtnInject, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnReconnect = CreateWindowW(L"BUTTON", L"🔄 重連 IPC", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            134, 58, 95, 34, hWnd, (HMENU)IDC_BTN_RECONNECT, NULL, NULL);
        SendMessage(hBtnReconnect, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnKill = CreateWindowW(L"BUTTON", L"💀 秒殺", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            235, 58, 85, 34, hWnd, (HMENU)IDC_BTN_KILL, NULL, NULL);
        SendMessage(hBtnKill, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemHeal = CreateWindowW(L"BUTTON", L"💖 滿血滿魔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            326, 58, 98, 34, hWnd, (HMENU)IDC_BTN_MEM_HEAL, NULL, NULL);
        SendMessage(g_hBtnMemHeal, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemGod = CreateWindowW(L"BUTTON", L"🛡️ 無敵鎖血: 關", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            430, 58, 125, 34, hWnd, (HMENU)IDC_BTN_MEM_GODMODE, NULL, NULL);
        SendMessage(g_hBtnMemGod, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemSpeed = CreateWindowW(L"BUTTON", L"⚡ 522滿速", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            561, 58, 95, 34, hWnd, (HMENU)IDC_BTN_MEM_SPEED, NULL, NULL);
        SendMessage(g_hBtnMemSpeed, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemGold = CreateWindowW(L"BUTTON", L"💰 金錢+10W", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            662, 58, 105, 34, hWnd, (HMENU)IDC_BTN_MEM_GOLD, NULL, NULL);
        SendMessage(g_hBtnMemGold, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnOpenWebUi = CreateWindowW(L"BUTTON", L"🌐 Web 控制台", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            773, 58, 130, 34, hWnd, (HMENU)IDC_BTN_OPEN_WEB_UI, NULL, NULL);
        SendMessage(g_hBtnOpenWebUi, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // -------------------------------------------------------------
        // Main Tab Control (4 大核心分頁)
        // -------------------------------------------------------------
        g_hTabMain = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            15, 112, 900, 645, hWnd, (HMENU)IDC_TAB_MAIN, NULL, NULL);
        SendMessage(g_hTabMain, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        g_hTabMonitor = g_hTabMain;

        TCITEMW tci0{};
        tci0.mask = TCIF_TEXT;
        tci0.pszText = (LPWSTR)L"💾 存檔修改與即時監控 (0 筆)";
        TabCtrl_InsertItemW_Helper(g_hTabMain, 0, &tci0);

        TCITEMW tci1{};
        tci1.mask = TCIF_TEXT;
        tci1.pszText = (LPWSTR)L"⚔️ 英雄與資源記憶體控制";
        TabCtrl_InsertItemW_Helper(g_hTabMain, 1, &tci1);

        TCITEMW tci2{};
        tci2.mask = TCIF_TEXT;
        tci2.pszText = (LPWSTR)L"🛒 商城偽造與名單設定";
        TabCtrl_InsertItemW_Helper(g_hTabMain, 2, &tci2);

        TCITEMW tci3{};
        tci3.mask = TCIF_TEXT;
        tci3.pszText = (LPWSTR)L"💻 JASS 終端與即時日誌";
        TabCtrl_InsertItemW_Helper(g_hTabMain, 3, &tci3);

        // -------------------------------------------------------------
        // Tab 0: 存檔修改與即時監控 (Save Manager & Live Monitor)
        // -------------------------------------------------------------
        HWND hBtnBatchRead = addTab0(CreateWindowW(L"BUTTON", L"🔍 一鍵遍歷讀取存檔 (!readall)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            28, 148, 220, 32, hWnd, (HMENU)IDC_BTN_BATCH_READ, NULL, NULL));
        SendMessage(hBtnBatchRead, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPanel = addTab0(CreateWindowW(L"BUTTON", L"📝 產生並開啟面板", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            254, 148, 150, 32, hWnd, (HMENU)IDC_BTN_PANEL, NULL, NULL));
        SendMessage(hBtnPanel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnSaveAll = addTab0(CreateWindowW(L"BUTTON", L"⚡ 批量覆寫存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            410, 148, 135, 32, hWnd, (HMENU)IDC_BTN_SAVEALL, NULL, NULL));
        SendMessage(hBtnSaveAll, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnEditKeys = addTab0(CreateWindowW(L"BUTTON", L"📜 編輯變數名單", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            551, 148, 120, 32, hWnd, (HMENU)IDC_BTN_EDIT_MAP_KEYS, NULL, NULL));

        HWND hBtnExtractKeys = addTab0(CreateWindowW(L"BUTTON", L"📂 提取Key", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            677, 148, 105, 32, hWnd, (HMENU)IDC_BTN_EXTRACT_KEYS, NULL, NULL));

        HWND hBtnOpenLocal = addTab0(CreateWindowW(L"BUTTON", L"📄 存檔日誌", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            788, 148, 114, 32, hWnd, (HMENU)IDC_BTN_OPEN_LOCALSAVE, NULL, NULL));

        // Quick Save Row
        addTab0(CreateWindowW(L"STATIC", L"玩家 ID:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            28, 192, 58, 20, hWnd, NULL, NULL, NULL));

        g_hEditPid = addTab0(CreateWindowW(L"EDIT", L"1048602", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            88, 189, 75, 25, hWnd, (HMENU)IDC_EDIT_PID, NULL, NULL));

        addTab0(CreateWindowW(L"STATIC", L"快速存檔:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            168, 192, 65, 20, hWnd, NULL, NULL, NULL));

        g_hEditQuickSave = addTab0(CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            236, 189, 320, 25, hWnd, (HMENU)IDC_EDIT_QUICKSAVE, NULL, NULL));
        g_oldEditQuickSaveProc = (WNDPROC)SetWindowLongPtr(g_hEditQuickSave, GWLP_WNDPROC, (LONG_PTR)EditQuickSaveSubclassProc);

        HWND hBtnQuickSave = addTab0(CreateWindowW(L"BUTTON", L"💾 寫入存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            562, 188, 100, 27, hWnd, (HMENU)IDC_BTN_QUICKSAVE, NULL, NULL));
        SendMessage(hBtnQuickSave, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPasteSave = addTab0(CreateWindowW(L"BUTTON", L"📋 貼上並寫入", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            668, 188, 110, 27, hWnd, (HMENU)IDC_BTN_PASTE_SAVE, NULL, NULL));
        SendMessage(hBtnPasteSave, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnFillSaveInput = addTab0(CreateWindowW(L"BUTTON", L"✏️ 填入所選", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            784, 188, 118, 27, hWnd, (HMENU)IDC_BTN_FILL_SAVE_INPUT, NULL, NULL));

        // Save Monitor Table Toolbar & Title
        HWND hLblSaveTableTitle = addTab0(CreateWindowW(L"STATIC", L"【即時存檔變數監控列表】(自動捕捉遊戲讀取與寫入，雙擊項目可自動填入上方輸入框)", WS_CHILD | WS_VISIBLE | SS_LEFT,
            28, 226, 615, 20, hWnd, NULL, NULL, NULL));
        SendMessage(hLblSaveTableTitle, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnCopySaveItem = addTab0(CreateWindowW(L"BUTTON", L"📋 複製格式", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            648, 222, 100, 26, hWnd, (HMENU)IDC_BTN_COPY_SAVE_ITEM, NULL, NULL));

        g_hBtnReloadSave = addTab0(CreateWindowW(L"BUTTON", L"🔄 重新讀取", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            754, 222, 90, 26, hWnd, (HMENU)IDC_BTN_RELOAD_SAVE, NULL, NULL));

        g_hBtnClearSaveList = addTab0(CreateWindowW(L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            850, 222, 52, 26, hWnd, (HMENU)IDC_BTN_CLEAR_SAVE_LIST, NULL, NULL));

        addTab0(CreateWindowW(L"BUTTON", L"儲存快照（操作前）", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            28, 252, 180, 28, hWnd, (HMENU)1130, NULL, NULL));
        addTab0(CreateWindowW(L"BUTTON", L"與快照比較（操作後）", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            216, 252, 195, 28, hWnd, (HMENU)1131, NULL, NULL));
        addTab0(CreateWindowW(L"STATIC", L"先等待存檔讀取完成，再儲存／比較最後觀測值", WS_CHILD | WS_VISIBLE,
            425, 258, 470, 22, hWnd, NULL, NULL, NULL));

        // Save Monitor ListView
        g_hListSaveMonitor = addTab0(CreateWindowW(WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_VSCROLL,
            28, 286, 874, 431, hWnd, (HMENU)IDC_LIST_SAVE_MONITOR, NULL, NULL));
        ListView_SetExtendedListViewStyle(g_hListSaveMonitor, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW lvc{};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        lvc.iSubItem = 0; lvc.pszText = (LPWSTR)L"時間"; lvc.cx = 75;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 0, &lvc);
        lvc.iSubItem = 1; lvc.pszText = (LPWSTR)L"操作"; lvc.cx = 95;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 1, &lvc);
        lvc.iSubItem = 2; lvc.pszText = (LPWSTR)L"玩家 ID"; lvc.cx = 85;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 2, &lvc);
        lvc.iSubItem = 3; lvc.pszText = (LPWSTR)L"存檔變數 (Key)"; lvc.cx = 280;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 3, &lvc);
        lvc.iSubItem = 4; lvc.pszText = (LPWSTR)L"數值 (Value)"; lvc.cx = 325;
        ListView_InsertColumnW_Helper(g_hListSaveMonitor, 4, &lvc);

        addTab0(CreateWindowW(L"STATIC", L"💡 存檔提示: 點擊「一鍵遍歷讀取存檔」向伺服器查詢全部變數；可直接複製遊戲日誌貼上，支援所有格式！", WS_CHILD | WS_VISIBLE | SS_LEFT,
            28, 724, 870, 20, hWnd, NULL, NULL, NULL));

        // -------------------------------------------------------------
        // Tab 1: 英雄與資源記憶體控制 (Hero & Resource Memory Controller)
        // -------------------------------------------------------------
        g_hLblUnitHeader = addTab1(CreateWindowW(L"STATIC", L"未選取單位 (請在魔獸遊戲中點選英雄或單位)", WS_CHILD | SS_LEFT,
            30, 153, 440, 22, hWnd, NULL, NULL, NULL));
        SendMessage(g_hLblUnitHeader, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemScan = addTab1(CreateWindowW(L"BUTTON", L"🔍 重新掃描單位", WS_CHILD | BS_PUSHBUTTON,
            475, 148, 125, 30, hWnd, (HMENU)IDC_BTN_MEM_SCAN, NULL, NULL));

        g_hBtnMemScanRes = addTab1(CreateWindowW(L"BUTTON", L"💰 重新掃描資源", WS_CHILD | BS_PUSHBUTTON,
            606, 148, 125, 30, hWnd, (HMENU)IDC_BTN_MEM_SCAN_RES, NULL, NULL));

        g_hBtnWebUiInTab = addTab1(CreateWindowW(L"BUTTON", L"🌐 開啟 Web 控制面板", WS_CHILD | BS_PUSHBUTTON,
            737, 148, 165, 30, hWnd, (HMENU)IDC_BTN_OPEN_WEB_UI, NULL, NULL));
        SendMessage(g_hBtnWebUiInTab, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Left Panel: Hero Attributes & Stats Groupbox
        HWND hGrpLeft = addTab1(CreateWindowW(L"BUTTON", L"【英雄屬性與狀態修改】", WS_CHILD | BS_GROUPBOX,
            28, 188, 436, 525, hWnd, NULL, NULL, NULL));
        SendMessage(hGrpLeft, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // HP
        addTab1(CreateWindowW(L"STATIC", L"生命值 (HP):", WS_CHILD | SS_LEFT, 42, 220, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitHp = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 218, 65, 24, hWnd, (HMENU)IDC_EDIT_UNIT_HP, NULL, NULL));
        addTab1(CreateWindowW(L"STATIC", L"/", WS_CHILD | SS_CENTER, 203, 220, 10, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMaxHp = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 215, 218, 65, 24, hWnd, (HMENU)IDC_EDIT_UNIT_MAXHP, NULL, NULL));
        g_hBtnSetUnitHp = addTab1(CreateWindowW(L"BUTTON", L"設HP", WS_CHILD | BS_PUSHBUTTON, 288, 217, 75, 26, hWnd, (HMENU)IDC_BTN_SET_HP, NULL, NULL));
        g_hBtnHealInTab = addTab1(CreateWindowW(L"BUTTON", L"滿血滿魔", WS_CHILD | BS_PUSHBUTTON, 368, 217, 85, 26, hWnd, (HMENU)IDC_BTN_MEM_HEAL, NULL, NULL));

        // MP
        addTab1(CreateWindowW(L"STATIC", L"魔法值 (MP):", WS_CHILD | SS_LEFT, 42, 256, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMp = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 254, 65, 24, hWnd, (HMENU)IDC_EDIT_UNIT_MP, NULL, NULL));
        addTab1(CreateWindowW(L"STATIC", L"/", WS_CHILD | SS_CENTER, 203, 256, 10, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMaxMp = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 215, 254, 65, 24, hWnd, (HMENU)IDC_EDIT_UNIT_MAXMP, NULL, NULL));
        g_hBtnSetUnitMp = addTab1(CreateWindowW(L"BUTTON", L"設MP", WS_CHILD | BS_PUSHBUTTON, 288, 253, 75, 26, hWnd, (HMENU)IDC_BTN_SET_MP, NULL, NULL));
        g_hBtnGodInTab = addTab1(CreateWindowW(L"BUTTON", L"無敵鎖血", WS_CHILD | BS_PUSHBUTTON, 368, 253, 85, 26, hWnd, (HMENU)IDC_BTN_MEM_GODMODE, NULL, NULL));

        // Str
        addTab1(CreateWindowW(L"STATIC", L"英雄力量:", WS_CHILD | SS_LEFT, 42, 294, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitStr = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 292, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_STR, NULL, NULL));
        g_hBtnAddUnitStr = addTab1(CreateWindowW(L"BUTTON", L"+1000 力量", WS_CHILD | BS_PUSHBUTTON, 248, 291, 105, 26, hWnd, (HMENU)IDC_BTN_ADD_UNIT_STR, NULL, NULL));
        g_hBtnSetUnitStr = addTab1(CreateWindowW(L"BUTTON", L"設定力量", WS_CHILD | BS_PUSHBUTTON, 360, 291, 93, 26, hWnd, (HMENU)IDC_BTN_SET_STR, NULL, NULL));

        // Agi
        addTab1(CreateWindowW(L"STATIC", L"英雄敏捷:", WS_CHILD | SS_LEFT, 42, 332, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitAgi = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 330, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_AGI, NULL, NULL));
        g_hBtnAddUnitAgi = addTab1(CreateWindowW(L"BUTTON", L"+1000 敏捷", WS_CHILD | BS_PUSHBUTTON, 248, 329, 105, 26, hWnd, (HMENU)IDC_BTN_ADD_UNIT_AGI, NULL, NULL));
        g_hBtnSetUnitAgi = addTab1(CreateWindowW(L"BUTTON", L"設定敏捷", WS_CHILD | BS_PUSHBUTTON, 360, 329, 93, 26, hWnd, (HMENU)IDC_BTN_SET_AGI, NULL, NULL));

        // Int
        addTab1(CreateWindowW(L"STATIC", L"英雄智力:", WS_CHILD | SS_LEFT, 42, 370, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitInt = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 368, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_INT, NULL, NULL));
        g_hBtnAddUnitInt = addTab1(CreateWindowW(L"BUTTON", L"+1000 智力", WS_CHILD | BS_PUSHBUTTON, 248, 367, 105, 26, hWnd, (HMENU)IDC_BTN_ADD_UNIT_INT, NULL, NULL));
        g_hBtnSetUnitInt = addTab1(CreateWindowW(L"BUTTON", L"設定智力", WS_CHILD | BS_PUSHBUTTON, 360, 367, 93, 26, hWnd, (HMENU)IDC_BTN_SET_INT, NULL, NULL));

        // Speed
        addTab1(CreateWindowW(L"STATIC", L"移動速度:", WS_CHILD | SS_LEFT, 42, 408, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitSpeed = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER, 135, 406, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_SPEED, NULL, NULL));
        g_hBtnMaxUnitSpeed = addTab1(CreateWindowW(L"BUTTON", L"⚡ 522滿速", WS_CHILD | BS_PUSHBUTTON, 248, 405, 105, 26, hWnd, (HMENU)IDC_BTN_MEM_SPEED, NULL, NULL));
        g_hBtnSetUnitSpeed = addTab1(CreateWindowW(L"BUTTON", L"設定移速", WS_CHILD | BS_PUSHBUTTON, 360, 405, 93, 26, hWnd, (HMENU)IDC_BTN_SET_SPEED, NULL, NULL));

        // Defense
        addTab1(CreateWindowW(L"STATIC", L"單位防禦:", WS_CHILD | SS_LEFT, 42, 446, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitDef = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER, 135, 444, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_DEF, NULL, NULL));
        g_hBtnSetUnitDef = addTab1(CreateWindowW(L"BUTTON", L"設定防禦", WS_CHILD | BS_PUSHBUTTON, 248, 443, 205, 26, hWnd, (HMENU)IDC_BTN_SET_DEF, NULL, NULL));

        // Experience
        addTab1(CreateWindowW(L"STATIC", L"英雄經驗:", WS_CHILD | SS_LEFT, 42, 484, 80, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitExp = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 135, 482, 105, 24, hWnd, (HMENU)IDC_EDIT_UNIT_EXP, NULL, NULL));
        g_hBtnAddUnitExp = addTab1(CreateWindowW(L"BUTTON", L"+10,000 經驗", WS_CHILD | BS_PUSHBUTTON, 248, 481, 105, 26, hWnd, (HMENU)IDC_BTN_ADD_UNIT_EXP, NULL, NULL));
        g_hBtnSetUnitExp = addTab1(CreateWindowW(L"BUTTON", L"設定經驗", WS_CHILD | BS_PUSHBUTTON, 360, 481, 93, 26, hWnd, (HMENU)IDC_BTN_SET_EXP, NULL, NULL));

        // All Stats Button
        g_hBtnAllStats = addTab1(CreateWindowW(L"BUTTON", L"💪 三維全屬性同時增加 +1000 (力量/敏捷/智力)", WS_CHILD | BS_PUSHBUTTON,
            42, 518, 411, 32, hWnd, (HMENU)IDC_BTN_MEM_STATS, NULL, NULL));
        SendMessage(g_hBtnAllStats, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Blink Button
        g_hBtnMemBlink = addTab1(CreateWindowW(L"BUTTON", L"⚡ 準備閃現（遊戲內 Ctrl+B）", WS_CHILD | BS_PUSHBUTTON,
            42, 557, 411, 32, hWnd, (HMENU)IDC_BTN_MEM_BLINK, NULL, NULL));
        SendMessage(g_hBtnMemBlink, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab1(CreateWindowW(L"STATIC", L"💡 使用技巧：\n1. 在遊戲中滑鼠點選英雄或任何單位，面板將自動即時同步數據。\n2.「無敵鎖血」會啟用背景守護線程，在遊戲中始終保持 HP/MP 最大值。\n3. 移速最高支援魔獸上限 522 滿速，亦可自訂輸入設定。\n4.「閃現」：選中單位，滑鼠移到目標位置按 Ctrl+B。\n5.「英雄三屬性」：選中英雄按 Ctrl+H，各設為 1 億。",
            WS_CHILD | SS_LEFT, 42, 597, 411, 150, hWnd, NULL, NULL, NULL));

        // Right Panel: Resources Groupbox
        HWND hGrpRes = addTab1(CreateWindowW(L"BUTTON", L"【玩家資源修改 (黃金 & 木材)】", WS_CHILD | BS_GROUPBOX,
            474, 188, 428, 175, hWnd, NULL, NULL, NULL));
        SendMessage(hGrpRes, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab1(CreateWindowW(L"STATIC", L"目標玩家:", WS_CHILD | SS_LEFT, 490, 216, 75, 20, hWnd, NULL, NULL, NULL));
        g_hCmbResPlayer = addTab1(CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            575, 213, 130, 160, hWnd, (HMENU)IDC_CMB_RES_PLAYER, NULL, NULL));
        for (int p = 1; p <= 12; ++p) {
            std::wstring pStr = L"玩家 " + std::to_wstring(p);
            SendMessageW(g_hCmbResPlayer, CB_ADDSTRING, 0, (LPARAM)pStr.c_str());
        }
        SendMessage(g_hCmbResPlayer, CB_SETCURSEL, 0, 0);

        HWND hBtnScanResRight = addTab1(CreateWindowW(L"BUTTON", L"🔄 讀取當前資源", WS_CHILD | BS_PUSHBUTTON,
            715, 212, 170, 26, hWnd, (HMENU)IDC_BTN_MEM_SCAN_RES, NULL, NULL));

        addTab1(CreateWindowW(L"STATIC", L"金錢 (Gold):", WS_CHILD | SS_LEFT, 490, 254, 85, 20, hWnd, NULL, NULL, NULL));
        g_hEditResGold = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
            580, 252, 120, 24, hWnd, (HMENU)IDC_EDIT_RES_GOLD, NULL, NULL));
        g_hBtnAddResGold = addTab1(CreateWindowW(L"BUTTON", L"+100,000", WS_CHILD | BS_PUSHBUTTON,
            710, 251, 85, 26, hWnd, (HMENU)IDC_BTN_ADD_RES_GOLD, NULL, NULL));
        g_hBtnSetResGold = addTab1(CreateWindowW(L"BUTTON", L"設定金錢", WS_CHILD | BS_PUSHBUTTON,
            803, 251, 82, 26, hWnd, (HMENU)IDC_BTN_SET_RES_GOLD, NULL, NULL));

        addTab1(CreateWindowW(L"STATIC", L"木材 (Lumber):", WS_CHILD | SS_LEFT, 490, 292, 85, 20, hWnd, NULL, NULL, NULL));
        g_hEditResLumber = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
            580, 290, 120, 24, hWnd, (HMENU)IDC_EDIT_RES_LUMBER, NULL, NULL));
        g_hBtnAddResLumber = addTab1(CreateWindowW(L"BUTTON", L"+100,000", WS_CHILD | BS_PUSHBUTTON,
            710, 289, 85, 26, hWnd, (HMENU)IDC_BTN_ADD_RES_LUMBER, NULL, NULL));
        g_hBtnSetResLumber = addTab1(CreateWindowW(L"BUTTON", L"設定木材", WS_CHILD | BS_PUSHBUTTON,
            803, 289, 82, 26, hWnd, (HMENU)IDC_BTN_SET_RES_LUMBER, NULL, NULL));

        addTab1(CreateWindowW(L"STATIC", L"💡 提示: 支援切換玩家 1 ~ 12，單機與區域網皆可修改各自資源。",
            WS_CHILD | SS_LEFT, 490, 328, 395, 20, hWnd, NULL, NULL, NULL));

        // Right Panel: 6 Item Slots Groupbox
        HWND hGrpItems = addTab1(CreateWindowW(L"BUTTON", L"【英雄物品欄 6 格使用次數 (Charges) 修改】", WS_CHILD | BS_GROUPBOX,
            474, 370, 428, 343, hWnd, NULL, NULL, NULL));
        SendMessage(hGrpItems, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        for (int i = 0; i < 6; ++i) {
            int yPos = 398 + i * 36;
            std::wstring slotName = L"格" + std::to_wstring(i + 1) + L" [空]:";
            g_hLblItemSlot[i] = addTab1(CreateWindowW(L"STATIC", slotName.c_str(), WS_CHILD | SS_LEFT,
                490, yPos + 3, 170, 20, hWnd, NULL, NULL, NULL));
            g_hEditItemSlot[i] = addTab1(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
                665, yPos, 80, 24, hWnd, (HMENU)(IDC_EDIT_ITEM_SLOT0 + i), NULL, NULL));
            g_hBtnSetItemSlot[i] = addTab1(CreateWindowW(L"BUTTON", L"💾 設定次數", WS_CHILD | BS_PUSHBUTTON,
                755, yPos - 1, 130, 26, hWnd, (HMENU)(IDC_BTN_SET_ITEM_SLOT0 + i), NULL, NULL));
        }

        addTab1(CreateWindowW(L"STATIC", L"💡 物品說明: 支援消耗品、充能神器或具使用次數的道具。\n若裝備本身無次數屬性，修改可能不會顯示變化。",
            WS_CHILD | SS_LEFT, 490, 622, 395, 45, hWnd, NULL, NULL, NULL));

        addTab1(CreateWindowW(L"STATIC", L"💡 記憶體模組: Game.dll 偏移引擎已就緒 | 支援單機、RPG自定義地圖",
            WS_CHILD | SS_LEFT, 28, 724, 870, 20, hWnd, NULL, NULL, NULL));

        // -------------------------------------------------------------
        // Tab 2: 商城偽造與名單設定 (Mall Spoof & Blacklist)
        // -------------------------------------------------------------
        HWND hGrpMallMode = addTab2(CreateWindowW(L"BUTTON", L"【商城偽造模式、隨機比例與同步設定】", WS_CHILD | BS_GROUPBOX,
            28, 148, 874, 185, hWnd, (HMENU)IDC_GRP_MALL, NULL, NULL));
        SendMessage(hGrpMallMode, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab2(CreateWindowW(L"STATIC", L"商城偽造模式:", WS_CHILD | SS_LEFT,
            45, 185, 105, 20, hWnd, NULL, NULL, NULL));

        g_hCmbMallMode = addTab2(CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            155, 182, 255, 160, hWnd, (HMENU)IDC_CMB_MALL_MODE, NULL, NULL));
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"0 - 關閉 (原廠數據)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"1 - 白名單 (mall_spoof.txt)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"2 - 全開模式 (過濾黑名單蜜罐)");
        SendMessageW(g_hCmbMallMode, CB_ADDSTRING, 0, (LPARAM)L"3 - 比例隨機模式 (自訂機率)");
        SendMessage(g_hCmbMallMode, CB_SETCURSEL, 2, 0);

        addTab2(CreateWindowW(L"STATIC", L"隨機比例:", WS_CHILD | SS_LEFT,
            425, 185, 75, 20, hWnd, NULL, NULL, NULL));

        g_hSliderPercent = addTab2(CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
            505, 181, 175, 28, hWnd, (HMENU)IDC_SLIDER_PERCENT, NULL, NULL));
        SendMessage(g_hSliderPercent, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
        SendMessage(g_hSliderPercent, TBM_SETPAGESIZE, 0, 10);
        SendMessage(g_hSliderPercent, TBM_SETTICFREQ, 10, 0);
        SendMessage(g_hSliderPercent, TBM_SETPOS, TRUE, 80);

        g_hEditPercent = addTab2(CreateWindowW(L"EDIT", L"80", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
            686, 182, 45, 24, hWnd, (HMENU)IDC_EDIT_PERCENT, NULL, NULL));
        g_oldEditPercentProc = (WNDPROC)SetWindowLongPtr(g_hEditPercent, GWLP_WNDPROC, (LONG_PTR)EditPercentSubclassProc);

        addTab2(CreateWindowW(L"STATIC", L"%", WS_CHILD | SS_LEFT,
            736, 185, 16, 20, hWnd, NULL, NULL, NULL));

        HWND hBtnApplyMall = addTab2(CreateWindowW(L"BUTTON", L"💾 套用並同步", WS_CHILD | BS_PUSHBUTTON,
            758, 179, 130, 30, hWnd, (HMENU)IDC_BTN_APPLY_MALL, NULL, NULL));
        SendMessage(hBtnApplyMall, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Quick Mode Buttons
        HWND hBtnMode2 = addTab2(CreateWindowW(L"BUTTON", L"🛒 一鍵商城全開 (模式2)", WS_CHILD | BS_PUSHBUTTON,
            45, 226, 200, 34, hWnd, (HMENU)IDC_BTN_MALL_MODE2, NULL, NULL));
        SendMessage(hBtnMode2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnMode1 = addTab2(CreateWindowW(L"BUTTON", L"📋 僅啟用白名單 (模式1)", WS_CHILD | BS_PUSHBUTTON,
            255, 226, 195, 34, hWnd, (HMENU)IDC_BTN_MALL_MODE1, NULL, NULL));
        SendMessage(hBtnMode1, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnMode0 = addTab2(CreateWindowW(L"BUTTON", L"❌ 關閉商城偽造 (模式0)", WS_CHILD | BS_PUSHBUTTON,
            460, 226, 195, 34, hWnd, (HMENU)IDC_BTN_MALL_MODE0, NULL, NULL));
        SendMessage(hBtnMode0, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnReloadQuick = addTab2(CreateWindowW(L"BUTTON", L"🔄 立即熱加載名單", WS_CHILD | BS_PUSHBUTTON,
            665, 226, 223, 34, hWnd, (HMENU)IDC_BTN_RELOAD_LISTS, NULL, NULL));
        SendMessage(hBtnReloadQuick, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab2(CreateWindowW(L"STATIC", L"💡 當前商城狀態與機率將即時透過 Named Pipe 管道同步至遊戲內，無需重啟魔獸。",
            WS_CHILD | SS_LEFT, 45, 280, 840, 20, hWnd, NULL, NULL, NULL));

        // Groupbox: List Management
        HWND hGrpMallLists = addTab2(CreateWindowW(L"BUTTON", L"【蜜罐黑名單與商城白名單管理】", WS_CHILD | BS_GROUPBOX,
            28, 345, 874, 135, hWnd, NULL, NULL, NULL));
        SendMessage(hGrpMallLists, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab2(CreateWindowW(L"STATIC", L"可點擊下方按鈕以系統預設文字編輯器 (記事本) 開啟名單。修改並存檔後，點擊「重新加載」即可即時套用：",
            WS_CHILD | SS_LEFT, 45, 375, 840, 22, hWnd, NULL, NULL, NULL));

        HWND hBtnEditBlacklist = addTab2(CreateWindowW(L"BUTTON", L"🛡️ 編輯蜜罐黑名單 (mall_blacklist.txt)", WS_CHILD | BS_PUSHBUTTON,
            45, 410, 275, 38, hWnd, (HMENU)IDC_BTN_EDIT_BLACKLIST, NULL, NULL));
        SendMessage(hBtnEditBlacklist, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnEditWhitelist = addTab2(CreateWindowW(L"BUTTON", L"📜 編輯解鎖白名單 (mall_spoof.txt)", WS_CHILD | BS_PUSHBUTTON,
            335, 410, 275, 38, hWnd, (HMENU)IDC_BTN_EDIT_WHITELIST, NULL, NULL));
        SendMessage(hBtnEditWhitelist, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnReloadLists = addTab2(CreateWindowW(L"BUTTON", L"🔄 重新加載所有名單 (RELOAD_LISTS)", WS_CHILD | BS_PUSHBUTTON,
            625, 410, 263, 38, hWnd, (HMENU)IDC_BTN_RELOAD_LISTS, NULL, NULL));
        SendMessage(hBtnReloadLists, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Groupbox: Security Guide
        HWND hGrpGuide = addTab2(CreateWindowW(L"BUTTON", L"【商城反作弊與蜜罐 (Honeypot) 防護機制說明】", WS_CHILD | BS_GROUPBOX,
            28, 492, 874, 235, hWnd, NULL, NULL, NULL));
        SendMessage(hGrpGuide, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab2(CreateWindowW(L"STATIC",
            L"▶ 什麼是商城蜜罐 (Honeypot)？\n"
            L"   現代 RPG 地圖常在商城道具清單中刻意埋入幽靈或陷阱 Token（如 cheat, BAN_CHECK, test, ASDZDF 等）。\n"
            L"   這些項目正常玩家絕對不可能購買，地圖作者藉由檢查玩家是否回傳「擁有」這些陷阱道具來判定外掛！\n\n"
            L"▶ JassHook.dll 智慧防護核心：\n"
            L"   1. 黑名單機制：任何符合 mall_blacklist.txt 的道具名稱，無論哪種模式一律強制回傳 0 (未擁有)，杜絕封鎖！\n"
            L"   2. 模式 2 (推薦)：全開正常商城道具，同時自動剔除蜜罐黑名單項目，安全且功能齊全。\n"
            L"   3. 模式 1 (極高防護)：僅解鎖在 mall_spoof.txt 白名單中確認安全的項目，適合反作弊嚴格的地圖。\n"
            L"   4. 模式 3 (擬真隨機)：依百分比機率隨機解鎖，模擬一般玩家零散購買商城外觀的行為。",
            WS_CHILD | SS_LEFT, 45, 524, 840, 185, hWnd, NULL, NULL, NULL));

        // -------------------------------------------------------------
        // Tab 3: JASS 終端與即時日誌 (Terminal & dztrace.log Tailer)
        // -------------------------------------------------------------
        HWND hGrpCmd = addTab3(CreateWindowW(L"BUTTON", L"【自訂控制台指令 (JASS 原生執行 & Named Pipe 協定)】", WS_CHILD | BS_GROUPBOX,
            28, 148, 874, 80, hWnd, (HMENU)IDC_GRP_CMD, NULL, NULL));
        SendMessage(hGrpCmd, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        addTab3(CreateWindowW(L"STATIC", L"指令輸入:", WS_CHILD | SS_LEFT,
            45, 182, 75, 20, hWnd, NULL, NULL, NULL));

        g_hEditCmd = addTab3(CreateWindowW(L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            125, 178, 480, 28, hWnd, (HMENU)IDC_EDIT_CMD, NULL, NULL));
        SendMessage(g_hEditCmd, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);
        g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);

        HWND hBtnSendCmd = addTab3(CreateWindowW(L"BUTTON", L"發送指令 (Enter)", WS_CHILD | BS_PUSHBUTTON,
            615, 176, 130, 32, hWnd, (HMENU)IDC_BTN_SEND_CMD, NULL, NULL));
        SendMessage(hBtnSendCmd, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnLaunchCli = addTab3(CreateWindowW(L"BUTTON", L"🖥️ JassTool 視窗", WS_CHILD | BS_PUSHBUTTON,
            755, 176, 130, 32, hWnd, (HMENU)IDC_BTN_LAUNCH_CLI, NULL, NULL));
        SendMessage(hBtnLaunchCli, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hGrpLog = addTab3(CreateWindowW(L"BUTTON", L"【即時系統日誌與 JASS / DzAPI 即時監控串流 (dztrace.log)】", WS_CHILD | BS_GROUPBOX,
            28, 238, 874, 492, hWnd, (HMENU)IDC_GRP_LOG, NULL, NULL));
        SendMessage(hGrpLog, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hEditLog = addTab3(CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            42, 268, 846, 412, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL));
        SendMessage(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);

        g_hChkAutoScroll = addTab3(CreateWindowW(L"BUTTON", L"自動滾動", WS_CHILD | BS_AUTOCHECKBOX,
            45, 690, 95, 24, hWnd, (HMENU)IDC_CHK_AUTO_SCROLL, NULL, NULL));
        SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

        HWND hBtnClearLog = addTab3(CreateWindowW(L"BUTTON", L"🧹 清除日誌", WS_CHILD | BS_PUSHBUTTON,
            150, 688, 105, 28, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, NULL, NULL));

        HWND hBtnCopyLog = addTab3(CreateWindowW(L"BUTTON", L"📋 複製日誌", WS_CHILD | BS_PUSHBUTTON,
            265, 688, 105, 28, hWnd, (HMENU)IDC_BTN_COPY_LOG, NULL, NULL));

        HWND hBtnOpenTrace = addTab3(CreateWindowW(L"BUTTON", L"📄 開啟 dztrace.log", WS_CHILD | BS_PUSHBUTTON,
            380, 688, 140, 28, hWnd, (HMENU)IDC_BTN_OPEN_TRACE, NULL, NULL));

        HWND hBtnOpenSave = addTab3(CreateWindowW(L"BUTTON", L"📂 開啟 local_save.txt", WS_CHILD | BS_PUSHBUTTON,
            530, 688, 150, 28, hWnd, (HMENU)IDC_BTN_OPEN_LOCALSAVE, NULL, NULL));

        addTab3(CreateWindowW(L"STATIC", L"💡 日誌即時串流 JassHook 內部 Native 攔截與 Named Pipe 雙向通訊",
            WS_CHILD | SS_LEFT, 690, 693, 200, 20, hWnd, NULL, NULL, NULL));

        // Ensure Tab 0 is initially visible and other tabs are hidden
        SwitchMainTab();

        // Load existing saves from local_save.txt if available
        LoadExistingSavesFromFile();

        // Status Check Timer (Every 1000ms)
        SetTimer(hWnd, IDT_TIMER_STATUS, 1000, NULL);
        UpdateStatus();

        AppendLog(L"=== 魔獸爭霸 III JASS 開發者整合控制面板 v2.2 初始化完成 ===");
        AppendLog(L"[*] 支援一鍵注入、全功能分頁導覽、即時存檔監控、直接複製貼上 Save、記憶體修改與商城蜜罐過濾");

        break;
    }

    case WM_TIMER: {
        if (wParam == IDT_TIMER_STATUS) {
            UpdateStatus();
            if (WebController::IsGodModeActive()) {
                WebController::TickGodMode();
            }
            if (g_hTabMonitor && TabCtrl_GetCurSel(g_hTabMonitor) == 1) {
                RefreshMemoryTab(false);
            }
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

        if (id == IDC_CMB_RES_PLAYER && HIWORD(wParam) == CBN_SELCHANGE) {
            RefreshMemoryTab(true);
            break;
        }

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

        case IDC_BTN_MALL_MODE0:
            if (g_hCmbMallMode) SendMessage(g_hCmbMallMode, CB_SETCURSEL, 0, 0);
            ApplyMallSettings();
            break;

        case IDC_BTN_MALL_MODE1:
            if (g_hCmbMallMode) SendMessage(g_hCmbMallMode, CB_SETCURSEL, 1, 0);
            ApplyMallSettings();
            break;

        case IDC_BTN_MALL_MODE2:
            if (g_hCmbMallMode) SendMessage(g_hCmbMallMode, CB_SETCURSEL, 2, 0);
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

        case IDC_BTN_BATCH_READ:
            StartBatchRead();
            break;

        case IDC_BTN_PANEL:
            GenerateAndOpenPanel();
            break;

        case IDC_BTN_SAVEALL:
            SaveAllFromPanel();
            break;

        case IDC_BTN_EDIT_MAP_KEYS: {
            std::string defaultContent = 
                "# ========================================================\n"
                "# [War3 RPG 存檔變數清單 - map_keys.txt]\n"
                "# 每一行為一個存檔變數 Key (例如: ITYMSTGCS, IWJCS, SCDCWCD3)\n"
                "# 點擊「🔍 一鍵遍歷讀取存檔」時將依序向伺服器查詢數值\n"
                "# ========================================================\n";
            OpenTextFile("map_keys.txt", defaultContent);
            break;
        }

        case IDC_BTN_EXTRACT_KEYS:
            ExtractKeysFromDialog();
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

        case 1130: SaveSnapshotOrCompare(false); break;
        case 1131: SaveSnapshotOrCompare(true); break;
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

        case IDC_BTN_KILL: {
            bool pipeSent = SendPipeCommand("KILL");
            bool memKilled = WebController::WriteUnitStat("hp", 0.0f);
            if (pipeSent || memKilled) {
                AppendLog(L"[+] 已秒殺選中單位！(IPC + 記憶體雙重執行)");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 秒殺失敗: 未連線至魔獸或未選中單位。");
            }
            break;
        }

        case IDC_BTN_MEM_HEAL: {
            if (WebController::HealSelectedUnit()) {
                AppendLog(L"[WebController] 💖 滿血滿魔成功！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 滿血滿魔失敗: 請確認已在魔獸中選取英雄/單位。");
            }
            break;
        }

        case IDC_BTN_MEM_GODMODE: {
            bool newGod = !WebController::IsGodModeActive();
            WebController::SetGodMode(newGod);
            std::wstring godText = newGod ? L"🛡️ 無敵鎖血: 開" : L"🛡️ 無敵鎖血: 關";
            if (g_hBtnMemGod) SetWindowTextW(g_hBtnMemGod, godText.c_str());
            if (g_hBtnGodInTab) SetWindowTextW(g_hBtnGodInTab, newGod ? L"鎖血中 (開)" : L"無敵鎖血");
            AppendLog(L"[WebController] 無敵鎖血模式: " + (newGod ? std::wstring(L"開啟 (持續自動鎖定 HP/MP)") : std::wstring(L"關閉")));
            break;
        }

        case IDC_BTN_MEM_SPEED: {
            if (WebController::SetSelectedUnitMaxSpeed()) {
                AppendLog(L"[WebController] ⚡ 已設定選中單位移動速度為 522 滿速！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 移速設定失敗: 請確認已在魔獸中選取單位。");
            }
            break;
        }

        case IDC_BTN_MEM_STATS: {
            if (WebController::AddSelectedUnitStats(1000, 1000, 1000)) {
                AppendLog(L"[WebController] 💪 選中英雄力量、敏捷、智力各增加 +1000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 屬性增加失敗: 請確認已選取英雄單位。");
            }
            break;
        }

        case IDC_BTN_MEM_GOLD: {
            int curP = 1;
            if (g_hCmbResPlayer) {
                int sel = (int)SendMessage(g_hCmbResPlayer, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) curP = sel + 1;
            }
            if (WebController::AddPlayerGoldLumber(curP, 100000, 0)) {
                AppendLog(L"[WebController] 💰 玩家 " + std::to_wstring(curP) + L" 金錢增加 +100,000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 金錢增加失敗，請確認已進入遊戲。");
            }
            break;
        }

        case IDC_BTN_MEM_LUMBER: {
            int curP = 1;
            if (g_hCmbResPlayer) {
                int sel = (int)SendMessage(g_hCmbResPlayer, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) curP = sel + 1;
            }
            if (WebController::AddPlayerGoldLumber(curP, 0, 100000)) {
                AppendLog(L"[WebController] 🌲 玩家 " + std::to_wstring(curP) + L" 木材增加 +100,000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 木材增加失敗，請確認已進入遊戲。");
            }
            break;
        }

        case IDC_BTN_OPEN_WEB_UI: {
            WebController::OpenWebControllerInBrowser(8088);
            AppendLog(L"[WebController] 🌐 已啟動 Web 控制台並在瀏覽器開啟: http://127.0.0.1:8088/");
            break;
        }

        case IDC_BTN_MEM_SCAN: {
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 🔍 重新掃描單位完成");
            break;
        }

        case IDC_BTN_MEM_SCAN_RES: {
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 💰 重新掃描資源完成");
            break;
        }

        case IDC_BTN_SET_HP: {
            wchar_t hpBuf[64], maxHpBuf[64];
            GetWindowTextW(g_hEditUnitHp, hpBuf, 64);
            GetWindowTextW(g_hEditUnitMaxHp, maxHpBuf, 64);
            float hpVal = (float)_wtof(hpBuf);
            float maxHpVal = (float)_wtof(maxHpBuf);
            if (maxHpVal > 0.0f) WebController::WriteUnitStat("maxHp", maxHpVal);
            if (hpVal > 0.0f) WebController::WriteUnitStat("hp", hpVal);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定 HP 為 " + std::wstring(hpBuf) + L" / " + std::wstring(maxHpBuf));
            break;
        }

        case IDC_BTN_SET_MP: {
            wchar_t mpBuf[64], maxMpBuf[64];
            GetWindowTextW(g_hEditUnitMp, mpBuf, 64);
            GetWindowTextW(g_hEditUnitMaxMp, maxMpBuf, 64);
            float mpVal = (float)_wtof(mpBuf);
            float maxMpVal = (float)_wtof(maxMpBuf);
            if (maxMpVal > 0.0f) WebController::WriteUnitStat("maxMp", maxMpVal);
            if (mpVal >= 0.0f) WebController::WriteUnitStat("mp", mpVal);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定 MP 為 " + std::wstring(mpBuf) + L" / " + std::wstring(maxMpBuf));
            break;
        }

        case IDC_BTN_ADD_UNIT_STR: {
            if (WebController::AddSelectedUnitStats(1000, 0, 0)) {
                AppendLog(L"[WebController] 💪 選中英雄力量增加 +1000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 力量增加失敗: 請確認已選取英雄單位。");
            }
            break;
        }

        case IDC_BTN_SET_STR: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitStr, buf, 64);
            WebController::WriteUnitStat("strength", (float)_wtoi(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定力量為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_ADD_UNIT_AGI: {
            if (WebController::AddSelectedUnitStats(0, 1000, 0)) {
                AppendLog(L"[WebController] 🏃 選中英雄敏捷增加 +1000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 敏捷增加失敗: 請確認已選取英雄單位。");
            }
            break;
        }

        case IDC_BTN_SET_AGI: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitAgi, buf, 64);
            WebController::WriteUnitStat("agility", (float)_wtoi(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定敏捷為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_ADD_UNIT_INT: {
            if (WebController::AddSelectedUnitStats(0, 0, 1000)) {
                AppendLog(L"[WebController] 🧠 選中英雄智力增加 +1000！");
                RefreshMemoryTab(true);
            } else {
                AppendLog(L"[-] 智力增加失敗: 請確認已選取英雄單位。");
            }
            break;
        }

        case IDC_BTN_SET_INT: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitInt, buf, 64);
            WebController::WriteUnitStat("intelligence", (float)_wtoi(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定智力為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_SET_SPEED: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitSpeed, buf, 64);
            WebController::WriteUnitStat("moveSpeed", (float)_wtof(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定移動速度為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_SET_DEF: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitDef, buf, 64);
            WebController::WriteUnitStat("defense", (float)_wtof(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定防禦值為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_ADD_UNIT_EXP: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitExp, buf, 64);
            int curExp = _wtoi(buf);
            int newExp = curExp + 10000;
            WebController::WriteUnitStat("experience", (float)newExp);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 🌟 選中英雄經驗值增加 +10,000 (當前: " + std::to_wstring(newExp) + L")！");
            break;
        }

        case IDC_BTN_SET_EXP: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitExp, buf, 64);
            WebController::WriteUnitStat("experience", (float)_wtoi(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定經驗值為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_SET_RES_GOLD: {
            int curP = 1;
            if (g_hCmbResPlayer) {
                int sel = (int)SendMessage(g_hCmbResPlayer, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) curP = sel + 1;
            }
            wchar_t buf[64];
            GetWindowTextW(g_hEditResGold, buf, 64);
            int val = _wtoi(buf);
            WebController::WritePlayerResource(curP, "gold", val);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 玩家 " + std::to_wstring(curP) + L" 金錢設定為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_SET_RES_LUMBER: {
            int curP = 1;
            if (g_hCmbResPlayer) {
                int sel = (int)SendMessage(g_hCmbResPlayer, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) curP = sel + 1;
            }
            wchar_t buf[64];
            GetWindowTextW(g_hEditResLumber, buf, 64);
            int val = _wtoi(buf);
            WebController::WritePlayerResource(curP, "lumber", val);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 玩家 " + std::to_wstring(curP) + L" 木材設定為 " + std::wstring(buf));
            break;
        }

        case IDC_BTN_SET_ITEM_SLOT0:
        case IDC_BTN_SET_ITEM_SLOT1:
        case IDC_BTN_SET_ITEM_SLOT2:
        case IDC_BTN_SET_ITEM_SLOT3:
        case IDC_BTN_SET_ITEM_SLOT4:
        case IDC_BTN_SET_ITEM_SLOT5: {
            int slot = id - IDC_BTN_SET_ITEM_SLOT0;
            wchar_t buf[64];
            GetWindowTextW(g_hEditItemSlot[slot], buf, 64);
            int chg = _wtoi(buf);
            WebController::WriteUnitItemCharges(slot, chg);
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 物品欄格 " + std::to_wstring(slot + 1) + L" 次數已修改為 " + std::wstring(buf));
            break;
        }

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

        case IDC_BTN_MEM_BLINK: {
            if (SendPipeCommand("BLINK:-1:-1")) {
                AppendLog(L"[Blink] 請切回遊戲，選取單位，將滑鼠移到目標地面後按 Ctrl+B；可移動鏡頭到遠處再閃現。");
            } else {
                AppendLog(L"[Blink] 管道未連線，請先注入 DLL。");
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
        if (g_hBatchReadThread) {
            g_isBatchReading = false;
            WaitForSingleObject(g_hBatchReadThread, 500);
            CloseHandle(g_hBatchReadThread);
            g_hBatchReadThread = NULL;
        }
        WebController::StopWebControllerServer();
        WebController::CleanupMemoryController();
        DisconnectPipe();
        if (g_hLogBrush) DeleteObject(g_hLogBrush);
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontConsole) DeleteObject(g_hFontConsole);
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

    RECT rc = { 0, 0, 930, 770 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    g_hWnd = CreateWindowW(
        L"War3DevToolUIClass",
        L"魔獸爭霸 III JASS 開發者整合控制面板 (War3DevTool v2.2 - 整合 Web Controller)",
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
