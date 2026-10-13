#pragma once
#include <windows.h>
#include <cstdint>

namespace War3Dev {

constexpr uintptr_t WAR3_GAME_DLL_BASE = 0x6F000000;
constexpr DWORD REGISTER_NATIVE_RVA    = 0x3BC8E0;
constexpr DWORD SELECTED_UNITS_RVA     = 0xBE4238;

// Windows Messages used for main-thread marshaling
constexpr UINT WM_WAR3_INVOKE_NATIVE = WM_APP + 1;
constexpr UINT WM_WAR3_KILL_UNIT     = WM_APP + 2;
constexpr UINT WM_WAR3_SAVE_SERVER   = WM_APP + 3;

struct NativeEntryDef {
    const char* name;
    const char* sig;
    DWORD rva;
};

// Blizzard Game.dll 1.27a Static Native Table
static const NativeEntryDef g_blizzardNatives[] = {
    { "EndGame", "(B)V", 0x1e0100 },
    { "RestartGame", "(B)V", 0x1f31e0 },
    { "RemovePlayer", "(Hplayer;Hplayerslotstate;)V", 0x1f2db0 },
    { "DialogDisplay", "(Hplayer;Hdialog;B)V", 0x1dfb70 },
    { "DialogAddQuitButton", "(Hdialog;BSI)Hbutton;", 0x1df9f0 },
    { "DialogSetMessage", "(Hdialog;S)V", 0x1dfbe0 },
    { "CreateDefeatCondition", "()Hdefeatcondition;", 0x1ddc00 },
    { "DefeatConditionSetDescription", "(Hdefeatcondition;S)V", 0x1df2d0 },
    { "EnableUserControl", "(B)V", 0x1e0030 },
};
static const size_t g_blizzardNativesCount = sizeof(g_blizzardNatives) / sizeof(g_blizzardNatives[0]);

// War3Plugin.dll Static DzAPI Table
static const NativeEntryDef g_dzApiNatives[] = {
    { "DzAPI_Map_SaveServerValue", "(Hplayer;SS)B", 0x14050 },
    { "DzAPI_Map_GetServerValue", "(Hplayer;S)S", 0x14010 },
    { "DzAPI_Map_HasMallItem", "(Hplayer;S)B", 0x13EB0 },
    { "DzAPI_Map_Stat_SetStat", "(Hplayer;SS)V", 0x14200 },
    { "DzAPI_Map_GetServerValueErrorCode", "(Hplayer;)I", 0x14100 },
    { "DzAPI_Map_IsVIP", "(Hplayer;)B", 0x13F00 },
    { "DzAPI_Map_GetMapLevel", "(Hplayer;)I", 0x13F30 },
};
static const size_t g_dzApiNativesCount = sizeof(g_dzApiNatives) / sizeof(g_dzApiNatives[0]);

} // namespace War3Dev
