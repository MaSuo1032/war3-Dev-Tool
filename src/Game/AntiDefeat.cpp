#include "AntiDefeat.hpp"
#include "Common/Config.hpp"
#include "Common/Logger.hpp"
#include "Core/HookEngine.hpp"
#include "Jass/JassTypes.hpp"
#include "Jass/NativeRegistry.hpp"

namespace War3Dev {

static AntiDefeat* s_instance = nullptr;

AntiDefeat& AntiDefeat::Get() {
    static AntiDefeat instance;
    s_instance = &instance;
    return instance;
}

bool AntiDefeat::IsDefeatDialog(DWORD dialog) const {
    return m_defeatDialogs.find(dialog) != m_defeatDialogs.end();
}

void AntiDefeat::MarkDefeatDialog(DWORD dialog) {
    if (dialog) {
        m_defeatDialogs.insert(dialog);
    }
}

void __cdecl AntiDefeat::Hook_EndGame(DWORD showScores) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED EndGame(showScores: %u)\n", showScores);
        return;
    }
    if (s_instance && s_instance->m_origEndGame) {
        s_instance->m_origEndGame(showScores);
    }
}

void __cdecl AntiDefeat::Hook_RestartGame() {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED RestartGame()!\n");
        return;
    }
}

void __cdecl AntiDefeat::Hook_RemovePlayer(DWORD player, DWORD slotState) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED RemovePlayer(%u, %u)\n", player, slotState);
        return;
    }
    if (s_instance && s_instance->m_origRemovePlayer) {
        s_instance->m_origRemovePlayer(player, slotState);
    }
}

void __cdecl AntiDefeat::Hook_EnableUserControl(DWORD bEnable) {
    if (!bEnable && Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED EnableUserControl(false)! Kept player controls active.\n");
        return;
    }
    if (s_instance && s_instance->m_origEnableUserControl) {
        s_instance->m_origEnableUserControl(bEnable);
    }
}

DWORD __cdecl AntiDefeat::Hook_DialogAddQuitButton(DWORD dialog, DWORD bScoreScreen, DWORD str_ptr, DWORD hotkey) {
    if (dialog) {
        s_instance->MarkDefeatDialog(dialog);
        const char* s = JassTypes::ReadJassString(str_ptr);
        LOG("[AntiDefeat] Intercepted DialogAddQuitButton on Dialog 0x%X (Text: \"%s\") -> Marked Defeat Dialog!\n", dialog, s);
    }
    return 0; // Return dummy button handle
}

void __cdecl AntiDefeat::Hook_DialogSetMessage(DWORD dialog, DWORD str_ptr) {
    const char* msg = JassTypes::ReadJassString(str_ptr);
    if (dialog && msg) {
        std::string s(msg);
        if (s.find("敗") != std::string::npos ||
            s.find("Defeat") != std::string::npos ||
            s.find("defeat") != std::string::npos ||
            s.find("作弊") != std::string::npos ||
            s.find("Cheat") != std::string::npos ||
            s.find("cheat") != std::string::npos ||
            s.find("Fail") != std::string::npos ||
            s.find("fail") != std::string::npos ||
            s.find("失敗") != std::string::npos ||
            s.find("任务失败") != std::string::npos ||
            s.find("任務失敗") != std::string::npos) {
            s_instance->MarkDefeatDialog(dialog);
            LOG("[AntiDefeat] Detected Defeat Message on Dialog 0x%X (\"%s\") -> Marked Defeat Dialog!\n", dialog, msg);
        }
    }
    if (s_instance && s_instance->m_origDialogSetMessage) {
        s_instance->m_origDialogSetMessage(dialog, str_ptr);
    }
}

void __cdecl AntiDefeat::Hook_DialogDisplay(DWORD player, DWORD dialog, DWORD bShow) {
    if (bShow && Config::Get().GetPreventGameOver() > 0) {
        if (s_instance && s_instance->IsDefeatDialog(dialog)) {
            LOG("[AntiDefeat] BLOCKED Defeat DialogDisplay(Player: %u, Dialog: 0x%X)!\n", player, dialog);
            return;
        }
    }
    if (s_instance && s_instance->m_origDialogDisplay) {
        s_instance->m_origDialogDisplay(player, dialog, bShow);
    }
}

DWORD __cdecl AntiDefeat::Hook_CreateDefeatCondition() {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED CreateDefeatCondition() -> Returned dummy handle.\n");
        return 0;
    }
    if (s_instance && s_instance->m_origCreateDefeatCondition) {
        return s_instance->m_origCreateDefeatCondition();
    }
    return 0;
}

void __cdecl AntiDefeat::Hook_DefeatConditionSetDescription(DWORD cond, DWORD str_ptr) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED DefeatConditionSetDescription()!\n");
        return;
    }
    if (s_instance && s_instance->m_origDefeatConditionSetDesc) {
        s_instance->m_origDefeatConditionSetDesc(cond, str_ptr);
    }
}

void AntiDefeat::Initialize() {
    auto& reg = NativeRegistry::Get();
    auto& hook = HookEngine::Get();

    auto installHook = [&](const char* name, void* hookFunc, size_t stolen, auto& origMember) {
        const NativeInfo* info = reg.Find(name);
        if (info && info->funcPtr) {
            void* tramp = hook.InstallTrampoline(name, info->funcPtr, hookFunc, stolen);
            origMember = reinterpret_cast<std::decay_t<decltype(origMember)>>(tramp);
        } else {
            LOG("[-] AntiDefeat: Native '%s' not found for hooking!\n", name);
        }
    };

    installHook("EndGame", Hook_EndGame, 9, m_origEndGame);
    installHook("RemovePlayer", Hook_RemovePlayer, 6, m_origRemovePlayer);
    installHook("EnableUserControl", Hook_EnableUserControl, 5, m_origEnableUserControl);
    installHook("DialogSetMessage", Hook_DialogSetMessage, 6, m_origDialogSetMessage);
    installHook("DialogDisplay", Hook_DialogDisplay, 6, m_origDialogDisplay);
    installHook("CreateDefeatCondition", Hook_CreateDefeatCondition, 6, m_origCreateDefeatCondition);
    installHook("DefeatConditionSetDescription", Hook_DefeatConditionSetDescription, 6, m_origDefeatConditionSetDesc);

    const NativeInfo* restartInfo = reg.Find("RestartGame");
    if (restartInfo && restartInfo->funcPtr) {
        hook.InstallDirectJmp("RestartGame", restartInfo->funcPtr, Hook_RestartGame);
    }

    const NativeInfo* quitBtnInfo = reg.Find("DialogAddQuitButton");
    if (quitBtnInfo && quitBtnInfo->funcPtr) {
        hook.InstallDirectJmp("DialogAddQuitButton", quitBtnInfo->funcPtr, Hook_DialogAddQuitButton);
    }

    LOG("[AntiDefeat] Protection system initialized successfully.\n");
}

} // namespace War3Dev
