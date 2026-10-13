#include "Logger.hpp"
#include "Config.hpp"
#include <cstdio>
#include <cstdarg>

namespace War3Dev {

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

void Logger::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_consoleAllocated) {
        AllocConsole();
        SetConsoleTitleA("Warcraft III - JASS Dev Tool Monitor");
        m_consoleAllocated = true;
    }
}

void Logger::Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(m_mutex);
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdOut && hStdOut != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteConsoleA(hStdOut, buffer, (DWORD)strlen(buffer), &written, NULL);
    }
    OutputDebugStringA(buffer);
}

void Logger::LogSave(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(m_mutex);
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    std::wstring savePath = Config::Get().GetFilePath(L"local_save.txt");
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, savePath.c_str(), L"a") == 0 && fp) {
        fputs(buffer, fp);
        fclose(fp);
    }
}

} // namespace War3Dev
