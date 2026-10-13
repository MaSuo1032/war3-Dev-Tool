#include "MallSpoofer.hpp"
#include "Common/Config.hpp"
#include "Common/Logger.hpp"
#include "Core/HookEngine.hpp"
#include "Jass/JassTypes.hpp"
#include "Jass/NativeRegistry.hpp"
#include <fstream>
#include <algorithm>

namespace War3Dev {

static MallSpoofer* s_instance = nullptr;
static PatchHook* s_hookHasMallItem = nullptr;

MallSpoofer& MallSpoofer::Get() {
    static MallSpoofer instance;
    s_instance = &instance;
    return instance;
}

void MallSpoofer::ReloadLists() {
    m_whitelist.clear();
    std::wstring wlPath = Config::Get().GetFilePath(L"mall_spoof.txt");
    std::ifstream wlFile(wlPath);
    if (wlFile.is_open()) {
        std::string line;
        while (std::getline(wlFile, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            m_whitelist.insert(line);
        }
        wlFile.close();
    }

    LOG("[MallSpoofer] Loaded %zu spoofable items from mall_spoof.txt\n", m_whitelist.size());
}

int __cdecl MallSpoofer::Hook_IsVIP() {
    return 1;
}

int __cdecl MallSpoofer::Hook_GetMapLevel() {
    return Config::Get().GetMapLevel();
}

int __cdecl MallSpoofer::Hook_HasMallItem(int player, DWORD key_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);

    // Call Original FIRST
    int originalResult = 0;
    if (s_hookHasMallItem) {
        s_hookHasMallItem->Unhook();
        typedef int(__cdecl *FuncType)(int, DWORD);
        originalResult = reinterpret_cast<FuncType>(s_hookHasMallItem->GetTarget())(player, key_ptr);
        s_hookHasMallItem->Rehook();
    }

    int mode = Config::Get().GetMallMode();
    int finalResult = originalResult;

    if (mode == 2) {
        finalResult = 1; // Spoof ALL items
    } else if (mode == 1) {
        if (s_instance && !s_instance->m_whitelist.empty() && keyStr) {
            std::string key(keyStr);
            if (s_instance->m_whitelist.find(key) != s_instance->m_whitelist.end()) {
                finalResult = 1;
            }
        }
    }

    LOG("[HasMallItem] Player: %d | Item: \"%s\" | Server: %d | Spoofed: %d\n", player, keyStr, originalResult, finalResult);
    LOG_SAVE("Player %d MALL ITEM: %s = %d (Spoofed to %d)\n", player, keyStr, originalResult, finalResult);
    return finalResult;
}

void MallSpoofer::Initialize() {
    ReloadLists();

    auto& reg = NativeRegistry::Get();
    auto& hook = HookEngine::Get();

    LOG("[Spoofer] Applying VIP / Mall / Level Spoofs...\n");
    int mode = Config::Get().GetMallMode();
    if (mode > 0) {
        const NativeInfo* isVip = reg.Find("DzAPI_Map_IsVIP");
        if (isVip && isVip->funcPtr) {
            hook.InstallSpoof("DzAPI_Map_IsVIP", isVip->funcPtr, Hook_IsVIP);
        } else {
            LOG("[Spoofer] Failed to hook DzAPI_Map_IsVIP (Not found)\n");
        }
    }

    const NativeInfo* mapLvl = reg.Find("DzAPI_Map_GetMapLevel");
    if (mapLvl && mapLvl->funcPtr) {
        hook.InstallSpoof("DzAPI_Map_GetMapLevel", mapLvl->funcPtr, Hook_GetMapLevel);
    }

    const NativeInfo* hasMall = reg.Find("DzAPI_Map_HasMallItem");
    if (hasMall && hasMall->funcPtr) {
        s_hookHasMallItem = hook.CreatePatchHook("DzAPI_Map_HasMallItem", hasMall->funcPtr, Hook_HasMallItem);
    }

    LOG("[MallSpoofer] Mall spoofing system initialized.\n");
}

} // namespace War3Dev
