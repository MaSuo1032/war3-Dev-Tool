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

MallSpoofer& MallSpoofer::Get() {
    static MallSpoofer instance;
    s_instance = &instance;
    return instance;
}

static uint32_t HashItemKey(const std::string& key) {
    uint32_t hash = 2166136261u;
    for (char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

void MallSpoofer::ReloadLists() {
    m_blacklist.clear();
    m_whitelist.clear();

    // Default built-in traps
    m_blacklist.insert("cheat");
    m_blacklist.insert("test");
    m_blacklist.insert("trap");
    m_blacklist.insert("BAN_CHECK");
    m_blacklist.insert("TEST_MALL_ITEM");

    std::wstring blPath = Config::Get().GetFilePath(L"mall_blacklist.txt");
    std::ifstream blFile(blPath);
    if (blFile.is_open()) {
        std::string line;
        while (std::getline(blFile, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            m_blacklist.insert(line);
        }
        blFile.close();
    }

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

    LOG("[MallSpoofer] Loaded %zu blacklist traps and %zu whitelist items.\n", 
        m_blacklist.size(), m_whitelist.size());
}

bool MallSpoofer::IsTrapItem(const std::string& key) const {
    if (m_blacklist.find(key) != m_blacklist.end()) return true;

    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("cheat") != std::string::npos ||
        lower.find("honeypot") != std::string::npos ||
        lower.find("trap") != std::string::npos) {
        return true;
    }
    return false;
}

bool MallSpoofer::IsValidMallItemKey(const std::string& key) const {
    if (key.empty()) return false;
    for (char c : key) {
        if (c >= 'a' && c <= 'z') {
            // Dynamic honeypot tokens typically contain random lowercase letters
            return false;
        }
    }
    return true;
}

int __cdecl MallSpoofer::Hook_IsVIP(int player) {
    if (Config::Get().GetMallMode() > 0) {
        return 1;
    }
    return 0;
}

int __cdecl MallSpoofer::Hook_GetMapLevel(int player) {
    return Config::Get().GetMapLevel();
}

int __cdecl MallSpoofer::Hook_GetServerValueErrorCode(int player) {
    return 0;
}

int __cdecl MallSpoofer::Hook_HasMallItem(int player, DWORD key_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);
    int orig = 0;
    if (s_instance && s_instance->m_origHasMallItem) {
        orig = s_instance->m_origHasMallItem(player, key_ptr);
    }

    if (!keyStr || keyStr[0] == '\0' || strcmp(keyStr, "(null)") == 0) {
        return orig;
    }

    std::string key(keyStr);

    // 1. Static Trap Blacklist check
    if (s_instance && s_instance->IsTrapItem(key)) {
        LOG("[MallSpoofer] TRAP NEUTRALIZED: Item \"%s\" is a trap! Spoofed to 0 (Denied).\n", keyStr);
        LOG_SAVE("Player %d MALL ITEM: %s = %d (Spoofed to 0 [TRAP])\n", player, keyStr, orig);
        return 0;
    }

    // 2. Dynamic Honeypot Token filter check
    if (s_instance && !s_instance->IsValidMallItemKey(key)) {
        LOG("[MallSpoofer] DYNAMIC HONEYPOT BLOCKED: Item \"%s\" rejected. Spoofed to 0 (Denied).\n", keyStr);
        LOG_SAVE("Player %d MALL ITEM: %s = %d (Spoofed to 0 [HONEYPOT])\n", player, keyStr, orig);
        return 0;
    }

    int finalVal = orig;
    int mode = Config::Get().GetMallMode();

    if (mode == 2) {
        finalVal = 1; // 100% All Open
    } else if (mode == 3) {
        uint32_t h = HashItemKey(key);
        int bucket = static_cast<int>(h % 100);
        if (bucket < Config::Get().GetMallPercent()) {
            finalVal = 1;
        }
    } else if (mode == 1) {
        if (s_instance && s_instance->m_whitelist.find(key) != s_instance->m_whitelist.end()) {
            finalVal = 1;
        }
    }

    LOG("[MallSpoofer] Player: %d | Item: \"%s\" | Server: %d | Spoofed: %d\n", player, keyStr, orig, finalVal);
    LOG_SAVE("Player %d MALL ITEM: %s = %d (Spoofed to %d)\n", player, keyStr, orig, finalVal);
    return finalVal;
}

void MallSpoofer::Initialize() {
    ReloadLists();

    auto& reg = NativeRegistry::Get();
    auto& hook = HookEngine::Get();

    const NativeInfo* hasMall = reg.Find("DzAPI_Map_HasMallItem");
    if (hasMall && hasMall->funcPtr) {
        void* tramp = hook.InstallTrampoline("DzAPI_Map_HasMallItem", hasMall->funcPtr, Hook_HasMallItem, 5);
        m_origHasMallItem = reinterpret_cast<HasMallItem_t>(tramp);
    }

    const NativeInfo* isVip = reg.Find("DzAPI_Map_IsVIP");
    if (isVip && isVip->funcPtr) {
        hook.InstallDirectJmp("DzAPI_Map_IsVIP", isVip->funcPtr, Hook_IsVIP);
    }

    const NativeInfo* mapLvl = reg.Find("DzAPI_Map_GetMapLevel");
    if (mapLvl && mapLvl->funcPtr) {
        hook.InstallDirectJmp("DzAPI_Map_GetMapLevel", mapLvl->funcPtr, Hook_GetMapLevel);
    }

    const NativeInfo* errCode = reg.Find("DzAPI_Map_GetServerValueErrorCode");
    if (errCode && errCode->funcPtr) {
        hook.InstallDirectJmp("DzAPI_Map_GetServerValueErrorCode", errCode->funcPtr, Hook_GetServerValueErrorCode);
    }

    LOG("[MallSpoofer] Mall spoofing system initialized.\n");
}

} // namespace War3Dev
