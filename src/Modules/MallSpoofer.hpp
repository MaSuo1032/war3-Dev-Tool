#pragma once
#include <windows.h>
#include <unordered_set>
#include <string>

namespace War3Dev {

class MallSpoofer {
public:
    static MallSpoofer& Get();

    void Initialize();
    void ReloadLists();

private:
    MallSpoofer() = default;

    static int __cdecl Hook_HasMallItem(int player, DWORD key_ptr);
    static int __cdecl Hook_IsVIP();
    static int __cdecl Hook_GetMapLevel();

    std::unordered_set<std::string> m_blacklist;
    std::unordered_set<std::string> m_whitelist;
};

} // namespace War3Dev
