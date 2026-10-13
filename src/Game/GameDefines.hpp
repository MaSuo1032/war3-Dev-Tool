#pragma once
#include <windows.h>
#include <cstdint>
#include "dz_registry.h"

namespace War3Dev {

constexpr uintptr_t WAR3_GAME_DLL_BASE = 0x6F000000;

// Correct Warcraft III 1.27a RegisterNative RVA
constexpr DWORD REGISTER_NATIVE_RVA    = 0x7E3710;
constexpr DWORD SELECTED_UNITS_RVA     = 0xBE4238;
constexpr DWORD THIS_GAME_RVA          = 0xBE40A8;

// Windows Messages used for main-thread marshaling
constexpr UINT WM_WAR3_INVOKE_NATIVE = WM_APP + 1;
constexpr UINT WM_WAR3_KILL_UNIT     = WM_APP + 2;
constexpr UINT WM_WAR3_SAVE_SERVER   = WM_APP + 3;

} // namespace War3Dev
