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

    bool IsTrapItem(const std::string& key) const;
    bool IsValidMallItemKey(const std::string& key) const;

private:
    MallSpoofer() = default;

    static int __cdecl Hook_HasMallItem(int player, DWORD key_ptr);
    static int __cdecl Hook_IsVIP(int player);
    static int __cdecl Hook_GetMapLevel(int player);
    static int __cdecl Hook_GetServerValueErrorCode(int player);

    typedef int(__cdecl *HasMallItem_t)(int, DWORD);
    HasMallItem_t m_origHasMallItem = nullptr;

    std::unordered_set<std::string> m_blacklist;
    std::unordered_set<std::string> m_whitelist;
};

} // namespace War3Dev
