#include "SaveManager.hpp"
#include "Common/Logger.hpp"
#include "Core/HookEngine.hpp"
#include "Jass/JassTypes.hpp"
#include "Jass/NativeRegistry.hpp"

namespace War3Dev {

static SaveManager* s_instance = nullptr;

SaveManager& SaveManager::Get() {
    static SaveManager instance;
    s_instance = &instance;
    return instance;
}

int __cdecl SaveManager::Hook_SaveServerValue(int player, DWORD key_ptr, DWORD value_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);
    const char* valStr = JassTypes::ReadJassString(value_ptr);

    LOG("[SaveServerValue] Player: %d | Key: \"%s\" | Value: \"%s\"\n", player, keyStr, valStr);
    LOG_SAVE("Player %d SAVED: %s = %s\n", player, keyStr, valStr);

    if (s_instance && s_instance->m_origSaveServerValue) {
        return s_instance->m_origSaveServerValue(player, key_ptr, value_ptr);
    }
    return 1;
}

DWORD __cdecl SaveManager::Hook_GetServerValue(int player, DWORD key_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);

    DWORD result = 0;
    if (s_instance && s_instance->m_origGetServerValue) {
        result = s_instance->m_origGetServerValue(player, key_ptr);
    }

    std::string valStr = JassTypes::DecodeServerString(result);
    LOG("[GetServerValue] Player: %d | Key: \"%s\" | Value(From Server): \"%s\"\n", player, keyStr, valStr.c_str());
    LOG_SAVE("Player %d LOADED: %s = %s\n", player, keyStr, valStr.c_str());

    return result;
}

void __cdecl SaveManager::Hook_Stat_SetStat(int player, DWORD key_ptr, DWORD value_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);
    const char* valStr = JassTypes::ReadJassString(value_ptr);

    LOG("[Stat_SetStat] Player: %d | Key: \"%s\" | Value: \"%s\"\n", player, keyStr, valStr);
    LOG_SAVE("Player %d SET_STAT: %s = %s\n", player, keyStr, valStr);

    if (s_instance && s_instance->m_origStatSetStat) {
        s_instance->m_origStatSetStat(player, key_ptr, value_ptr);
    }
}

void SaveManager::ExecuteManualSave(const SavePayload* payload) {
    if (!payload) return;

    DWORD fakeKey = JassTypes::CreateFakeJassString(payload->key);
    DWORD fakeVal = JassTypes::CreateFakeJassString(payload->val);

    LOG("[SaveServerValue] Manual IPC Save -> Player: %d | Key: \"%s\" | Value: \"%s\"\n",
        payload->player, payload->key, payload->val);

    if (m_origSaveServerValue) {
        int ret = m_origSaveServerValue(payload->player, fakeKey, fakeVal);
        LOG("  -> DzAPI_Map_SaveServerValue returned: %d\n", ret);
    } else {
        LOG("  -> ERROR: m_origSaveServerValue is NULL! Native was not hooked.\n");
    }

    LOG_SAVE("Player %d MANUAL_SAVED: %s = %s\n", payload->player, payload->key, payload->val);

    JassTypes::FreeFakeJassString(fakeKey);
    JassTypes::FreeFakeJassString(fakeVal);
}

void SaveManager::Initialize() {
    auto& reg = NativeRegistry::Get();
    auto& hook = HookEngine::Get();

    const NativeInfo* saveInfo = reg.Find("DzAPI_Map_SaveServerValue");
    if (saveInfo && saveInfo->funcPtr) {
        void* tramp = hook.InstallTrampoline("DzAPI_Map_SaveServerValue", saveInfo->funcPtr, Hook_SaveServerValue, 7);
        m_origSaveServerValue = reinterpret_cast<SaveServerValue_t>(tramp);
    }

    const NativeInfo* getInfo = reg.Find("DzAPI_Map_GetServerValue");
    if (getInfo && getInfo->funcPtr) {
        void* tramp = hook.InstallTrampoline("DzAPI_Map_GetServerValue", getInfo->funcPtr, Hook_GetServerValue, 5);
        m_origGetServerValue = reinterpret_cast<GetServerValue_t>(tramp);
    }

    const NativeInfo* statInfo = reg.Find("DzAPI_Map_Stat_SetStat");
    if (statInfo && statInfo->funcPtr) {
        void* tramp = hook.InstallTrampoline("DzAPI_Map_Stat_SetStat", statInfo->funcPtr, Hook_Stat_SetStat, 5);
        m_origStatSetStat = reinterpret_cast<StatSetStat_t>(tramp);
    }

    LOG("[SaveManager] Save monitoring & injection hooks initialized.\n");
}

} // namespace War3Dev
