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

        DWORD hpIndex = *reinterpret_cast<DWORD*>(selectedUnit + 0xA0); // UNIT_HP_INDEX_OFF
        
        DWORD thisGame = *reinterpret_cast<DWORD*>(WAR3_GAME_DLL_BASE + THIS_GAME_RVA);
        if (!thisGame) return;
        DWORD gameMemory = *reinterpret_cast<DWORD*>(thisGame + 0x0C);
        if (!gameMemory) return;

        DWORD entryAddress = gameMemory + hpIndex * 8 + 4;
        DWORD address1 = *reinterpret_cast<DWORD*>(entryAddress);
        if (!address1) return;

        float* hpValue = reinterpret_cast<float*>(address1 + 0x78);
        *hpValue = 0.0f; // INSTAKILL
        LOG("[Selection] Instantly killed selected unit (0x%X)!\n", selectedUnit);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("[Selection] Exception in KillSelectedUnit!\n");
    }
}

} // namespace War3Dev
