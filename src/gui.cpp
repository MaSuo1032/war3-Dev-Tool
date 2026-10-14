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

HWND g_hTabMonitor = NULL;
HWND g_hListSaveMonitor = NULL;
HWND g_hBtnCopySaveItem = NULL;
HWND g_hBtnFillSaveInput = NULL;
HWND g_hBtnClearSaveList = NULL;
HWND g_hBtnReloadSave = NULL;
HWND g_hBtnClearLog = NULL;
HWND g_hBtnCopyLog = NULL;
HWND g_hBtnOpenTrace = NULL;

// Quick Memory Action Buttons (Group 4)
HWND g_hBtnMemHeal = NULL;
HWND g_hBtnMemGod = NULL;
HWND g_hBtnMemSpeed = NULL;
HWND g_hBtnMemStats = NULL;
HWND g_hBtnMemGold = NULL;
HWND g_hBtnMemLumber = NULL;
HWND g_hBtnOpenWebUi = NULL;

// Tab 1: Memory Controller Controls
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
        if (ParseSaveLine(line, "1048602", p, k, v)) {
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
    AppendLog(L"[+] 批量回寫完成！共發送 " + std::to_wstring(count) + L" 筆存檔指令至遊戲。");
    MessageBoxW(g_hWnd, (L"批量回寫完成！共送出 " + std::to_wstring(count) + L" 筆指令。").c_str(), L"成功", MB_ICONINFORMATION);
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

    AppendLog(L"[*] 開始向伺服器一鍵遍歷讀取存檔 [來源: " + Utf8ToWide(sourceDesc) + L"] (目標玩家: " + Utf8ToWide(defPid) + L", 共 " + std::to_wstring(keys.size()) + L" 筆變數)...");

    int count = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!g_isBatchReading) break;
        const std::string& k = keys[i];
        std::string cmd = "GET:" + defPid + ":" + k;
        if (SendPipeCommand(cmd)) {
            count++;
        }
        Sleep(15);
    }

    AppendLog(L"[+] 遍歷讀取請求發送完畢！共向伺服器查詢 " + std::to_wstring(count) + L" 筆變數，數值將即時顯示於「存檔即時監控」列表中。");
    MessageBoxW(g_hWnd, (L"遍歷讀取請求發送完成！共向伺服器查詢 " + std::to_wstring(count) + L" 筆存檔變數。\n[來源: " + Utf8ToWide(sourceDesc) + L"]\n\n數值將於數秒內陸續在「存檔即時監控」列表中呈現。").c_str(), L"遍歷讀取完成", MB_ICONINFORMATION);
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
        if (ParseSaveLine(line, defPid, pid, key, val)) {
            lastParsedPid = pid;
            lastParsedKey = key;
            lastParsedVal = val;

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

    // Auto-update the PID, Key, Val text boxes if single entry was executed
    if (successCount == 1 && !lastParsedKey.empty()) {
        if (g_hEditPid && !lastParsedPid.empty()) SetWindowTextA(g_hEditPid, lastParsedPid.c_str());
        if (g_hEditKey) SetWindowTextA(g_hEditKey, lastParsedKey.c_str());
        if (g_hEditVal) SetWindowTextA(g_hEditVal, lastParsedVal.c_str());
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

// Refresh selected unit attributes & player resources in Tab 1
void RefreshMemoryTab(bool force = false) {
    if (!g_hWnd) return;
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

// Toggle visibility between Save Monitor (0), Memory Controller (1), and All Logs (2) tabs
void SwitchMonitorTab() {
    if (!g_hTabMonitor) return;
    int curTab = TabCtrl_GetCurSel(g_hTabMonitor);
    bool isSaveTab = (curTab == 0);
    bool isMemTab = (curTab == 1);
    bool isLogTab = (curTab == 2);

    // Tab 0: Save Monitor
    ShowWindow(g_hListSaveMonitor, isSaveTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnFillSaveInput, isSaveTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnCopySaveItem, isSaveTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnReloadSave, isSaveTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnClearSaveList, isSaveTab ? SW_SHOW : SW_HIDE);

    // Tab 1: Memory Controller
    for (HWND h : g_memControls) {
        ShowWindow(h, isMemTab ? SW_SHOW : SW_HIDE);
    }
    if (isMemTab) {
        RefreshMemoryTab(true);
    }

    // Tab 2: Logs
    ShowWindow(g_hEditLog, isLogTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hChkAutoScroll, isLogTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnClearLog, isLogTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnCopyLog, isLogTab ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnOpenTrace, isLogTab ? SW_SHOW : SW_HIDE);
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

        // 3. Group 3: 伺服器存檔管理、即時寫入與遍歷讀取 (Save Management & Batch Query)
        HWND hGrp3 = CreateWindowW(L"BUTTON", L"【伺服器存檔管理、即時寫入與一鍵遍歷讀取 (Method 1: 全地圖存檔讀取)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 225, 850, 120, hWnd, (HMENU)IDC_GRP_SAVE, NULL, NULL);
        SendMessage(hGrp3, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnBatchRead = CreateWindowW(L"BUTTON", L"🔍 一鍵遍歷讀取存檔 (!readall)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 245, 220, 30, hWnd, (HMENU)IDC_BTN_BATCH_READ, NULL, NULL);
        SendMessage(hBtnBatchRead, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnPanel = CreateWindowW(L"BUTTON", L"📝 產生並開啟面板", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            255, 245, 160, 30, hWnd, (HMENU)IDC_BTN_PANEL, NULL, NULL);
        SendMessage(hBtnPanel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnSaveAll = CreateWindowW(L"BUTTON", L"⚡ 批量覆寫存檔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            420, 245, 140, 30, hWnd, (HMENU)IDC_BTN_SAVEALL, NULL, NULL);
        SendMessage(hBtnSaveAll, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        HWND hBtnEditKeys = CreateWindowW(L"BUTTON", L"📜 編輯變數名單", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            565, 245, 115, 30, hWnd, (HMENU)IDC_BTN_EDIT_MAP_KEYS, NULL, NULL);
        SendMessage(hBtnEditKeys, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnExtractKeys = CreateWindowW(L"BUTTON", L"📂 提取Key", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            685, 245, 85, 30, hWnd, (HMENU)IDC_BTN_EXTRACT_KEYS, NULL, NULL);
        SendMessage(hBtnExtractKeys, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnOpenLocal = CreateWindowW(L"BUTTON", L"📄 存檔日誌", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            775, 245, 75, 30, hWnd, (HMENU)IDC_BTN_OPEN_LOCALSAVE, NULL, NULL);
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

        HWND hLblHint = CreateWindowW(L"STATIC", L"💡 存檔功能: 點擊「一鍵遍歷讀取存檔」向伺服器查詢全部變數，支援手動輸入 [SaveServerValue]、ID:Key=Val 或 !save", WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 316, 820, 18, hWnd, NULL, NULL, NULL);
        SendMessage(hLblHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 4. Group 4: 單位屬性快捷與原版 Jass 指令終端 (Web Controller 記憶體功能)
        HWND hGrp4 = CreateWindowW(L"BUTTON", L"【單位屬性快捷與原版 Jass 指令終端 (Web Controller 記憶體功能)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 350, 850, 95, hWnd, (HMENU)IDC_GRP_CMD, NULL, NULL);
        SendMessage(hGrp4, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Row 1: Quick Action Buttons
        HWND hBtnKill = CreateWindowW(L"BUTTON", L"💀 秒殺", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            25, 372, 85, 28, hWnd, (HMENU)IDC_BTN_KILL, NULL, NULL);
        SendMessage(hBtnKill, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemHeal = CreateWindowW(L"BUTTON", L"💖 滿血滿魔", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            115, 372, 95, 28, hWnd, (HMENU)IDC_BTN_MEM_HEAL, NULL, NULL);
        SendMessage(g_hBtnMemHeal, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemGod = CreateWindowW(L"BUTTON", L"🛡️ 無敵鎖血: 關", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            215, 372, 120, 28, hWnd, (HMENU)IDC_BTN_MEM_GODMODE, NULL, NULL);
        SendMessage(g_hBtnMemGod, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemSpeed = CreateWindowW(L"BUTTON", L"⚡ 522滿速", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            340, 372, 85, 28, hWnd, (HMENU)IDC_BTN_MEM_SPEED, NULL, NULL);
        SendMessage(g_hBtnMemSpeed, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemStats = CreateWindowW(L"BUTTON", L"💪 屬性+1000", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            430, 372, 95, 28, hWnd, (HMENU)IDC_BTN_MEM_STATS, NULL, NULL);
        SendMessage(g_hBtnMemStats, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemGold = CreateWindowW(L"BUTTON", L"💰 金錢+10W", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 372, 95, 28, hWnd, (HMENU)IDC_BTN_MEM_GOLD, NULL, NULL);
        SendMessage(g_hBtnMemGold, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnMemLumber = CreateWindowW(L"BUTTON", L"🌲 木材+10W", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            630, 372, 95, 28, hWnd, (HMENU)IDC_BTN_MEM_LUMBER, NULL, NULL);
        SendMessage(g_hBtnMemLumber, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hBtnOpenWebUi = CreateWindowW(L"BUTTON", L"🌐 Web 控制台", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            730, 372, 120, 28, hWnd, (HMENU)IDC_BTN_OPEN_WEB_UI, NULL, NULL);
        SendMessage(g_hBtnOpenWebUi, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Row 2: Jass Terminal Command
        HWND hLblCmd = CreateWindowW(L"STATIC", L"Jass 指令:", WS_CHILD | WS_VISIBLE | SS_LEFT,
            25, 412, 70, 20, hWnd, NULL, NULL, NULL);
        SendMessage(hLblCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hEditCmd = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            100, 409, 620, 26, hWnd, (HMENU)IDC_EDIT_CMD, NULL, NULL);
        SendMessage(g_hEditCmd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        g_oldEditCmdProc = (WNDPROC)SetWindowLongPtr(g_hEditCmd, GWLP_WNDPROC, (LONG_PTR)EditCmdSubclassProc);

        HWND hBtnSend = CreateWindowW(L"BUTTON", L"發送指令", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            730, 408, 120, 28, hWnd, (HMENU)IDC_BTN_SEND_CMD, NULL, NULL);
        SendMessage(hBtnSend, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // 5. Group 5: 即時監控與記憶體控制器 (Live Monitor & Controller)
        HWND hGrp5 = CreateWindowW(L"BUTTON", L"【即時監控與記憶體控制器 (Live Monitor & Controller)】", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            15, 452, 850, 310, hWnd, (HMENU)IDC_GRP_LOG, NULL, NULL);
        SendMessage(hGrp5, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Tab Control for Monitor
        g_hTabMonitor = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            30, 472, 420, 28, hWnd, (HMENU)IDC_TAB_MONITOR, NULL, NULL);
        SendMessage(g_hTabMonitor, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        TCITEMW tci0{};
        tci0.mask = TCIF_TEXT;
        tci0.pszText = (LPWSTR)L"💾 存檔即時監控 (0 筆)";
        TabCtrl_InsertItemW_Helper(g_hTabMonitor, 0, &tci0);

        TCITEMW tci1{};
        tci1.mask = TCIF_TEXT;
        tci1.pszText = (LPWSTR)L"⚔️ 英雄與記憶體控制";
        TabCtrl_InsertItemW_Helper(g_hTabMonitor, 1, &tci1);

        TCITEMW tci2{};
        tci2.mask = TCIF_TEXT;
        tci2.pszText = (LPWSTR)L"📜 全部串流日誌";
        TabCtrl_InsertItemW_Helper(g_hTabMonitor, 2, &tci2);

        // Save Monitor Toolbar Buttons (Tab 0)
        g_hBtnFillSaveInput = CreateWindowW(L"BUTTON", L"✏️ 填入輸入框", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            460, 472, 105, 26, hWnd, (HMENU)IDC_BTN_FILL_SAVE_INPUT, NULL, NULL);
        SendMessage(g_hBtnFillSaveInput, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnCopySaveItem = CreateWindowW(L"BUTTON", L"📋 複製格式", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            570, 472, 115, 26, hWnd, (HMENU)IDC_BTN_COPY_SAVE_ITEM, NULL, NULL);
        SendMessage(g_hBtnCopySaveItem, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnReloadSave = CreateWindowW(L"BUTTON", L"🔄 重新讀取", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            690, 472, 95, 26, hWnd, (HMENU)IDC_BTN_RELOAD_SAVE, NULL, NULL);
        SendMessage(g_hBtnReloadSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnClearSaveList = CreateWindowW(L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            790, 472, 60, 26, hWnd, (HMENU)IDC_BTN_CLEAR_SAVE_LIST, NULL, NULL);
        SendMessage(g_hBtnClearSaveList, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Save Monitor ListView (Tab 0)
        g_hListSaveMonitor = CreateWindowW(WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_VSCROLL,
            30, 502, 820, 250, hWnd, (HMENU)IDC_LIST_SAVE_MONITOR, NULL, NULL);
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

        // General Log Toolbar Buttons (Tab 2, initially hidden)
        g_hChkAutoScroll = CreateWindowW(L"BUTTON", L"自動滾動", WS_CHILD | BS_AUTOCHECKBOX,
            460, 474, 90, 20, hWnd, (HMENU)IDC_CHK_AUTO_SCROLL, NULL, NULL);
        SendMessage(g_hChkAutoScroll, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessage(g_hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

        g_hBtnClearLog = CreateWindowW(L"BUTTON", L"清空日誌", WS_CHILD | BS_PUSHBUTTON,
            555, 472, 85, 26, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, NULL, NULL);
        SendMessage(g_hBtnClearLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnCopyLog = CreateWindowW(L"BUTTON", L"複製日誌", WS_CHILD | BS_PUSHBUTTON,
            645, 472, 85, 26, hWnd, (HMENU)IDC_BTN_COPY_LOG, NULL, NULL);
        SendMessage(g_hBtnCopyLog, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        g_hBtnOpenTrace = CreateWindowW(L"BUTTON", L"開啟 dztrace.log", WS_CHILD | BS_PUSHBUTTON,
            735, 472, 115, 26, hWnd, (HMENU)IDC_BTN_OPEN_TRACE, NULL, NULL);
        SendMessage(g_hBtnOpenTrace, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // All Logs EditBox (Tab 2, initially hidden)
        g_hEditLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            30, 502, 820, 250, hWnd, (HMENU)IDC_EDIT_LOG, NULL, NULL);
        SendMessage(g_hEditLog, WM_SETFONT, (WPARAM)g_hFontConsole, TRUE);

        // -------------------------------------------------------------
        // Tab 1: Memory Controller Controls (Initially Hidden)
        // -------------------------------------------------------------
        g_memControls.clear();

        auto addMemCtrl = [&](HWND h) {
            if (h) {
                SendMessage(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                g_memControls.push_back(h);
            }
            return h;
        };

        // Header and Scan Toolbar
        g_hLblUnitHeader = addMemCtrl(CreateWindowW(L"STATIC", L"未選取單位 (請在魔獸遊戲中點選英雄或單位)", WS_CHILD | SS_LEFT,
            35, 506, 430, 20, hWnd, NULL, NULL, NULL));
        g_hBtnMemScan = addMemCtrl(CreateWindowW(L"BUTTON", L"🔍 重新掃描單位", WS_CHILD | BS_PUSHBUTTON,
            470, 502, 110, 26, hWnd, (HMENU)IDC_BTN_MEM_SCAN, NULL, NULL));
        g_hBtnMemScanRes = addMemCtrl(CreateWindowW(L"BUTTON", L"💰 重新掃描資源", WS_CHILD | BS_PUSHBUTTON,
            585, 502, 110, 26, hWnd, (HMENU)IDC_BTN_MEM_SCAN_RES, NULL, NULL));
        g_hBtnWebUiInTab = addMemCtrl(CreateWindowW(L"BUTTON", L"🌐 開啟 Web 控制面板", WS_CHILD | BS_PUSHBUTTON,
            700, 502, 150, 26, hWnd, (HMENU)IDC_BTN_OPEN_WEB_UI, NULL, NULL));

        // Left Column: Hero & Unit Attributes
        addMemCtrl(CreateWindowW(L"STATIC", L"HP 當前/最大:", WS_CHILD | SS_LEFT, 35, 536, 92, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitHp = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 130, 534, 58, 22, hWnd, (HMENU)IDC_EDIT_UNIT_HP, NULL, NULL));
        addMemCtrl(CreateWindowW(L"STATIC", L"/", WS_CHILD | SS_CENTER, 190, 536, 10, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMaxHp = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 202, 534, 58, 22, hWnd, (HMENU)IDC_EDIT_UNIT_MAXHP, NULL, NULL));
        g_hBtnSetUnitHp = addMemCtrl(CreateWindowW(L"BUTTON", L"設HP", WS_CHILD | BS_PUSHBUTTON, 265, 533, 55, 24, hWnd, (HMENU)IDC_BTN_SET_HP, NULL, NULL));
        g_hBtnHealInTab = addMemCtrl(CreateWindowW(L"BUTTON", L"滿血滿魔", WS_CHILD | BS_PUSHBUTTON, 325, 533, 75, 24, hWnd, (HMENU)IDC_BTN_MEM_HEAL, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"MP 當前/最大:", WS_CHILD | SS_LEFT, 35, 564, 92, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMp = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 130, 562, 58, 22, hWnd, (HMENU)IDC_EDIT_UNIT_MP, NULL, NULL));
        addMemCtrl(CreateWindowW(L"STATIC", L"/", WS_CHILD | SS_CENTER, 190, 564, 10, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitMaxMp = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 202, 562, 58, 22, hWnd, (HMENU)IDC_EDIT_UNIT_MAXMP, NULL, NULL));
        g_hBtnSetUnitMp = addMemCtrl(CreateWindowW(L"BUTTON", L"設MP", WS_CHILD | BS_PUSHBUTTON, 265, 561, 55, 24, hWnd, (HMENU)IDC_BTN_SET_MP, NULL, NULL));
        g_hBtnGodInTab = addMemCtrl(CreateWindowW(L"BUTTON", L"無敵鎖血", WS_CHILD | BS_PUSHBUTTON, 325, 561, 75, 24, hWnd, (HMENU)IDC_BTN_MEM_GODMODE, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"英雄力量:", WS_CHILD | SS_LEFT, 35, 592, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitStr = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 130, 590, 75, 22, hWnd, (HMENU)IDC_EDIT_UNIT_STR, NULL, NULL));
        g_hBtnAddUnitStr = addMemCtrl(CreateWindowW(L"BUTTON", L"+1000", WS_CHILD | BS_PUSHBUTTON, 212, 589, 58, 24, hWnd, (HMENU)IDC_BTN_MEM_STATS, NULL, NULL));
        g_hBtnSetUnitStr = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 275, 589, 45, 24, hWnd, (HMENU)IDC_BTN_SET_STR, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"英雄敏捷:", WS_CHILD | SS_LEFT, 35, 620, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitAgi = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 130, 618, 75, 22, hWnd, (HMENU)IDC_EDIT_UNIT_AGI, NULL, NULL));
        g_hBtnAddUnitAgi = addMemCtrl(CreateWindowW(L"BUTTON", L"+1000", WS_CHILD | BS_PUSHBUTTON, 212, 617, 58, 24, hWnd, (HMENU)IDC_BTN_MEM_STATS, NULL, NULL));
        g_hBtnSetUnitAgi = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 275, 617, 45, 24, hWnd, (HMENU)IDC_BTN_SET_AGI, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"英雄智力:", WS_CHILD | SS_LEFT, 35, 648, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitInt = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 130, 646, 75, 22, hWnd, (HMENU)IDC_EDIT_UNIT_INT, NULL, NULL));
        g_hBtnAddUnitInt = addMemCtrl(CreateWindowW(L"BUTTON", L"+1000", WS_CHILD | BS_PUSHBUTTON, 212, 645, 58, 24, hWnd, (HMENU)IDC_BTN_MEM_STATS, NULL, NULL));
        g_hBtnSetUnitInt = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 275, 645, 45, 24, hWnd, (HMENU)IDC_BTN_SET_INT, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"移動速度:", WS_CHILD | SS_LEFT, 35, 676, 90, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitSpeed = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER, 130, 674, 75, 22, hWnd, (HMENU)IDC_EDIT_UNIT_SPEED, NULL, NULL));
        g_hBtnMaxUnitSpeed = addMemCtrl(CreateWindowW(L"BUTTON", L"522滿速", WS_CHILD | BS_PUSHBUTTON, 212, 673, 62, 24, hWnd, (HMENU)IDC_BTN_MEM_SPEED, NULL, NULL));
        g_hBtnSetUnitSpeed = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 280, 673, 45, 24, hWnd, (HMENU)IDC_BTN_SET_SPEED, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"防禦:", WS_CHILD | SS_LEFT, 35, 704, 35, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitDef = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER, 72, 702, 55, 22, hWnd, (HMENU)IDC_EDIT_UNIT_DEF, NULL, NULL));
        g_hBtnSetUnitDef = addMemCtrl(CreateWindowW(L"BUTTON", L"設防", WS_CHILD | BS_PUSHBUTTON, 130, 701, 45, 24, hWnd, (HMENU)IDC_BTN_SET_DEF, NULL, NULL));
        addMemCtrl(CreateWindowW(L"STATIC", L"經驗:", WS_CHILD | SS_LEFT, 180, 704, 35, 20, hWnd, NULL, NULL, NULL));
        g_hEditUnitExp = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 218, 702, 65, 22, hWnd, (HMENU)IDC_EDIT_UNIT_EXP, NULL, NULL));
        g_hBtnAddUnitExp = addMemCtrl(CreateWindowW(L"BUTTON", L"+1萬", WS_CHILD | BS_PUSHBUTTON, 288, 701, 50, 24, hWnd, (HMENU)IDC_BTN_SET_EXP, NULL, NULL));
        g_hBtnSetUnitExp = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 342, 701, 45, 24, hWnd, (HMENU)IDC_BTN_SET_EXP, NULL, NULL));

        // Right Column: Player Resources & Inventory
        addMemCtrl(CreateWindowW(L"STATIC", L"玩家:", WS_CHILD | SS_LEFT, 440, 536, 40, 20, hWnd, NULL, NULL, NULL));
        g_hCmbResPlayer = addMemCtrl(CreateWindowW(L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 485, 533, 90, 160, hWnd, (HMENU)IDC_CMB_RES_PLAYER, NULL, NULL));
        for (int p = 1; p <= 12; ++p) {
            std::wstring pStr = L"玩家 " + std::to_wstring(p);
            SendMessageW(g_hCmbResPlayer, CB_ADDSTRING, 0, (LPARAM)pStr.c_str());
        }
        SendMessage(g_hCmbResPlayer, CB_SETCURSEL, 0, 0);

        addMemCtrl(CreateWindowW(L"STATIC", L"金錢:", WS_CHILD | SS_LEFT, 585, 536, 40, 20, hWnd, NULL, NULL, NULL));
        g_hEditResGold = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 630, 534, 75, 22, hWnd, (HMENU)IDC_EDIT_RES_GOLD, NULL, NULL));
        g_hBtnAddResGold = addMemCtrl(CreateWindowW(L"BUTTON", L"+10W", WS_CHILD | BS_PUSHBUTTON, 710, 533, 50, 24, hWnd, (HMENU)IDC_BTN_ADD_RES_GOLD, NULL, NULL));
        g_hBtnSetResGold = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 765, 533, 45, 24, hWnd, (HMENU)IDC_BTN_SET_RES_GOLD, NULL, NULL));

        addMemCtrl(CreateWindowW(L"STATIC", L"木材:", WS_CHILD | SS_LEFT, 585, 564, 40, 20, hWnd, NULL, NULL, NULL));
        g_hEditResLumber = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER, 630, 562, 75, 22, hWnd, (HMENU)IDC_EDIT_RES_LUMBER, NULL, NULL));
        g_hBtnAddResLumber = addMemCtrl(CreateWindowW(L"BUTTON", L"+10W", WS_CHILD | BS_PUSHBUTTON, 710, 561, 50, 24, hWnd, (HMENU)IDC_BTN_ADD_RES_LUMBER, NULL, NULL));
        g_hBtnSetResLumber = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON, 765, 561, 45, 24, hWnd, (HMENU)IDC_BTN_SET_RES_LUMBER, NULL, NULL));

        // 6 Inventory Slots
        struct SlotPos { int lx, ly, ex, ey, bx, by; };
        SlotPos sp[6] = {
            { 440, 592, 535, 590, 580, 589 },
            { 635, 592, 730, 590, 775, 589 },
            { 440, 620, 535, 618, 580, 617 },
            { 635, 620, 730, 618, 775, 617 },
            { 440, 648, 535, 646, 580, 645 },
            { 635, 648, 730, 646, 775, 645 }
        };

        for (int i = 0; i < 6; ++i) {
            std::wstring slotName = L"格" + std::to_wstring(i + 1) + L" [空]:";
            g_hLblItemSlot[i] = addMemCtrl(CreateWindowW(L"STATIC", slotName.c_str(), WS_CHILD | SS_LEFT,
                sp[i].lx, sp[i].ly, 90, 20, hWnd, NULL, NULL, NULL));
            g_hEditItemSlot[i] = addMemCtrl(CreateWindowW(L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_CENTER | ES_NUMBER,
                sp[i].ex, sp[i].ey, 40, 22, hWnd, (HMENU)(IDC_EDIT_ITEM_SLOT0 + i), NULL, NULL));
            g_hBtnSetItemSlot[i] = addMemCtrl(CreateWindowW(L"BUTTON", L"設定", WS_CHILD | BS_PUSHBUTTON,
                sp[i].bx, sp[i].by, 40, 24, hWnd, (HMENU)(IDC_BTN_SET_ITEM_SLOT0 + i), NULL, NULL));
        }

        addMemCtrl(CreateWindowW(L"STATIC", L"💡 提示: 在魔獸中選取任何單位後點擊「重新掃描」，即可直接修改生命/屬性/移速/物品或開啟 Web UI！", WS_CHILD | SS_LEFT,
            440, 680, 400, 45, hWnd, NULL, NULL, NULL));

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

        case IDC_BTN_SET_STR: {
            wchar_t buf[64];
            GetWindowTextW(g_hEditUnitStr, buf, 64);
            WebController::WriteUnitStat("strength", (float)_wtoi(buf));
            RefreshMemoryTab(true);
            AppendLog(L"[WebController] 已設定力量為 " + std::wstring(buf));
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

    RECT rc = { 0, 0, 880, 790 };
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
