#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>

// [SUPPORTED MODDING] / [OFFICIAL]
// War3JassDevTool - IPC Client (External Developer Console)

void PrintMenu() {
    std::cout << "========================================================\n";
    std::cout << " Warcraft III JASS & Native Developer Console\n";
    std::cout << "========================================================\n";
    std::cout << " [璅∪ 1: JASS 單砍瑁] \n";
    std::cout << " 湔亥撓亙啣鋆∠∪貊賣詨 (靘憒: Debug_TestFunction)\n\n";
    std::cout << " [璅∪ 2: 摨撅 Native 澆 (Dynamic Invoker)] \n";
    std::cout << " 隢典Ｗ銝 ! 蝚西嚗貊函征賡\n";
    std::cout << " 靘憒: !PurchaseZeppelin\n";
    std::cout << " 靘憒: !scan_dzapi  (寞隞歹 War3Plugin 閮園)\n";
    std::cout << " 靘憒: !SuicideUnit 123 456\n";
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
        std::cout << "Waiting for Warcraft III JassHook Pipe...\n";
        Sleep(2000);
    }
    
    std::cout << "[IPC] Connected to Warcraft III successfully!\n";

    std::string input;
    while (true) {
        std::cout << "\n> Command ('exit' to quit): ";
        std::getline(std::cin, input);
        
        if (input == "exit") break;
        if (input.empty()) continue;
        
        // 蝘駁 \r  \n
        while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) continue;
        
        // 閫 Native 隞 (! )
        if (input == "!kill" || input == "!killselected") {
            input = "KILL";
        }
        else if (input == "!panel") {
            std::ifstream infile("local_save.txt");
            if (!infile.is_open()) {
                std::cout << "[-] 無法讀取存檔紀錄，找不到 local_save.txt\n";
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

            std::cout << "[Panel] 已產生 save_panel.txt，為您自動開啟...\n";
            system("notepad save_panel.txt");
            continue;
        }
        else if (input == "!saveall") {
            std::ifstream pfile("save_panel.txt");
            if (!pfile.is_open()) {
                std::cout << "[-] 曆 save_panel.txt嚗隢頛詨 !panel Ｙ\n";
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
                    WriteFile(hPipe, payload.c_str(), payload.length(), &bw, NULL);
                    std::cout << "[IPC] Sent: " << payload << "\n";
                    count++;
                    Sleep(20); // 脫揪璈
                }
            }
            pfile.close();
            std::cout << "[+] 寞活潮 " << count << " 蝑摮瑼蝝嚗\n";
            continue;
        }
        else if (input.rfind("!save ", 0) == 0) {
            std::stringstream ss(input.substr(6));
            std::string p, k, v;
            if (ss >> p >> k >> v) {
                input = "SAVE:" + p + ":" + k + ":" + v;
            } else {
                std::cout << "[-] 澆航炊嚗甇蝣箸澆: !save <PlayerID> <Key> <Value>\n";
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
        bool success = WriteFile(hPipe, input.c_str(), input.length(), &bytesWritten, NULL);
        
        if (success) {
            std::cout << "[IPC] Sent: " << input << std::endl;
        } else {
            std::cout << "[-] Failed to send command. Pipe might be closed.\n";
            break;
        }
    }
    
    CloseHandle(hPipe);
    return 0;
}
