#pragma once
#include <windows.h>
#include <atomic>

namespace War3Dev {

class PipeServer {
public:
    static PipeServer& Get();

    void Start(HWND hGameWnd);
    void Stop();

private:
    PipeServer() = default;

    static DWORD WINAPI ThreadProc(LPVOID lpParam);
    void Run();

    HWND m_hGameWnd = NULL;
    HANDLE m_hThread = NULL;
    std::atomic<bool> m_running{ false };
};

} // namespace War3Dev
