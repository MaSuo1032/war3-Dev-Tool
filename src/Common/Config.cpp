#include "Config.hpp"

namespace War3Dev {

Config& Config::Get() {
    static Config instance;
    return instance;
}

void Config::Initialize(HMODULE hModule) {
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(hModule, path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) {
        *lastSlash = L'\0';
    }
    m_dllDir = path;
    Reload();
}

void Config::Reload() {
    std::wstring iniPath = GetFilePath(L"JassHookConfig.ini");

    m_mallMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 1, iniPath.c_str());
    m_mallPercent = GetPrivateProfileIntW(L"Spoof", L"MallPercent", 30, iniPath.c_str());
    if (m_mallPercent < 0) m_mallPercent = 0;
    if (m_mallPercent > 100) m_mallPercent = 100;

    m_mapLevel = GetPrivateProfileIntW(L"Spoof", L"MapLevel", 50, iniPath.c_str());
    m_preventGameOver = GetPrivateProfileIntW(L"Spoof", L"PreventGameOver", 1, iniPath.c_str());
}

std::wstring Config::GetFilePath(const std::wstring& filename) const {
    return m_dllDir + L"\\" + filename;
}

} // namespace War3Dev
