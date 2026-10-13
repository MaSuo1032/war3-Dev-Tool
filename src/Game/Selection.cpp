#include "Selection.hpp"
#include "GameDefines.hpp"
#include "Common/Logger.hpp"

namespace War3Dev {

void Selection::KillSelectedUnit() {
    __try {
        DWORD selectedUnitList = *reinterpret_cast<DWORD*>(WAR3_GAME_DLL_BASE + SELECTED_UNITS_RVA);
        if (!selectedUnitList) return;

        DWORD playerId = *reinterpret_cast<DWORD*>(selectedUnitList + 0x28);
        if (playerId >= 16) return;

        DWORD player = *reinterpret_cast<DWORD*>(selectedUnitList + 0x58 + playerId * 4);
        if (!player) return;

        DWORD listRoot = *reinterpret_cast<DWORD*>(player + 0x34);
        if (!listRoot) return;

        DWORD listHead = *reinterpret_cast<DWORD*>(listRoot + 0x1F0);
        if (!listHead) return;

        DWORD listLength = *reinterpret_cast<DWORD*>(listRoot + 0x1F8);
        if (listLength == 0) return;

        DWORD firstNode = *reinterpret_cast<DWORD*>(listHead + 0x00);
        if (!firstNode) return;

        DWORD selectedUnit = *reinterpret_cast<DWORD*>(firstNode + 0x08);
        if (!selectedUnit) return;

        // Set Unit HP to 0.0f (Offset +0x130 is HP in 1.27a unit structure)
        *reinterpret_cast<float*>(selectedUnit + 0x130) = 0.0f;
        LOG("[Selection] KillSelectedUnit executed successfully on Unit 0x%X!\n", selectedUnit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("[Selection] Exception in KillSelectedUnit!\n");
    }
}

} // namespace War3Dev
