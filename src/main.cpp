#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>
#include <cstring>

bool CopyToClipboard(const std::string& value) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, value.size() + 1);
    if (!buffer) { CloseClipboard(); return false; }
    void* address = GlobalLock(buffer);
    if (!address) { GlobalFree(buffer); CloseClipboard(); return false; }
    memcpy(address, value.c_str(), value.size() + 1);
    GlobalUnlock(buffer);
    if (!SetClipboardData(CF_TEXT, buffer)) { GlobalFree(buffer); CloseClipboard(); return false; }
    CloseClipboard();
    return true;
}

std::wstring ExeDirectory() {
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring path = exe_path;
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : path.substr(0, slash + 1);
}

std::string ToUtf8(const std::wstring& value) {
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], size, nullptr, nullptr);
    result.pop_back();
    return result;
}

bool PrepareLuaTraceModules(HANDLE pipe, std::wstring& game_dir) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t game_path[MAX_PATH] = {};
    DWORD length = MAX_PATH;
    bool found = QueryFullProcessImageNameW(process, 0, game_path, &length) != FALSE;
    CloseHandle(process);
    if (!found) return false;
    game_dir = game_path;
    size_t slash = game_dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return false;
    game_dir.resize(slash + 1);
    const std::wstring source = ExeDirectory();
    for (const wchar_t* name : {L"death_trace.lua", L"death_trace_stop.lua"}) {
        if (!CopyFileW((source + name).c_str(), (game_dir + name).c_str(), FALSE)) return false;
    }
    return true;
}

void PrintMenu() {
    std::cout << "========================================================\n";
    std::cout << " Warcraft III JASS & Native 開發者控制台 v2.1\n";
    std::cout << "========================================================\n";
    std::cout << " [存檔相關指令]\n";
    std::cout << "  !panel            : 從 local_save.txt 產生 save_panel.txt 並用記事本開啟修改\n";
    std::cout << "  !saveall          : 將 save_panel.txt 內的修改批量寫入回遊戲伺服器\n";
    std::cout << "  !save <P> <K> <V> : 手動發送單筆存檔 (例: !save 1048602 IWJCS 999)\n\n";
    std::cout << " [商城模式與黑名單控制]\n";
    std::cout << "  !mall <0|1|2|3>   : 切換商城模式 (0=原廠關閉, 1=白名單, 2=全開, 3=百分比)\n";
    std::cout << "  !percent <1~100>  : 設定商城隨機開啟百分比 (僅模式3生效)\n";
    std::cout << "  !reload           : 重新載入 mall_spoof.txt 與 mall_blacklist.txt\n\n";
    std::cout << " [單位操作]\n";
    std::cout << "  !kill             : 以遊戲 KillUnit 原生函式殺死選取單位 (Ctrl+K)\n\n";
    std::cout << "  !hero100m         : 選取英雄的力量／敏捷／智慧各設為 1 億 (Ctrl+H)\n";
    std::cout << "  !luatrace         : 部署 Lua 死亡/掉寶追蹤模組並複製啟動指令\n";
    std::cout << "  !luatrace stop    : 複製停止 Lua 追蹤指令\n\n";
    std::cout << " [動態 Native 呼叫]\n";
    std::cout << "  !<Native名稱> <參數1> <參數2> ... (例: !SuicideUnit 123 456)\n";
    std::cout << "  !help             : 重新顯示此選單\n";
    std::cout << "========================================================\n";
}

DWORD WINAPI CliLogTailer(LPVOID lpParam) {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string s = exePath;
    size_t last = s.find_last_of("\\/");
    std::string dir = (last != std::string::npos) ? s.substr(0, last + 1) : ".\\";
    std::string logPath = dir + "dztrace.log";
    std::streampos lastPos = 0;
    {
        std::ifstream initFile(logPath, std::ios::in | std::ios::binary);
        if (initFile.is_open()) {
            initFile.seekg(0, std::ios::end);
            lastPos = initFile.tellg();
            if (lastPos < 4096) lastPos = 0;
        }
    }
    while (true) {
        std::ifstream file(logPath, std::ios::in | std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            std::streampos curSize = file.tellg();
            if (curSize < lastPos) lastPos = 0;
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
                        std::cout << "\n[MONITOR] " << line << "\n> " << std::flush;
                    }
                }
            }
            file.close();
        }
        Sleep(200);
    }
    return 0;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    PrintMenu();
    
    HANDLE hPipe;
    while (true) {
        hPipe = CreateFileA(
            "\\\\.\\pipe\\War3JassDevTool",
            GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);
            
        if (hPipe != INVALID_HANDLE_VALUE) {
            break;
        }
        std::cout << "等待魔獸爭霸 JassHook 連線 (請先注入 JassHook.dll)...\n";
        Sleep(2000);
    }
    
    std::cout << "[IPC] 已成功連線至魔獸爭霸！(已啟用非阻塞 dztrace.log 監控串流)\n";
    CreateThread(NULL, 0, CliLogTailer, NULL, 0, NULL);

    std::string input;
    while (true) {
        std::cout << "\n> Command ('exit' 離開): ";
        std::getline(std::cin, input);
        
        if (input == "exit") break;
        if (input.empty()) continue;
        
        while (!input.empty() && (input.back() == '\r' || input.back() == '\n' || input.back() == ' ')) {
            input.pop_back();
        }
        if (input.empty()) continue;
        
        if (input == "!luatrace" || input == "!luatrace stop") {
            std::wstring game_dir;
            if (!PrepareLuaTraceModules(hPipe, game_dir)) {
                std::cout << "[LuaTrace] 無法部署模組到遊戲資料夾；請確認遊戲路徑與檔案權限。\n";
                continue;
            }
            const bool stop = input == "!luatrace stop";
            const std::string command = stop ? "exec-lua:death_trace_stop" : "exec-lua:death_trace";
            const std::string request = stop ? "LUA_TRACE_STOP\n" : "LUA_TRACE_START\n";
            DWORD written = 0;
            bool sent = WriteFile(hPipe, request.data(), (DWORD)request.size(), &written, nullptr) != FALSE && written == request.size();
            std::cout << (sent ? "[LuaTrace] 已請求遊戲內 Lua 引擎載入模組。" : "[LuaTrace] 自動啟動失敗。")
                      << (CopyToClipboard(command) ? "備用指令已複製。\n" : "\n")
                      << "紀錄位置：" << ToUtf8(game_dir) << "war3dev_death_trace.log；確認 READY 與 death_event=true。\n";
            continue;
        }
        if (input == "!hero100m") {
            input = "HERO_100M";
        }
        else if (input == "!kill" || input == "!killselected") {
            input = "KILL";
        }
        else if (input == "!help") {
            PrintMenu();
            continue;
        }
        else if (input.rfind("!mall ", 0) == 0) {
            input = "MALL:" + input.substr(6);
        }
        else if (input.rfind("!percent ", 0) == 0) {
            input = "PERCENT:" + input.substr(9);
        }
        else if (input == "!reload") {
            input = "RELOAD_LISTS";
        }
        else if (input == "!panel") {
            std::ifstream infile("local_save.txt");
            if (!infile.is_open()) {
                std::cout << "[-] 無法讀取存檔紀錄，找不到 local_save.txt (請先在遊戲中載入存檔)\n";
                continue;
            }
            std::map<std::pair<std::string, std::string>, std::string> uniqueKeys;
            std::string line;
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
                        }
                    }
                }
            }
            infile.close();

            std::ofstream outfile("save_panel.txt");
            outfile << "# ==========================================\n";
            outfile << "# [War3 RPG 存檔修改面板 - Save Panel]\n";
            outfile << "# 格式: 玩家ID:存檔變數Key=目標數值\n";
            outfile << "# 等號右邊是你從伺服器真實載入的原始數值\n";
            outfile << "# 請在這裡直接修改數值，例如把 (null/empty) 改成 1 或 <StringID: 419>\n";
            outfile << "# 修改完畢後儲存 (Ctrl+S)，並在控制台輸入 !saveall 來上傳至地圖\n";
            outfile << "# ==========================================\n\n";
            for (auto const& [pk, val] : uniqueKeys) {
                outfile << pk.first << ":" << pk.second << "=" << val << "\n";
            }
            outfile.close();

            std::cout << "[Panel] 已產生 save_panel.txt (共 " << uniqueKeys.size() << " 筆數據)，為您自動開啟...\n";
            system("notepad save_panel.txt");
            continue;
        }
        else if (input == "!saveall") {
            std::ifstream pfile("save_panel.txt");
            if (!pfile.is_open()) {
                std::cout << "[-] 找不到 save_panel.txt，請先輸入 !panel 生成\n";
                continue;
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
                    
                    std::string payload = "SAVE:" + p + ":" + k + ":" + v + "\n";
                    DWORD bw;
                    WriteFile(hPipe, payload.c_str(), (DWORD)payload.length(), &bw, NULL);
                    std::cout << "[IPC] 已發送: " << payload.substr(0, payload.length() - 1) << "\n";
                    count++;
                    Sleep(20);
                }
            }
            pfile.close();
            std::cout << "[+] 批量存檔完成，共送出 " << count << " 筆指令！\n";
            continue;
        }
        else if (input.rfind("!save ", 0) == 0) {
            std::string rest = input.substr(6);
            std::stringstream ss(rest);
            std::string p, k;
            if (ss >> p >> k) {
                std::string v;
                std::getline(ss, v);
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                if (!v.empty()) {
                    input = "SAVE:" + p + ":" + k + ":" + v;
                } else {
                    std::cout << "[-] 格式錯誤: 必須提供數值！正確格式: !save <PlayerID> <Key> <Value>\n";
                    continue;
                }
            } else {
                std::cout << "[-] 格式錯誤: !save <PlayerID> <Key> <Value>\n";
                continue;
            }
        }
        else if (input[0] == '!') {
            std::string cmd = input.substr(1);
            std::string formatted = "NATIVE:";
            std::stringstream ss(cmd);
            std::string token;
            bool first = true;
            while (ss >> token) {
                if (!first) formatted += ":";
                formatted += token;
                first = false;
            }
            input = formatted;
        }
        
        std::string payload = input;
        if (payload.back() != '\n') payload += "\n";

        DWORD bytesWritten;
        bool success = WriteFile(hPipe, payload.c_str(), (DWORD)payload.length(), &bytesWritten, NULL);
        
        if (success) {
            std::cout << "[IPC] 已發送指令: " << input << std::endl;
        } else {
            std::cout << "[-] 發送指令失敗，Named Pipe 可能已中斷。\n";
            break;
        }
    }
    
    CloseHandle(hPipe);
    return 0;
}
