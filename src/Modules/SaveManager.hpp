#pragma once
#include <windows.h>
#include <string>

namespace War3Dev {

struct SavePayload {
    int player;
    char key[256];
    char val[512];
};

class SaveManager {
public:
    static SaveManager& Get();

    void Initialize();
    void ExecuteManualSave(const SavePayload* payload);

private:
    SaveManager() = default;

    static int __cdecl Hook_SaveServerValue(int player, DWORD key_ptr, DWORD value_ptr);
    static DWORD __cdecl Hook_GetServerValue(int player, DWORD key_ptr);
    static void __cdecl Hook_Stat_SetStat(int player, DWORD key_ptr, DWORD value_ptr);

    typedef int(__cdecl *SaveServerValue_t)(int, DWORD, DWORD);
    typedef DWORD(__cdecl *GetServerValue_t)(int, DWORD);
    typedef void(__cdecl *StatSetStat_t)(int, DWORD, DWORD);

    SaveServerValue_t m_origSaveServerValue = nullptr;
    GetServerValue_t m_origGetServerValue = nullptr;
    StatSetStat_t m_origStatSetStat = nullptr;
};

} // namespace War3Dev
