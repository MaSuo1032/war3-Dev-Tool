#include "PipeServer.hpp"
#include "Protocol.hpp"
#include "Common/Config.hpp"
#include "Common/Logger.hpp"
#include "Game/GameDefines.hpp"
#include "Jass/NativeRegistry.hpp"
#include "Jass/NativeInvoker.hpp"
#include "Modules/SaveManager.hpp"
#include <cstring>
#include <cstdlib>

namespace War3Dev {

PipeServer& PipeServer::Get() {
    static PipeServer instance;
    return instance;
}

void PipeServer::Start(HWND hGameWnd) {
    m_hGameWnd = hGameWnd;
    m_running = true;
    m_hThread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
}

void PipeServer::Stop() {
    m_running = false;
    if (m_hThread) {
        WaitForSingleObject(m_hThread, 1000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }
}

DWORD WINAPI PipeServer::ThreadProc(LPVOID lpParam) {
    PipeServer* server = static_cast<PipeServer*>(lpParam);
    server->Run();
    return 0;
}

void PipeServer::Run() {
    char buffer[2048];

    while (m_running) {
        HANDLE hPipe = CreateNamedPipeA(
            WAR3_DEV_PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            2048,
            2048,
            0,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            LOG("[PipeServer] Client connected to Named Pipe.\n");
            DWORD bytesRead = 0;

            while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                LOG("[PipeServer] Received command: %s\n", buffer);

                if (strncmp(buffer, CMD_ANTIOVER, strlen(CMD_ANTIOVER)) == 0) {
                    int val = atoi(buffer + strlen(CMD_ANTIOVER));
                    Config::Get().SetPreventGameOver(val);
                    LOG("[PipeServer] PreventGameOver set to: %d\n", val);
                }
                else if (strncmp(buffer, CMD_PERCENT, strlen(CMD_PERCENT)) == 0) {
                    int pct = atoi(buffer + strlen(CMD_PERCENT));
                    Config::Get().SetMallPercent(pct);
                    LOG("[PipeServer] MallPercent set to: %d%%\n", pct);
                }
                else if (strncmp(buffer, CMD_MALL, strlen(CMD_MALL)) == 0) {
                    int mode = atoi(buffer + strlen(CMD_MALL));
                    Config::Get().SetMallMode(mode);
                    LOG("[PipeServer] MallMode set to: %d\n", mode);
                }
                else if (strcmp(buffer, CMD_KILL) == 0) {
                    if (m_hGameWnd) {
                        SendMessageA(m_hGameWnd, WM_WAR3_KILL_UNIT, 0, 0);
                    }
                }
                else if (strncmp(buffer, CMD_SAVE, strlen(CMD_SAVE)) == 0) {
                    char* firstColon = strchr(buffer, ':');
                    if (firstColon) {
                        char* secondColon = strchr(firstColon + 1, ':');
                        if (secondColon) {
                            char* thirdColon = strchr(secondColon + 1, ':');
                            if (thirdColon && m_hGameWnd) {
                                *secondColon = '\0';
                                *thirdColon = '\0';
                                char* pStr = firstColon + 1;
                                char* kStr = secondColon + 1;
                                char* vStr = thirdColon + 1;

                                SavePayload* payload = new SavePayload();
                                payload->player = atoi(pStr);
                                strncpy_s(payload->key, sizeof(payload->key), kStr, _TRUNCATE);
                                strncpy_s(payload->val, sizeof(payload->val), vStr, _TRUNCATE);

                                SendMessageA(m_hGameWnd, WM_WAR3_SAVE_SERVER, 0, reinterpret_cast<LPARAM>(payload));
                            }
                        }
                    }
                }
                else if (strncmp(buffer, CMD_NATIVE, strlen(CMD_NATIVE)) == 0) {
                    char* ctx = nullptr;
                    strtok_s(buffer, ":", &ctx); // Skip NATIVE
                    char* nativeName = strtok_s(nullptr, ":", &ctx);
                    char* arg1 = strtok_s(nullptr, ":", &ctx);
                    char* arg2 = strtok_s(nullptr, ":", &ctx);
                    char* arg3 = strtok_s(nullptr, ":", &ctx);

                    if (nativeName) {
                        const NativeInfo* info = NativeRegistry::Get().Find(nativeName);
                        if (info && m_hGameWnd) {
                            InvokePayload* payload = new InvokePayload();
                            payload->funcPtr = info->funcPtr;
                            strncpy_s(payload->sig, sizeof(payload->sig), info->signature, _TRUNCATE);
                            strncpy_s(payload->arg1, sizeof(payload->arg1), arg1 ? arg1 : "", _TRUNCATE);
                            strncpy_s(payload->arg2, sizeof(payload->arg2), arg2 ? arg2 : "", _TRUNCATE);
                            strncpy_s(payload->arg3, sizeof(payload->arg3), arg3 ? arg3 : "", _TRUNCATE);

                            SendMessageA(m_hGameWnd, WM_WAR3_INVOKE_NATIVE, 0, reinterpret_cast<LPARAM>(payload));
                        } else {
                            LOG("[-] PipeServer: Native '%s' not found in registry!\n", nativeName);
                        }
                    }
                }
            }
        }
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
}

} // namespace War3Dev
