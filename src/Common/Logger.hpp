#pragma once
#include <windows.h>
#include <string>
#include <mutex>

namespace War3Dev {

class Logger {
public:
    static Logger& Get();

    void Initialize();
    void Log(const char* fmt, ...);
    void LogSave(const char* fmt, ...);

private:
    Logger() = default;

    bool m_consoleAllocated = false;
    std::mutex m_mutex;
};

#define LOG(fmt, ...) ::War3Dev::Logger::Get().Log(fmt, ##__VA_ARGS__)
#define LOG_SAVE(fmt, ...) ::War3Dev::Logger::Get().LogSave(fmt, ##__VA_ARGS__)

} // namespace War3Dev
