#include "SaveManager.hpp"
#include "Common/Logger.hpp"
#include "Core/HookEngine.hpp"
#include "Jass/JassTypes.hpp"
#include "Jass/NativeRegistry.hpp"

namespace War3Dev {

static SaveManager* s_instance = nullptr;
static PatchHook* s_hookSave = nullptr;
static PatchHook* s_hookGet = nullptr;
static PatchHook* s_hookStat = nullptr;

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

    int result = 0;
    if (s_hookSave) {
        s_hookSave->Unhook();
        typedef int(__cdecl *FuncType)(int, DWORD, DWORD);
        result = reinterpret_cast<FuncType>(s_hookSave->GetTarget())(player, key_ptr, value_ptr);
        s_hookSave->Rehook();
    }
    return result;
}

DWORD __cdecl SaveManager::Hook_GetServerValue(int player, DWORD key_ptr) {
    const char* keyStr = JassTypes::ReadJassString(key_ptr);

    DWORD result = 0;
    if (s_hookGet) {
        s_hookGet->Unhook();
        typedef DWORD(__cdecl *FuncType)(int, DWORD);
        result = reinterpret_cast<FuncType>(s_hookGet->GetTarget())(player, key_ptr);
        s_hookGet->Rehook();
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

    if (s_hookStat) {
        s_hookStat->Unhook();
        typedef void(__cdecl *FuncType)(int, DWORD, DWORD);
        reinterpret_cast<FuncType>(s_hookStat->GetTarget())(player, key_ptr, value_ptr);
        s_hookStat->Rehook();
    }
}

void SaveManager::ExecuteManualSave(const SavePayload* payload) {
    if (!payload) return;

    DWORD fakeKey = JassTypes::CreateFakeJassString(payload->key);
    DWORD fakeVal = JassTypes::CreateFakeJassString(payload->val);

    LOG("[SaveServerValue] Manual IPC Save -> Player: %d | Key: \"%s\" | Value: \"%s\"\n",
        payload->player, payload->key, payload->val);

    if (s_hookSave) {
        s_hookSave->Unhook();
        typedef int(__cdecl *FuncType)(int, DWORD, DWORD);
        int ret = reinterpret_cast<FuncType>(s_hookSave->GetTarget())(payload->player, fakeKey, fakeVal);
        s_hookSave->Rehook();
        LOG("  -> DzAPI_Map_SaveServerValue returned: %d\n", ret);
    } else {
        LOG("  -> ERROR: DzAPI_Map_SaveServerValue is not hooked!\n");
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
        s_hookSave = hook.CreatePatchHook("DzAPI_Map_SaveServerValue", saveInfo->funcPtr, Hook_SaveServerValue);
    }

    const NativeInfo* getInfo = reg.Find("DzAPI_Map_GetServerValue");
    if (getInfo && getInfo->funcPtr) {
        s_hookGet = hook.CreatePatchHook("DzAPI_Map_GetServerValue", getInfo->funcPtr, Hook_GetServerValue);
    }

    const NativeInfo* statInfo = reg.Find("DzAPI_Map_Stat_SetStat");
    if (statInfo && statInfo->funcPtr) {
        s_hookStat = hook.CreatePatchHook("DzAPI_Map_Stat_SetStat", statInfo->funcPtr, Hook_Stat_SetStat);
    }

    LOG("[SaveManager] Save monitoring & injection hooks initialized.\n");
}

} // namespace War3Dev
