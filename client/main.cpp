#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <map>
#include <vector>

void PrintMenu() {
    std::cout << "========================================================\n";
    std::cout << " Warcraft III JASS & Native Developer Console v2.0\n";
    std::cout << "========================================================\n";
    std::cout << " [模式 1: 存檔即時修改面板]\n";
    std::cout << "   panel    - 自動擷取遊戲攔截到的所有存檔 Key 並開啟記事本編輯\n";
    std::cout << "   saveall  - 一鍵將 save_panel.txt 中修改後的數值批次寫入地圖\n";
    std::cout << "   save <PlayerID> <Key> <Value> - 單筆寫入存檔 (支援含空白與字串ID)\n\n";
    std::cout << " [模式 2: 防作弊結束保護 (Anti-GameOver)]\n";
    std::cout << "   antiover 1 - 開啟防結束保護 (阻擋 EndGame, RemovePlayer, 戰役失敗彈窗)\n";
    std::cout << "   antiover 0 - 關閉防結束保護\n\n";
    std::cout << " [模式 3: 商城道具偽裝與自訂]\n";
    std::cout << "   mall 2     - 100% 全開商城道具\n";
    std::cout << "   mall 1     - 白名單模式 (讀取 mall_spoof.txt)\n";
    std::cout << "   mall 0     - 關閉商城修改\n";
    std::cout << "   percent 30 - 隨機開啟 30% 商城 (可指定 1~100 趴)\n\n";
    std::cout << " [模式 4: 即時控制與 Native 調度]\n";
    std::cout << "   kill       - 秒殺當前選取的單位\n";
    std::cout << "   !<Native> [參數...] - 動態調用底層 Native 函數\n";
    std::cout << "========================================================\n";
}

std::string FindLocalSavePath() {
    const char* candidates[] = {
        "local_save.txt",
        "..\\local_save.txt",
        "..\\WarcraftⅢ-11hf\\local_save.txt",
        "C:\\warcraft\\1.27new\\WarcraftⅢ-11hf\\local_save.txt",
        "C:\\warcraft\\1.27new\\war3DevTool\\local_save.txt",
    };
    for (const char* path : candidates) {
        std::ifstream test(path);
        if (test.good()) {
            return std::string(path);
        }
    }
    return "local_save.txt";
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    PrintMenu();

    HANDLE hPipe = INVALID_HANDLE_VALUE;
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
        std::cout << "Waiting for Warcraft III Named Pipe connection...\n";
        Sleep(2000);
    }

    std::cout << "[IPC] Connected to Warcraft III successfully!\n";

    std::string input;
    while (true) {
        std::cout << "\n> Command ('exit' to quit): ";
        std::getline(std::cin, input);

        if (input == "exit" || input == "quit") break;
        if (input.empty()) continue;

        while (!input.empty() && (input.back() == '\r' || input.back() == '\n')) {
            input.pop_back();
        }
        if (input.empty()) continue;

        if (input == "help" || input == "!help" || input == "?") {
            PrintMenu();
            continue;
        }

        if (input == "antiover 1" || input == "!antiover 1" || input == "antiover1" || input == "!antiover1") {
            input = "ANTIOVER:1";
        }
        else if (input == "antiover 0" || input == "!antiover 0" || input == "antiover0" || input == "!antiover0") {
            input = "ANTIOVER:0";
        }
        else if (input.rfind("percent ", 0) == 0 || input.rfind("!percent ", 0) == 0) {
            size_t offset = (input[0] == '!') ? 9 : 8;
            int pct = atoi(input.substr(offset).c_str());
            input = "PERCENT:" + std::to_string(pct);
        }
        else if (input.rfind("mall ", 0) == 0 || input.rfind("!mall ", 0) == 0) {
            size_t offset = (input[0] == '!') ? 6 : 5;
            int mode = atoi(input.substr(offset).c_str());
            input = "MALL:" + std::to_string(mode);
        }
        else if (input == "kill" || input == "!kill") {
            input = "KILL";
        }
        else if (input == "panel" || input == "!panel") {
            std::string saveFile = FindLocalSavePath();
            std::ifstream infile(saveFile);
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
                else if (line.find("SAVED:") != std::string::npos || line.find("MANUAL_SAVED:") != std::string::npos || line.find("SET_STAT:") != std::string::npos) {
                    size_t pPos = line.find("Player ");
                    size_t colonPos = line.find(":", pPos);
                    if (pPos != std::string::npos && colonPos != std::string::npos) {
                        size_t tagSpace = line.find(" ", colonPos);
                        if (tagSpace != std::string::npos) {
                            std::string player = line.substr(pPos + 7, line.find(" ", pPos + 7) - (pPos + 7));
                            size_t eqPos = line.find(" = ", tagSpace);
                            if (eqPos != std::string::npos) {
                                std::string key = line.substr(tagSpace + 1, eqPos - (tagSpace + 1));
                                std::string val = line.substr(eqPos + 3);
                                uniqueKeys[{player, key}] = val;
                            }
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
            outfile << "# 修改完畢後儲存，並在控制台輸入 saveall 來上傳至地圖\n";
            outfile << "# ==========================================\n\n";
            for (auto const& [pk, val] : uniqueKeys) {
                outfile << pk.first << ":" << pk.second << "=" << val << "\n";
            }
            outfile.close();

            std::cout << "[Panel] 已從 " << saveFile << " 擷取 " << uniqueKeys.size() << " 筆存檔項目，已開啟 save_panel.txt...\n";
            system("notepad save_panel.txt");
            continue;
        }
        else if (input == "saveall" || input == "!saveall") {
            std::ifstream pfile("save_panel.txt");
            if (!pfile.is_open()) {
                std::cout << "[-] 找不到 save_panel.txt，請先輸入 panel 產生\n";
                continue;
            }
            std::string line;
            int count = 0;
            while (std::getline(pfile, line)) {
                if (line.empty() || line[0] == '#') continue;
                size_t colon = line.find(':');
                size_t eq = line.find('=');
                if (colon != std::string::npos && eq != std::string::npos && colon < eq) {
                    std::string p = line.substr(0, colon);
                    std::string k = line.substr(colon + 1, eq - colon - 1);
                    std::string v = line.substr(eq + 1);

                    std::string payload = "SAVE:" + p + ":" + k + ":" + v;
                    DWORD bw;
                    WriteFile(hPipe, payload.c_str(), static_cast<DWORD>(payload.length()), &bw, NULL);
                    std::cout << "[IPC] 已寫入: Player " << p << " | Key: " << k << " = " << v << "\n";
                    count++;
                    Sleep(20);
                }
            }
            pfile.close();
            std::cout << "[+] 成功上傳 " << count << " 筆存檔紀錄至魔獸爭霸！\n";
            continue;
        }
        else if (input.rfind("save ", 0) == 0 || input.rfind("!save ", 0) == 0) {
            size_t offset = (input[0] == '!') ? 6 : 5;
            std::stringstream ss(input.substr(offset));
            std::string p, k, v;
            if (ss >> p >> k) {
                std::getline(ss, v);
                size_t firstNonSpace = v.find_first_not_of(" \t");
                if (firstNonSpace != std::string::npos) {
                    v = v.substr(firstNonSpace);
                } else {
                    v = "";
                }
                if (!v.empty()) {
                    input = "SAVE:" + p + ":" + k + ":" + v;
                } else {
                    std::cout << "[-] 參數格式錯誤！正確格式: save <PlayerID> <Key> <Value>\n";
                    continue;
                }
            } else {
                std::cout << "[-] 參數格式錯誤！正確格式: save <PlayerID> <Key> <Value>\n";
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
        bool success = WriteFile(hPipe, input.c_str(), static_cast<DWORD>(input.length()), &bytesWritten, NULL);

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
