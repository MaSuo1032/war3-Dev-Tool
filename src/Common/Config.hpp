#pragma once
#include <windows.h>
#include <string>

namespace War3Dev {

class Config {
public:
    static Config& Get();

    void Initialize(HMODULE hModule);
    void Reload();

    int GetMallMode() const { return m_mallMode; }
    void SetMallMode(int mode) { m_mallMode = mode; }

    int GetMallPercent() const { return m_mallPercent; }
    void SetMallPercent(int pct) { m_mallPercent = pct; }

    int GetMapLevel() const { return m_mapLevel; }
    void SetMapLevel(int lvl) { m_mapLevel = lvl; }

    int GetPreventGameOver() const { return m_preventGameOver; }
    void SetPreventGameOver(int enable) { m_preventGameOver = enable; }

    const std::wstring& GetDllDirectory() const { return m_dllDir; }
    std::wstring GetFilePath(const std::wstring& filename) const;

private:
    Config() = default;

    int m_mallMode = 1;
    int m_mallPercent = 30;
    int m_mapLevel = 50;
    int m_preventGameOver = 1;
    std::wstring m_dllDir;
};

} // namespace War3Dev
