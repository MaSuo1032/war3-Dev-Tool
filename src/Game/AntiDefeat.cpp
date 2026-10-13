#include "AntiDefeat.hpp"
#include "Common/Config.hpp"
#include "Common/Logger.hpp"
#include "Core/HookEngine.hpp"
#include "Jass/NativeRegistry.hpp"

namespace War3Dev {

static AntiDefeat* s_instance = nullptr;

AntiDefeat& AntiDefeat::Get() {
    static AntiDefeat instance;
    s_instance = &instance;
    return instance;
}

void __cdecl AntiDefeat::Hook_EndGame(DWORD showScores) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED EndGame(showScores: %u)! Prevented map from ending game.\n", showScores);
        return;
    }
    PatchHook* pHook = HookEngine::Get().GetHook("EndGame");
    if (pHook) {
        pHook->Unhook();
        typedef void(__cdecl *FuncType)(DWORD);
        reinterpret_cast<FuncType>(pHook->GetTarget())(showScores);
        pHook->Rehook();
    }
}

void __cdecl AntiDefeat::Hook_RemovePlayer(DWORD player, DWORD slotState) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED RemovePlayer(%u, %u)!\n", player, slotState);
        return;
    }
    PatchHook* pHook = HookEngine::Get().GetHook("RemovePlayer");
    if (pHook) {
        pHook->Unhook();
        typedef void(__cdecl *FuncType)(DWORD, DWORD);
        reinterpret_cast<FuncType>(pHook->GetTarget())(player, slotState);
        pHook->Rehook();
    }
}

void __cdecl AntiDefeat::Hook_DzPlatform_GameEnd(DWORD player, DWORD result) {
    if (Config::Get().GetPreventGameOver() > 0) {
        LOG("[AntiDefeat] BLOCKED DzPlatform_GameEnd(player: %u, result: %u)!\n", player, result);
        return;
    }
    PatchHook* pHook = HookEngine::Get().GetHook("DzPlatform_GameEnd");
    if (pHook) {
        pHook->Unhook();
        typedef void(__cdecl *FuncType)(DWORD, DWORD);
        reinterpret_cast<FuncType>(pHook->GetTarget())(player, result);
        pHook->Rehook();
    }
}

void AntiDefeat::Initialize() {
    auto& reg = NativeRegistry::Get();
    auto& hook = HookEngine::Get();

    const NativeInfo* endGame = reg.Find("EndGame");
    if (endGame && endGame->funcPtr) {
        hook.CreatePatchHook("EndGame", endGame->funcPtr, Hook_EndGame);
    }

    const NativeInfo* removePl = reg.Find("RemovePlayer");
    if (removePl && removePl->funcPtr) {
        hook.CreatePatchHook("RemovePlayer", removePl->funcPtr, Hook_RemovePlayer);
    }

    const NativeInfo* dzEnd = reg.Find("DzPlatform_GameEnd");
    if (dzEnd && dzEnd->funcPtr) {
        hook.CreatePatchHook("DzPlatform_GameEnd", dzEnd->funcPtr, Hook_DzPlatform_GameEnd);
    }

    LOG("[AntiDefeat] Safe defeat protection hooks initialized.\n");
}

} // namespace War3Dev
