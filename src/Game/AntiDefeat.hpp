#pragma once
#include <windows.h>
#include <unordered_set>
#include <cstdint>

namespace War3Dev {

class AntiDefeat {
public:
    static AntiDefeat& Get();

    void Initialize();

    bool IsDefeatDialog(DWORD dialog) const;
    void MarkDefeatDialog(DWORD dialog);

private:
    AntiDefeat() = default;

    // Hooked callbacks
    static void __cdecl Hook_EndGame(DWORD showScores);
    static void __cdecl Hook_RestartGame();
    static void __cdecl Hook_RemovePlayer(DWORD player, DWORD slotState);
    static void __cdecl Hook_EnableUserControl(DWORD bEnable);
    static void __cdecl Hook_DialogSetMessage(DWORD dialog, DWORD str_ptr);
    static DWORD __cdecl Hook_DialogAddQuitButton(DWORD dialog, DWORD bScoreScreen, DWORD str_ptr, DWORD hotkey);
    static void __cdecl Hook_DialogDisplay(DWORD player, DWORD dialog, DWORD bShow);
    static DWORD __cdecl Hook_CreateDefeatCondition();
    static void __cdecl Hook_DefeatConditionSetDescription(DWORD cond, DWORD str_ptr);

    std::unordered_set<DWORD> m_defeatDialogs;

    // Trampolines
    typedef void(__cdecl *EndGame_t)(DWORD);
    typedef void(__cdecl *RemovePlayer_t)(DWORD, DWORD);
    typedef void(__cdecl *EnableUserControl_t)(DWORD);
    typedef void(__cdecl *DialogSetMessage_t)(DWORD, DWORD);
    typedef void(__cdecl *DialogDisplay_t)(DWORD, DWORD, DWORD);
    typedef DWORD(__cdecl *CreateDefeatCondition_t)();
    typedef void(__cdecl *DefeatConditionSetDesc_t)(DWORD, DWORD);

    EndGame_t m_origEndGame = nullptr;
    RemovePlayer_t m_origRemovePlayer = nullptr;
    EnableUserControl_t m_origEnableUserControl = nullptr;
    DialogSetMessage_t m_origDialogSetMessage = nullptr;
    DialogDisplay_t m_origDialogDisplay = nullptr;
    CreateDefeatCondition_t m_origCreateDefeatCondition = nullptr;
    DefeatConditionSetDesc_t m_origDefeatConditionSetDesc = nullptr;
};

} // namespace War3Dev
