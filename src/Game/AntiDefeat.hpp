#pragma once
#include <windows.h>
#include <cstdint>

namespace War3Dev {

class AntiDefeat {
public:
    static AntiDefeat& Get();

    void Initialize();

private:
    AntiDefeat() = default;

    static void __cdecl Hook_EndGame(DWORD showScores);
    static void __cdecl Hook_RemovePlayer(DWORD player, DWORD slotState);
    static void __cdecl Hook_DzPlatform_GameEnd(DWORD player, DWORD result);
};

} // namespace War3Dev
