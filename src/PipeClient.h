#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Named Pipe client for asynchronous/non-blocking communication with JassHook.dll
class PipeClient {
public:
    static PipeClient& Instance() {
        static PipeClient inst;
        return inst;
    }

    void Init() {
        if (!m_initialized) {
            InitializeCriticalSection(&m_cs);
            m_initialized = true;
        }
    }

    void Shutdown() {
        Disconnect();
        if (m_initialized) {
            DeleteCriticalSection(&m_cs);
            m_initialized = false;
        }
    }

    bool Connect(const char* pipeName = "\\\\.\\pipe\\War3JassDevTool") {
        Init();
        EnterCriticalSection(&m_cs);
        if (m_hPipe != INVALID_HANDLE_VALUE) {
            LeaveCriticalSection(&m_cs);
            return true;
        }
        m_hPipe = CreateFileA(pipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        bool ok = (m_hPipe != INVALID_HANDLE_VALUE);
        LeaveCriticalSection(&m_cs);
        return ok;
    }

    void Disconnect() {
        Init();
        EnterCriticalSection(&m_cs);
        if (m_hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&m_cs);
    }

    bool Send(const std::string& cmd) {
        if (!Connect()) return false;
        EnterCriticalSection(&m_cs);
        if (m_hPipe == INVALID_HANDLE_VALUE) {
            LeaveCriticalSection(&m_cs);
            return false;
        }
        std::string payload = cmd;
        if (payload.empty() || payload.back() != '\n') {
            payload += "\n";
        }
        DWORD bytesWritten = 0;
        BOOL ok = WriteFile(m_hPipe, payload.c_str(), (DWORD)payload.length(), &bytesWritten, NULL);
        if (!ok) {
            CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
        LeaveCriticalSection(&m_cs);
        return ok != FALSE;
    }

    bool IsConnected() const {
        return m_hPipe != INVALID_HANDLE_VALUE;
    }

private:
    PipeClient() : m_hPipe(INVALID_HANDLE_VALUE), m_initialized(false) {
        Init();
    }
    ~PipeClient() {
        Shutdown();
    }

    HANDLE m_hPipe;
    CRITICAL_SECTION m_cs;
    bool m_initialized;
};

inline bool ConnectPipe() {
    return PipeClient::Instance().Connect();
}

inline void DisconnectPipe() {
    PipeClient::Instance().Disconnect();
}

inline bool SendPipeCommand(const std::string& cmd) {
    return PipeClient::Instance().Send(cmd);
}
