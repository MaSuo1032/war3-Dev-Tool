#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>

void PrintMenu() {
    std::cout << "========================================================\n";
    std::cout << " Warcraft III JASS & Native 開發者控制台 v2.0\n";
    std::cout << "========================================================\n";
    std::cout << " [存檔相關指令]\n";
    std::cout << "  !panel            : 從 local_save.txt 產生 save_panel.txt 並用記事本開啟修改\n";
    std::cout << "  !saveall          : 將 save_panel.txt 內的修改批量寫入回遊戲伺服器\n";
    std::cout << "  !save <P> <K> <V> : 手動發送單筆存檔 (例: !save 1048584 Isuipian1 999)\n\n";
    std::cout << " [遊戲與商城動態控制]\n";
    std::cout << "  !mall <0|1|2|3>   : 切換商城模式 (0=原廠關閉, 1=白名單, 2=全開, 3=百分比)\n";
    std::cout << "  !percent <1~100>  : 設定商城隨機開啟百分比 (僅模式3生效)\n";
    std::cout << "  !level <等級>     : 動態偽造地圖等級 (例: !level 99)\n";
    std::cout << "  !antiover <0|1>   : 切換防作弊遊戲失敗/結算阻擋保護 (1=開啟, 0=關閉)\n";
    std::cout << "  !kill             : 秒殺當前選取的單位 (HP歸零)\n\n";
    std::cout << " [動態 Native 呼叫]\n";
    std::cout << "  !<Native名稱> <參數1> <參數2> ... (例: !SuicideUnit 123 456)\n";
    std::cout << "  !help             : 重新顯示此選單\n";
    std::cout << "========================================================\n";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    PrintMenu();
    
    HANDLE hPipe;
    while (true) {
        hPipe = CreateFileA(
            "\\\\.\\pipe\\War3JassDevTool",
            GENERIC_READ | GENERIC_WRITE,
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
    
    std::cout << "[IPC] 已成功連線至魔獸爭霸！\n";

    std::string input;
    while (true) {
        std::cout << "\n> Command ('exit' 離開): ";
        std::getline(std::cin, input);
        
        if (input == "exit") break;
        if (input.empty()) continue;
        
        while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) continue;
        
        if (input == "!kill" || input == "!killselected") {
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
        else if (input.rfind("!level ", 0) == 0) {
            input = "LEVEL:" + input.substr(7);
        }
        else if (input.rfind("!antiover ", 0) == 0) {
            input = "ANTIOVER:" + input.substr(10);
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
            }
            infile.close();

            std::ofstream outfile("save_panel.txt");
            outfile << "# ==========================================\n";
            outfile << "# [外掛存檔修改面板]\n";
            outfile << "# 等號右邊是你從伺服器真實載入的原始數值\n";
            outfile << "# 請在這裡直接修改數值，例如把 (null/empty) 改成 1 或 <StringID: 419>\n";
            outfile << "# 修改完畢後儲存，並在控制台輸入 !saveall 來上傳至地圖\n";
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
                if (line.empty() || line[0] == '#') continue;
                size_t colon = line.find(':');
                size_t eq = line.find('=');
                if (colon != std::string::npos && eq != std::string::npos) {
                    std::string p = line.substr(0, colon);
                    std::string k = line.substr(colon + 1, eq - colon - 1);
                    std::string v = line.substr(eq + 1);
                    
                    std::string payload = "SAVE:" + p + ":" + k + ":" + v;
                    DWORD bw;
                    WriteFile(hPipe, payload.c_str(), (DWORD)payload.length(), &bw, NULL);
                    std::cout << "[IPC] 已發送: " << payload << "\n";
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
        
        DWORD bytesWritten;
        bool success = WriteFile(hPipe, input.c_str(), (DWORD)input.length(), &bytesWritten, NULL);
        
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
