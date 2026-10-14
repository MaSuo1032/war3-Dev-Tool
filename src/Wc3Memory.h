#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <vector>
#include <string>

namespace WC3_127A {
    constexpr uintptr_t THIS_GAME_RVA        = 0xBE40A8;
    constexpr uintptr_t UNIT_LIST_RVA        = 0xBE4238;
    constexpr uintptr_t MOVE_SPEED_ADDRESS   = 0x5DF420;

    constexpr uintptr_t UNIT_ATTRIBUTES_OFF  = 0x1E8;
    constexpr uintptr_t HERO_ATTRIBUTES_OFF  = 0x1F0;
    constexpr uintptr_t ITEMS_LIST_OFF       = 0x1F8;
    constexpr uintptr_t MOVE_SPEED_OFF       = 0x1DC;

    constexpr uintptr_t PLAYER_ID_OFF        = 0x28;
    constexpr uintptr_t PLAYER_TABLE_OFF     = 0x58;
    constexpr uintptr_t UNIT_LIST_ROOT_OFF   = 0x34;

    constexpr uintptr_t LIST_HEAD_OFF        = 0x1F0;
    constexpr uintptr_t LIST_LENGTH_OFF      = 0x1F8;

    constexpr uintptr_t NODE_NEXT_OFF        = 0x00;
    constexpr uintptr_t NODE_UNIT_OFF        = 0x08;

    constexpr uintptr_t HERO_EXP_OFF         = 0x8C;
    constexpr uintptr_t HERO_STR_OFF         = 0x94;
    constexpr uintptr_t HERO_AGI_OFF         = 0xA8;
    constexpr uintptr_t HERO_INT_INDEX_OFF   = 0x84;
    constexpr uintptr_t HERO_INT_REF_OFF     = 0x88;

    constexpr uintptr_t UNIT_HP_INDEX_OFF    = 0xA0;
    constexpr uintptr_t UNIT_MP_INDEX_OFF    = 0xC0;
    constexpr uintptr_t UNIT_HP_REGEN_OFF    = 0xB0;
    constexpr uintptr_t UNIT_MP_REGEN_OFF    = 0xD4;

    constexpr uintptr_t UNIT_DEFENSE_OFF     = 0xE0;
    constexpr uintptr_t UNIT_DEF_TYPE_OFF    = 0xE4;

    constexpr uintptr_t ATK_RATE_OFF         = 0x1B0;
    constexpr uintptr_t ATK_RANGE_OFF        = 0x258;

    constexpr uintptr_t ATK_MULT_OFF         = 0x88;
    constexpr uintptr_t ATK_DICE_OFF         = 0x94;
    constexpr uintptr_t ATK_BASE1_OFF        = 0xA0;
    constexpr uintptr_t ATK_BASE2_OFF        = 0xAC;
    constexpr uintptr_t ATK_TYPE_OFF         = 0xF4;
    constexpr uintptr_t ATK_INTERVAL_OFF     = 0x158;

    constexpr uintptr_t GAME_TABLE_OFF       = 0x0C;
    constexpr uintptr_t GAME_REF_OFF         = 0x18;

    constexpr uintptr_t MOVE_NEXT_INDEX_OFF  = 0x24;
    constexpr uintptr_t MOVE_NEXT_END_OFF    = 0x28;
    constexpr uintptr_t MOVE_FN_PTR_OFF      = 0x2D4;
    constexpr uintptr_t MOVE_VALUE_OFF       = 0x70;

    constexpr uintptr_t ITEM_LIST_ENTRY_OFF  = 0x70;
    constexpr uintptr_t ITEM_SLOT_STRIDE     = 0x0C;
    constexpr uintptr_t ITEM_RAW_INVALID_OFF = 0x20;
    constexpr uintptr_t ITEM_CURRENT_OFF     = 0x54;
    constexpr uintptr_t ITEM_NAME_OFF        = 0x30;
    constexpr uintptr_t ITEM_CHARGES_OFF     = 0x84;

    constexpr uintptr_t RESOURCE_LUMBER_OFF  = 0x80;
    constexpr int RESOURCE_MIN_PLAYER        = 1;
    constexpr int RESOURCE_MAX_PLAYER        = 12;
    constexpr uintptr_t RESOURCE_BASES[13] = {
        0x0000,
        0x0190,
        0x1410,
        0x26A0,
        0x3920,
        0x4BB0,
        0x5E30,
        0x70C0,
        0x8350,
        0x95D0,
        0xA860,
        0xBAE0,
        0xCD70
    };
}

class ProcessMemory {
public:
    explicit ProcessMemory(HANDLE h = nullptr);

    void setHandle(HANDLE h);
    HANDLE getHandle() const;

    template <typename T>
    bool read(uintptr_t address, T* outVal) const {
        if (!h_ || !address || !outVal) return false;
        T value{};
        SIZE_T bytes = 0;
        if (!ReadProcessMemory(h_, reinterpret_cast<LPCVOID>(address), &value, sizeof(T), &bytes)) return false;
        if (bytes != sizeof(T)) return false;
        *outVal = value;
        return true;
    }

    template <typename T>
    bool write(uintptr_t address, const T& value) const {
        if (!h_ || !address) return false;
        SIZE_T bytes = 0;
        if (!WriteProcessMemory(h_, reinterpret_cast<LPVOID>(address), &value, sizeof(T), &bytes)) return false;
        return bytes == sizeof(T);
    }

    bool committed(uintptr_t address) const;

private:
    HANDLE h_{};
};

struct AddressValue {
    uintptr_t address{};
    bool valid{};
};

struct AttackInfo {
    AddressValue multiplier;
    AddressValue dice;
    AddressValue base1;
    AddressValue base2;
    AddressValue type;
    AddressValue interval;
    AddressValue range;
};

struct ItemInfo {
    int slot{};
    uintptr_t currentItem{};
    uintptr_t nameAddress{};
    uintptr_t chargesAddress{};
};

struct UnitInfo {
    uintptr_t unit{};
    uintptr_t unitAttributes{};
    uintptr_t heroAttributes{};

    AddressValue hp;
    AddressValue maxHp;
    AddressValue hpRegen;

    AddressValue mp;
    AddressValue maxMp;
    AddressValue mpRegen;

    AddressValue strength;
    AddressValue agility;
    AddressValue intelligence;
    AddressValue experience;

    AddressValue defense;
    AddressValue defenseType;

    AddressValue attackRate;
    AttackInfo attack[2];
    AddressValue moveSpeed;

    std::vector<ItemInfo> items;
};

struct ResourceInfo {
    int playerId{};
    AddressValue gold;
    AddressValue lumber;
};

class WC3Resolver {
public:
    WC3Resolver(ProcessMemory& mem, uintptr_t gameBase = 0);

    void setGameBase(uintptr_t base);
    uintptr_t getGameBase() const;

    uintptr_t getSelectedUnit() const;
    uintptr_t gameMemoryAddress(int32_t index, const int32_t* reference = nullptr) const;
    uintptr_t gameMemoryValueAddress(int32_t index, const int32_t* reference = nullptr) const;
    std::vector<ItemInfo> getItems(uintptr_t unit) const;
    bool inspectUnit(uintptr_t unit, UnitInfo* info) const;
    bool inspectResources(int playerId, ResourceInfo* info) const;

private:
    uintptr_t resolveMoveSpeed(uintptr_t unit, int32_t moveIndex) const;

    ProcessMemory& mem_;
    uintptr_t gameBase_{};
};

uintptr_t FindModuleBase(DWORD pid, const std::string& wanted);
