#include "Wc3Memory.h"
#include <tlhelp32.h>

ProcessMemory::ProcessMemory(HANDLE h) : h_(h) {}

void ProcessMemory::setHandle(HANDLE h) { h_ = h; }
HANDLE ProcessMemory::getHandle() const { return h_; }

bool ProcessMemory::committed(uintptr_t address) const {
    if (!h_ || !address) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQueryEx(h_, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) return false;
    return mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_NOACCESS) && !(mbi.Protect & PAGE_GUARD);
}

WC3Resolver::WC3Resolver(ProcessMemory& mem, uintptr_t gameBase)
    : mem_(mem), gameBase_(gameBase) {}

void WC3Resolver::setGameBase(uintptr_t base) { gameBase_ = base; }
uintptr_t WC3Resolver::getGameBase() const { return gameBase_; }

uintptr_t WC3Resolver::getSelectedUnit() const {
    if (!gameBase_) return 0;
    uint32_t selectedUnitList = 0;
    if (!mem_.read(gameBase_ + WC3_127A::UNIT_LIST_RVA, &selectedUnitList) || !selectedUnitList) return 0;

    uint16_t playerId = 0;
    if (!mem_.read(static_cast<uintptr_t>(selectedUnitList) + WC3_127A::PLAYER_ID_OFF, &playerId) || playerId >= 16) return 0;

    uint32_t player = 0;
    if (!mem_.read(static_cast<uintptr_t>(selectedUnitList) + WC3_127A::PLAYER_TABLE_OFF + static_cast<uintptr_t>(playerId) * 4, &player) || !player) return 0;

    uint32_t root = 0;
    if (!mem_.read(static_cast<uintptr_t>(player) + WC3_127A::UNIT_LIST_ROOT_OFF, &root) || !root) return 0;

    uint32_t head = 0;
    uint32_t length = 0;
    if (!mem_.read(static_cast<uintptr_t>(root) + WC3_127A::LIST_HEAD_OFF, &head) ||
        !mem_.read(static_cast<uintptr_t>(root) + WC3_127A::LIST_LENGTH_OFF, &length)) return 0;

    if (!head || !length || length > 64) return 0;

    uint32_t unit = 0;
    if (mem_.read(head + WC3_127A::NODE_UNIT_OFF, &unit) && unit && mem_.committed(unit)) {
        return unit;
    }
    return 0;
}

uintptr_t WC3Resolver::gameMemoryAddress(int32_t index, const int32_t* reference) const {
    if (index < 0 || !gameBase_) return 0;
    uint32_t thisGame = 0;
    if (!mem_.read(gameBase_ + WC3_127A::THIS_GAME_RVA, &thisGame) || !thisGame) return 0;

    uint32_t gameMemory = 0;
    if (!mem_.read(static_cast<uintptr_t>(thisGame) + WC3_127A::GAME_TABLE_OFF, &gameMemory) || !gameMemory) return 0;

    uintptr_t entryAddress = static_cast<uintptr_t>(gameMemory) + static_cast<uintptr_t>(index) * 8ULL + 4ULL;
    uint32_t address1 = 0;
    if (!mem_.read(entryAddress, &address1) || !address1) return 0;

    if (reference) {
        int32_t ref = 0;
        if (!mem_.read(static_cast<uintptr_t>(address1) + WC3_127A::GAME_REF_OFF, &ref) || ref != *reference) return 0;
    }
    return static_cast<uintptr_t>(address1);
}

uintptr_t WC3Resolver::gameMemoryValueAddress(int32_t index, const int32_t* reference) const {
    uintptr_t a = gameMemoryAddress(index, reference);
    if (!a) return 0;
    return a + 0x78;
}

std::vector<ItemInfo> WC3Resolver::getItems(uintptr_t unit) const {
    std::vector<ItemInfo> items;
    uint32_t list = 0;
    if (!mem_.read(unit + WC3_127A::ITEMS_LIST_OFF, &list) || !list) return items;

    for (int slot = 0; slot < 6; ++slot) {
        uintptr_t entry = static_cast<uintptr_t>(list) + static_cast<uintptr_t>(WC3_127A::ITEM_SLOT_STRIDE * slot) + WC3_127A::ITEM_LIST_ENTRY_OFF;
        int32_t index = 0;
        if (!mem_.read(entry, &index) || index <= 0) continue;

        uintptr_t raw = gameMemoryAddress(index);
        if (!raw) continue;

        uint32_t invalid = 0;
        if (!mem_.read(raw + WC3_127A::ITEM_RAW_INVALID_OFF, &invalid) || invalid != 0) continue;

        uint32_t current = 0;
        if (!mem_.read(raw + WC3_127A::ITEM_CURRENT_OFF, &current) || !current) continue;

        items.push_back({
            slot,
            static_cast<uintptr_t>(current),
            static_cast<uintptr_t>(current) + WC3_127A::ITEM_NAME_OFF,
            static_cast<uintptr_t>(current) + WC3_127A::ITEM_CHARGES_OFF
        });
    }
    return items;
}

bool WC3Resolver::inspectResources(int playerId, ResourceInfo* info) const {
    if (!info || !gameBase_) return false;
    *info = ResourceInfo{};

    if (playerId < WC3_127A::RESOURCE_MIN_PLAYER ||
        playerId > WC3_127A::RESOURCE_MAX_PLAYER) {
        return false;
    }

    uintptr_t gameMemoryEntry = gameMemoryAddress(1);
    if (!gameMemoryEntry) return false;

    uintptr_t upperAddress = gameMemoryEntry & static_cast<uintptr_t>(0xFFFF0000u);
    uintptr_t playerBase = WC3_127A::RESOURCE_BASES[playerId];

    uintptr_t goldAddress = upperAddress + playerBase;
    uintptr_t lumberAddress = goldAddress + WC3_127A::RESOURCE_LUMBER_OFF;

    if (!mem_.committed(goldAddress) || !mem_.committed(lumberAddress)) {
        return false;
    }

    info->playerId = playerId;
    info->gold = {goldAddress, true};
    info->lumber = {lumberAddress, true};
    return true;
}

bool WC3Resolver::inspectUnit(uintptr_t unit, UnitInfo* info) const {
    if (!unit || !info || !mem_.committed(unit)) return false;

    *info = UnitInfo{};
    info->unit = unit;

    uint32_t unitAttr = 0;
    if (mem_.read(unit + WC3_127A::UNIT_ATTRIBUTES_OFF, &unitAttr) && unitAttr) info->unitAttributes = unitAttr;

    uint32_t heroAttr = 0;
    if (mem_.read(unit + WC3_127A::HERO_ATTRIBUTES_OFF, &heroAttr) && heroAttr) info->heroAttributes = heroAttr;

    int32_t hpIndex = 0;
    if (mem_.read(unit + WC3_127A::UNIT_HP_INDEX_OFF, &hpIndex)) {
        uintptr_t hpMax = gameMemoryAddress(hpIndex);
        if (hpMax) {
            uintptr_t maxAddr = hpMax + 0x84;
            info->maxHp = {maxAddr, true};
            info->hp = {maxAddr - 0x0C, true};
        }
    }
    info->hpRegen = {unit + WC3_127A::UNIT_HP_REGEN_OFF, true};

    int32_t mpIndex = 0;
    if (mem_.read(unit + WC3_127A::UNIT_MP_INDEX_OFF, &mpIndex)) {
        uintptr_t mpMax = gameMemoryAddress(mpIndex);
        if (mpMax) {
            uintptr_t maxAddr = mpMax + 0x84;
            info->maxMp = {maxAddr, true};
            info->mp = {maxAddr - 0x0C, true};
        }
    }
    info->mpRegen = {unit + WC3_127A::UNIT_MP_REGEN_OFF, true};

    if (info->heroAttributes) {
        info->experience = {info->heroAttributes + WC3_127A::HERO_EXP_OFF, true};
        info->strength = {info->heroAttributes + WC3_127A::HERO_STR_OFF, true};
        info->agility = {info->heroAttributes + WC3_127A::HERO_AGI_OFF, true};

        int32_t intIndex = 0, intRef = 0;
        if (mem_.read(info->heroAttributes + WC3_127A::HERO_INT_INDEX_OFF, &intIndex) &&
            mem_.read(info->heroAttributes + WC3_127A::HERO_INT_REF_OFF, &intRef)) {
            uintptr_t intAddress = gameMemoryValueAddress(intIndex, &intRef);
            if (intAddress) info->intelligence = {intAddress, true};
        }
    }

    info->defense = {unit + WC3_127A::UNIT_DEFENSE_OFF, true};
    info->defenseType = {unit + WC3_127A::UNIT_DEF_TYPE_OFF, true};

    if (info->unitAttributes) {
        info->attackRate = {info->unitAttributes + WC3_127A::ATK_RATE_OFF, true};

        for (int i = 0; i < 2; ++i) {
            auto& a = info->attack[i];
            a.multiplier = {info->unitAttributes + WC3_127A::ATK_MULT_OFF + static_cast<uintptr_t>(i * 4), true};
            a.dice = {info->unitAttributes + WC3_127A::ATK_DICE_OFF + static_cast<uintptr_t>(i * 4), true};
            a.base1 = {info->unitAttributes + WC3_127A::ATK_BASE1_OFF + static_cast<uintptr_t>(i * 4), true};
            a.base2 = {info->unitAttributes + WC3_127A::ATK_BASE2_OFF + static_cast<uintptr_t>(i * 4), true};
            a.type = {info->unitAttributes + WC3_127A::ATK_TYPE_OFF + static_cast<uintptr_t>(i * 4), true};
            a.interval = {info->unitAttributes + WC3_127A::ATK_INTERVAL_OFF + static_cast<uintptr_t>(i * 8), true};
            a.range = {info->unitAttributes + WC3_127A::ATK_RANGE_OFF + static_cast<uintptr_t>(i * 8), true};
        }
    }

    int32_t moveIndex = 0;
    if (mem_.read(unit + WC3_127A::MOVE_SPEED_OFF, &moveIndex)) {
        uintptr_t move = resolveMoveSpeed(unit, moveIndex);
        if (move) info->moveSpeed = {move, true};
    }

    info->items = getItems(unit);
    return true;
}

uintptr_t WC3Resolver::resolveMoveSpeed(uintptr_t unit, int32_t moveIndex) const {
    uint32_t thisGame = 0;
    if (!mem_.read(gameBase_ + WC3_127A::THIS_GAME_RVA, &thisGame) || !thisGame) return 0;
    uint32_t gameMemory = 0;
    if (!mem_.read(static_cast<uintptr_t>(thisGame) + WC3_127A::GAME_TABLE_OFF, &gameMemory) || !gameMemory) return 0;

    int32_t index = moveIndex;
    for (int depth = 0; depth < 32 && index > 0; ++depth) {
        uintptr_t address = static_cast<uintptr_t>(gameMemory) + static_cast<uintptr_t>(index) * 8ULL + 4ULL;
        uint32_t address3 = 0;
        if (!mem_.read(address, &address3) || !address3) return 0;

        int32_t zero = 0, nextIndex = 0, endIndex = 0;
        if (!mem_.read(static_cast<uintptr_t>(address3) + 0x20, &zero)) return 0;
        mem_.read(static_cast<uintptr_t>(address3) + 0x24, &nextIndex);
        mem_.read(static_cast<uintptr_t>(address3) + 0x28, &endIndex);

        if (zero == 0) {
            uint32_t parameter = 0;
            if (!mem_.read(static_cast<uintptr_t>(address3) + 0x54, &parameter) || !parameter) return 0;
            uint32_t fnObject = 0;
            if (!mem_.read(static_cast<uintptr_t>(parameter), &fnObject)) return 0;
            uint32_t fn = 0;
            if (mem_.read(static_cast<uintptr_t>(fnObject) + WC3_127A::MOVE_FN_PTR_OFF, &fn) &&
                fn == gameBase_ + WC3_127A::MOVE_SPEED_ADDRESS) {
                return static_cast<uintptr_t>(parameter) + WC3_127A::MOVE_VALUE_OFF;
            }
        }

        if (nextIndex <= 0 || endIndex <= 0) return 0;
        uint32_t parameter = 0;
        if (!mem_.read(static_cast<uintptr_t>(address3) + 0x54, &parameter) || !parameter) return 0;
        int32_t next = 0;
        if (!mem_.read(static_cast<uintptr_t>(parameter) + WC3_127A::MOVE_NEXT_INDEX_OFF, &next) || next <= 0) return 0;
        index = next;
    }
    return 0;
}

uintptr_t FindModuleBase(DWORD pid, const std::string& wanted) {
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    uintptr_t result = 0;
    std::wstring wWanted(wanted.begin(), wanted.end());
    if (Module32FirstW(snap, &me)) {
        do {
            if (lstrcmpiW(me.szModule, wWanted.c_str()) == 0) {
                result = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return result;
}
