#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Wc3Memory.h"
#include "web_ui.h"
#include "ProcessInjector.h"

#pragma comment(lib, "ws2_32.lib")

namespace WebController {

inline HANDLE g_memProcess = nullptr;
inline uintptr_t g_memGameBase = 0;
inline DWORD g_memPid = 0;
inline ProcessMemory g_memInstance;
inline WC3Resolver g_memResolver(g_memInstance);
inline CRITICAL_SECTION g_memCs;
inline bool g_memCsInitialized = false;

inline bool g_godModeActive = false;
inline SOCKET g_httpServerSocket = INVALID_SOCKET;
inline HANDLE g_httpServerThread = nullptr;
inline bool g_httpServerRunning = false;
inline int g_httpServerPort = 8088;

inline void InitMemoryController() {
    if (!g_memCsInitialized) {
        InitializeCriticalSection(&g_memCs);
        g_memCsInitialized = true;
    }
}

inline void CleanupMemoryController() {
    if (g_memCsInitialized) {
        EnterCriticalSection(&g_memCs);
        if (g_memProcess) {
            CloseHandle(g_memProcess);
            g_memProcess = nullptr;
        }
        g_memGameBase = 0;
        g_memPid = 0;
        LeaveCriticalSection(&g_memCs);
        DeleteCriticalSection(&g_memCs);
        g_memCsInitialized = false;
    }
}

inline bool AttachToGame(DWORD targetPid = 0) {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (targetPid == 0) {
        targetPid = FindProcessId();
    }

    if (!targetPid) {
        if (g_memProcess) {
            CloseHandle(g_memProcess);
            g_memProcess = nullptr;
        }
        g_memGameBase = 0;
        g_memPid = 0;
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    // If already attached to this PID and handle is valid
    if (g_memProcess && g_memPid == targetPid && g_memGameBase != 0) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(g_memProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            LeaveCriticalSection(&g_memCs);
            return true;
        }
    }

    // Reattach
    if (g_memProcess) {
        CloseHandle(g_memProcess);
        g_memProcess = nullptr;
    }
    g_memGameBase = 0;
    g_memPid = targetPid;

    g_memGameBase = FindModuleBase(g_memPid, "Game.dll");
    if (!g_memGameBase) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    EnableDebugPrivilege();
    g_memProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, g_memPid);
    if (!g_memProcess) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    g_memInstance.setHandle(g_memProcess);
    g_memResolver.setGameBase(g_memGameBase);

    LeaveCriticalSection(&g_memCs);
    return true;
}

inline bool IsAttached() {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);
    bool ok = (g_memProcess != nullptr && g_memGameBase != 0);
    LeaveCriticalSection(&g_memCs);
    return ok;
}

inline bool ReadSelectedUnit(UnitInfo* outInfo) {
    if (!outInfo) return false;
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return false;
        }
    }

    uintptr_t unit = g_memResolver.getSelectedUnit();
    if (!unit) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    bool ok = g_memResolver.inspectUnit(unit, outInfo);
    LeaveCriticalSection(&g_memCs);
    return ok;
}

inline bool WriteUnitStat(const std::string& field, float value) {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return false;
        }
    }

    uintptr_t unit = g_memResolver.getSelectedUnit();
    if (!unit) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    UnitInfo info{};
    if (!g_memResolver.inspectUnit(unit, &info)) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    bool success = false;
    if (field == "hp" && info.hp.valid) success = g_memInstance.write(info.hp.address, value);
    else if (field == "maxHp" && info.maxHp.valid) success = g_memInstance.write(info.maxHp.address, value);
    else if (field == "hpRegen" && info.hpRegen.valid) success = g_memInstance.write(info.hpRegen.address, value);
    else if (field == "mp" && info.mp.valid) success = g_memInstance.write(info.mp.address, value);
    else if (field == "maxMp" && info.maxMp.valid) success = g_memInstance.write(info.maxMp.address, value);
    else if (field == "mpRegen" && info.mpRegen.valid) success = g_memInstance.write(info.mpRegen.address, value);
    else if (field == "strength" && info.strength.valid) success = g_memInstance.write(info.strength.address, static_cast<int32_t>(value));
    else if (field == "agility" && info.agility.valid) success = g_memInstance.write(info.agility.address, static_cast<int32_t>(value));
    else if (field == "intelligence" && info.intelligence.valid) success = g_memInstance.write(info.intelligence.address, static_cast<int32_t>(value));
    else if (field == "experience" && info.experience.valid) success = g_memInstance.write(info.experience.address, static_cast<int32_t>(value));
    else if (field == "defense" && info.defense.valid) success = g_memInstance.write(info.defense.address, value);
    else if (field == "moveSpeed" && info.moveSpeed.valid) success = g_memInstance.write(info.moveSpeed.address, value);
    else if (field == "a1_base1" && info.attack[0].base1.valid) success = g_memInstance.write(info.attack[0].base1.address, static_cast<int32_t>(value));
    else if (field == "a1_base2" && info.attack[0].base2.valid) success = g_memInstance.write(info.attack[0].base2.address, static_cast<int32_t>(value));
    else if (field == "a1_dice" && info.attack[0].dice.valid) success = g_memInstance.write(info.attack[0].dice.address, static_cast<int32_t>(value));
    else if (field == "a1_mult" && info.attack[0].multiplier.valid) success = g_memInstance.write(info.attack[0].multiplier.address, static_cast<int32_t>(value));
    else if (field == "a1_interval" && info.attack[0].interval.valid) success = g_memInstance.write(info.attack[0].interval.address, value);
    else if (field == "a1_range" && info.attack[0].range.valid) success = g_memInstance.write(info.attack[0].range.address, value);
    else if (field == "a2_base1" && info.attack[1].base1.valid) success = g_memInstance.write(info.attack[1].base1.address, static_cast<int32_t>(value));
    else if (field == "a2_base2" && info.attack[1].base2.valid) success = g_memInstance.write(info.attack[1].base2.address, static_cast<int32_t>(value));
    else if (field == "a2_dice" && info.attack[1].dice.valid) success = g_memInstance.write(info.attack[1].dice.address, static_cast<int32_t>(value));
    else if (field == "a2_mult" && info.attack[1].multiplier.valid) success = g_memInstance.write(info.attack[1].multiplier.address, static_cast<int32_t>(value));
    else if (field == "a2_interval" && info.attack[1].interval.valid) success = g_memInstance.write(info.attack[1].interval.address, value);
    else if (field == "a2_range" && info.attack[1].range.valid) success = g_memInstance.write(info.attack[1].range.address, value);

    LeaveCriticalSection(&g_memCs);
    return success;
}

inline bool WriteUnitItemCharges(int slot, int charges) {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return false;
        }
    }

    uintptr_t unit = g_memResolver.getSelectedUnit();
    if (!unit) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    std::vector<ItemInfo> items = g_memResolver.getItems(unit);
    bool success = false;
    for (const auto& item : items) {
        if (item.slot == slot && item.chargesAddress) {
            success = g_memInstance.write(item.chargesAddress, charges);
            break;
        }
    }

    LeaveCriticalSection(&g_memCs);
    return success;
}

inline bool ReadPlayerResources(int playerId, int* outGold, int* outLumber, uintptr_t* outGoldAddr = nullptr, uintptr_t* outLumberAddr = nullptr) {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return false;
        }
    }

    ResourceInfo info{};
    if (!g_memResolver.inspectResources(playerId, &info)) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    int32_t goldRaw = 0;
    int32_t lumberRaw = 0;
    bool goldOk = info.gold.valid && g_memInstance.read(info.gold.address, &goldRaw);
    bool lumberOk = info.lumber.valid && g_memInstance.read(info.lumber.address, &lumberRaw);

    if (outGold) *outGold = goldOk ? (goldRaw / 10) : 0;
    if (outLumber) *outLumber = lumberOk ? (lumberRaw / 10) : 0;
    if (outGoldAddr) *outGoldAddr = info.gold.address;
    if (outLumberAddr) *outLumberAddr = info.lumber.address;

    LeaveCriticalSection(&g_memCs);
    return (goldOk || lumberOk);
}

inline bool WritePlayerResource(int playerId, const std::string& type, int amount) {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return false;
        }
    }

    ResourceInfo resource{};
    if (playerId < WC3_127A::RESOURCE_MIN_PLAYER ||
        playerId > WC3_127A::RESOURCE_MAX_PLAYER ||
        !g_memResolver.inspectResources(playerId, &resource)) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    if (amount < 0 || amount > 214748364) {
        LeaveCriticalSection(&g_memCs);
        return false;
    }

    int32_t rawValue = amount * 10;
    bool success = false;
    if (type == "gold" && resource.gold.valid) {
        success = g_memInstance.write(resource.gold.address, rawValue);
    } else if (type == "lumber" && resource.lumber.valid) {
        success = g_memInstance.write(resource.lumber.address, rawValue);
    }

    LeaveCriticalSection(&g_memCs);
    return success;
}

// Convenient Action Helpers
inline bool HealSelectedUnit() {
    UnitInfo info{};
    if (!ReadSelectedUnit(&info)) return false;

    float maxHp = 0.0f, maxMp = 0.0f;
    if (info.maxHp.valid) g_memInstance.read(info.maxHp.address, &maxHp);
    if (info.maxMp.valid) g_memInstance.read(info.maxMp.address, &maxMp);

    bool ok = true;
    if (info.hp.valid && maxHp > 0.0f) {
        ok &= g_memInstance.write(info.hp.address, maxHp);
    }
    if (info.mp.valid && maxMp > 0.0f) {
        ok &= g_memInstance.write(info.mp.address, maxMp);
    }
    return ok;
}

inline bool SetSelectedUnitMaxSpeed() {
    return WriteUnitStat("moveSpeed", 522.0f);
}

inline bool AddSelectedUnitStats(int strDelta, int agiDelta, int intDelta) {
    UnitInfo info{};
    if (!ReadSelectedUnit(&info)) return false;

    bool ok = true;
    if (strDelta != 0 && info.strength.valid) {
        int32_t curStr = 0;
        if (g_memInstance.read(info.strength.address, &curStr)) {
            ok &= g_memInstance.write(info.strength.address, curStr + strDelta);
        }
    }
    if (agiDelta != 0 && info.agility.valid) {
        int32_t curAgi = 0;
        if (g_memInstance.read(info.agility.address, &curAgi)) {
            ok &= g_memInstance.write(info.agility.address, curAgi + agiDelta);
        }
    }
    if (intDelta != 0 && info.intelligence.valid) {
        int32_t curInt = 0;
        if (g_memInstance.read(info.intelligence.address, &curInt)) {
            ok &= g_memInstance.write(info.intelligence.address, curInt + intDelta);
        }
    }
    return ok;
}

inline bool AddPlayerGoldLumber(int playerId, int goldDelta, int lumberDelta) {
    int curGold = 0, curLumber = 0;
    if (!ReadPlayerResources(playerId, &curGold, &curLumber)) return false;

    bool ok = true;
    if (goldDelta != 0) {
        ok &= WritePlayerResource(playerId, "gold", curGold + goldDelta);
    }
    if (lumberDelta != 0) {
        ok &= WritePlayerResource(playerId, "lumber", curLumber + lumberDelta);
    }
    return ok;
}

inline void SetGodMode(bool enable) {
    g_godModeActive = enable;
}

inline bool IsGodModeActive() {
    return g_godModeActive;
}

inline void TickGodMode() {
    if (!g_godModeActive) return;

    InitMemoryController();
    EnterCriticalSection(&g_memCs);

    if (!g_memProcess || !g_memGameBase) {
        if (!AttachToGame()) {
            LeaveCriticalSection(&g_memCs);
            return;
        }
    }

    uintptr_t unit = g_memResolver.getSelectedUnit();
    if (unit) {
        UnitInfo info{};
        if (g_memResolver.inspectUnit(unit, &info)) {
            float godVal = 999999.0f;
            if (info.hp.valid) g_memInstance.write(info.hp.address, godVal);
            if (info.mp.valid) g_memInstance.write(info.mp.address, godVal);
        }
    }

    LeaveCriticalSection(&g_memCs);
}

// -------------------------------------------------------------
// Embedded HTTP Server (Serving web_ui.h & Web Controller APIs)
// -------------------------------------------------------------

inline std::string HandleApiHook() {
    bool ok = AttachToGame();
    std::ostringstream ss;
    if (ok) {
        ss << "{\"success\":true,\"pid\":" << g_memPid << ",\"gameDll\":" << g_memGameBase << "}";
    } else {
        ss << "{\"success\":false,\"message\":\"Could not locate war3.exe or Game.dll\"}";
    }
    return ss.str();
}

inline std::string HandleApiResources() {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);
    if (!g_memProcess || !g_memGameBase) {
        LeaveCriticalSection(&g_memCs);
        return "{\"success\":false,\"resources\":[]}";
    }

    std::ostringstream ss;
    ss << "{\"success\":true,\"resources\":[";

    bool first = true;
    for (int playerId = WC3_127A::RESOURCE_MIN_PLAYER;
         playerId <= WC3_127A::RESOURCE_MAX_PLAYER;
         ++playerId) {

        ResourceInfo info{};
        if (!g_memResolver.inspectResources(playerId, &info)) {
            continue;
        }

        int32_t goldRaw = 0;
        int32_t lumberRaw = 0;
        bool goldRead = info.gold.valid && g_memInstance.read(info.gold.address, &goldRaw);
        bool lumberRead = info.lumber.valid && g_memInstance.read(info.lumber.address, &lumberRaw);

        if (!first) ss << ",";
        first = false;

        int32_t gold = goldRead ? (goldRaw / 10) : 0;
        int32_t lumber = lumberRead ? (lumberRaw / 10) : 0;

        ss << "{"
           << "\"player\":" << playerId << ","
           << "\"gold\":" << gold << ","
           << "\"lumber\":" << lumber << ","
           << "\"goldRaw\":" << goldRaw << ","
           << "\"lumberRaw\":" << lumberRaw << ","
           << "\"goldAddress\":" << info.gold.address << ","
           << "\"lumberAddress\":" << info.lumber.address
           << "}";
    }

    ss << "]}";
    LeaveCriticalSection(&g_memCs);
    return ss.str();
}

inline std::string HandleApiUnit() {
    InitMemoryController();
    EnterCriticalSection(&g_memCs);
    if (!g_memProcess || !g_memGameBase) {
        LeaveCriticalSection(&g_memCs);
        return "{\"found\":false}";
    }

    uintptr_t unit = g_memResolver.getSelectedUnit();
    if (!unit) {
        LeaveCriticalSection(&g_memCs);
        return "{\"found\":false}";
    }

    UnitInfo info{};
    if (!g_memResolver.inspectUnit(unit, &info)) {
        LeaveCriticalSection(&g_memCs);
        return "{\"found\":false}";
    }

    float hp = 0.0f, maxHp = 0.0f, hpRegen = 0.0f;
    float mp = 0.0f, maxMp = 0.0f, mpRegen = 0.0f;
    int32_t str = 0, agi = 0, intel = 0, exp = 0;
    float def = 0.0f, speed = 0.0f;

    if (info.hp.valid) g_memInstance.read(info.hp.address, &hp);
    if (info.maxHp.valid) g_memInstance.read(info.maxHp.address, &maxHp);
    if (info.hpRegen.valid) g_memInstance.read(info.hpRegen.address, &hpRegen);

    if (info.mp.valid) g_memInstance.read(info.mp.address, &mp);
    if (info.maxMp.valid) g_memInstance.read(info.maxMp.address, &maxMp);
    if (info.mpRegen.valid) g_memInstance.read(info.mpRegen.address, &mpRegen);

    if (info.strength.valid) g_memInstance.read(info.strength.address, &str);
    if (info.agility.valid) g_memInstance.read(info.agility.address, &agi);
    if (info.intelligence.valid) g_memInstance.read(info.intelligence.address, &intel);
    if (info.experience.valid) g_memInstance.read(info.experience.address, &exp);

    if (info.defense.valid) g_memInstance.read(info.defense.address, &def);
    if (info.moveSpeed.valid) g_memInstance.read(info.moveSpeed.address, &speed);

    int32_t a1_b1 = 0, a1_b2 = 0, a1_dice = 0, a1_mult = 0;
    float a1_int = 0.0f, a1_rng = 0.0f;
    if (info.attack[0].base1.valid) g_memInstance.read(info.attack[0].base1.address, &a1_b1);
    if (info.attack[0].base2.valid) g_memInstance.read(info.attack[0].base2.address, &a1_b2);
    if (info.attack[0].dice.valid) g_memInstance.read(info.attack[0].dice.address, &a1_dice);
    if (info.attack[0].multiplier.valid) g_memInstance.read(info.attack[0].multiplier.address, &a1_mult);
    if (info.attack[0].interval.valid) g_memInstance.read(info.attack[0].interval.address, &a1_int);
    if (info.attack[0].range.valid) g_memInstance.read(info.attack[0].range.address, &a1_rng);

    int32_t a2_b1 = 0, a2_b2 = 0, a2_dice = 0, a2_mult = 0;
    float a2_int = 0.0f, a2_rng = 0.0f;
    if (info.attack[1].base1.valid) g_memInstance.read(info.attack[1].base1.address, &a2_b1);
    if (info.attack[1].base2.valid) g_memInstance.read(info.attack[1].base2.address, &a2_b2);
    if (info.attack[1].dice.valid) g_memInstance.read(info.attack[1].dice.address, &a2_dice);
    if (info.attack[1].multiplier.valid) g_memInstance.read(info.attack[1].multiplier.address, &a2_mult);
    if (info.attack[1].interval.valid) g_memInstance.read(info.attack[1].interval.address, &a2_int);
    if (info.attack[1].range.valid) g_memInstance.read(info.attack[1].range.address, &a2_rng);

    std::ostringstream ss;
    ss << "{\"found\":true,\"unit\":{"
       << "\"address\":" << unit << ","
       << "\"heroAttributes\":" << info.heroAttributes << ","
       << "\"hp\":" << hp << ",\"maxHp\":" << maxHp << ",\"hpRegen\":" << hpRegen << ","
       << "\"mp\":" << mp << ",\"maxMp\":" << maxMp << ",\"mpRegen\":" << mpRegen << ","
       << "\"strength\":" << str << ",\"agility\":" << agi << ",\"intelligence\":" << intel << ","
       << "\"experience\":" << exp << ",\"defense\":" << def << ",\"moveSpeed\":" << speed << ","
       << "\"a1_base1\":" << a1_b1 << ",\"a1_base2\":" << a1_b2 << ",\"a1_dice\":" << a1_dice << ","
       << "\"a1_multiplier\":" << a1_mult << ",\"a1_interval\":" << a1_int << ",\"a1_range\":" << a1_rng << ","
       << "\"a2_base1\":" << a2_b1 << ",\"a2_base2\":" << a2_b2 << ",\"a2_dice\":" << a2_dice << ","
       << "\"a2_multiplier\":" << a2_mult << ",\"a2_interval\":" << a2_int << ",\"a2_range\":" << a2_rng << ","
       << "\"items\":[";

    for (size_t i = 0; i < info.items.size(); ++i) {
        uint32_t rawName = 0;
        int32_t charges = 0;
        g_memInstance.read(info.items[i].nameAddress, &rawName);
        g_memInstance.read(info.items[i].chargesAddress, &charges);
        ss << "{\"slot\":" << info.items[i].slot << ",\"address\":" << info.items[i].currentItem
           << ",\"rawName\":" << rawName << ",\"charges\":" << charges << "}";
        if (i + 1 < info.items.size()) ss << ",";
    }

    ss << "]}}";
    LeaveCriticalSection(&g_memCs);
    return ss.str();
}

inline std::string HandleApiWrite(const std::string& body) {
    std::string field;
    float value = 0.0f;

    size_t fPos = body.find("\"field\":");
    if (fPos != std::string::npos) {
        size_t q1 = body.find("\"", fPos + 8);
        size_t q2 = body.find("\"", q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            field = body.substr(q1 + 1, q2 - q1 - 1);
        }
    }

    size_t vPos = body.find("\"value\":");
    if (vPos != std::string::npos) {
        std::string sub = body.substr(vPos + 8);
        value = static_cast<float>(atof(sub.c_str()));
    }

    if (field == "gold" || field == "lumber") {
        int playerId = 0;
        size_t pPos = body.find("\"player\":");
        if (pPos != std::string::npos) {
            playerId = atoi(body.substr(pPos + 9).c_str());
        }

        bool success = WritePlayerResource(playerId, field, static_cast<int>(value));
        std::ostringstream ss;
        ss << "{\"success\":" << (success ? "true" : "false") << "}";
        return ss.str();
    }

    bool success = WriteUnitStat(field, value);
    std::ostringstream ss;
    ss << "{\"success\":" << (success ? "true" : "false") << "}";
    return ss.str();
}

inline std::string HandleApiWriteItem(const std::string& body) {
    int slot = -1, charges = 0;

    size_t sPos = body.find("\"slot\":");
    if (sPos != std::string::npos) slot = atoi(body.substr(sPos + 7).c_str());

    size_t cPos = body.find("\"charges\":");
    if (cPos != std::string::npos) charges = atoi(body.substr(cPos + 10).c_str());

    bool success = WriteUnitItemCharges(slot, charges);
    std::ostringstream ss;
    ss << "{\"success\":" << (success ? "true" : "false") << "}";
    return ss.str();
}

inline DWORD WINAPI ClientHandlerThread(LPVOID param) {
    SOCKET clientSocket = (SOCKET)reinterpret_cast<uintptr_t>(param);
    char buffer[4096] = {0};
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        closesocket(clientSocket);
        return 0;
    }

    std::string req(buffer, bytesRead);
    std::string response;

    std::string method, path;
    std::istringstream reqStream(req);
    reqStream >> method >> path;

    if (method == "GET" && (path == "/" || path == "/index.html" || path.empty())) {
        std::string html(HTML_UI);
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: text/html; charset=utf-8\r\n"
           << "Content-Length: " << html.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << html;
        response = ss.str();
    } else if (method == "POST" && path == "/api/hook") {
        std::string json = HandleApiHook();
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Content-Length: " << json.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << json;
        response = ss.str();
    } else if (method == "GET" && path == "/api/unit") {
        std::string json = HandleApiUnit();
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Content-Length: " << json.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << json;
        response = ss.str();
    } else if (method == "GET" && path == "/api/resources") {
        std::string json = HandleApiResources();
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Content-Length: " << json.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << json;
        response = ss.str();
    } else if (method == "POST" && path == "/api/write") {
        size_t bPos = req.find("\r\n\r\n");
        std::string body = (bPos != std::string::npos) ? req.substr(bPos + 4) : "";
        std::string json = HandleApiWrite(body);
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Content-Length: " << json.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << json;
        response = ss.str();
    } else if (method == "POST" && path == "/api/write_item") {
        size_t bPos = req.find("\r\n\r\n");
        std::string body = (bPos != std::string::npos) ? req.substr(bPos + 4) : "";
        std::string json = HandleApiWriteItem(body);
        std::ostringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << "Content-Type: application/json\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Content-Length: " << json.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << json;
        response = ss.str();
    } else {
        std::string notFound = "404 Not Found";
        std::ostringstream ss;
        ss << "HTTP/1.1 404 Not Found\r\n"
           << "Content-Type: text/plain\r\n"
           << "Content-Length: " << notFound.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << notFound;
        response = ss.str();
    }

    send(clientSocket, response.c_str(), static_cast<int>(response.size()), 0);
    closesocket(clientSocket);
    return 0;
}

inline DWORD WINAPI HttpServerThread(LPVOID param) {
    int port = static_cast<int>(reinterpret_cast<uintptr_t>(param));
    if (port <= 0) port = 8088;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    g_httpServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_httpServerSocket == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(static_cast<u_short>(port));

    if (bind(g_httpServerSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(g_httpServerSocket);
        g_httpServerSocket = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    listen(g_httpServerSocket, 10);
    g_httpServerRunning = true;

    while (g_httpServerRunning) {
        SOCKET clientSocket = accept(g_httpServerSocket, nullptr, nullptr);
        if (clientSocket != INVALID_SOCKET) {
            HANDLE hThread = CreateThread(nullptr, 0, ClientHandlerThread, (LPVOID)(uintptr_t)clientSocket, 0, nullptr);
            if (hThread) {
                CloseHandle(hThread);
            } else {
                closesocket(clientSocket);
            }
        } else {
            break;
        }
    }

    if (g_httpServerSocket != INVALID_SOCKET) {
        closesocket(g_httpServerSocket);
        g_httpServerSocket = INVALID_SOCKET;
    }
    WSACleanup();
    g_httpServerRunning = false;
    return 0;
}

inline bool StartWebControllerServer(int port = 8088) {
    if (g_httpServerRunning) return true;
    g_httpServerPort = port;
    g_httpServerThread = CreateThread(nullptr, 0, HttpServerThread, (LPVOID)(uintptr_t)port, 0, nullptr);
    if (g_httpServerThread) {
        Sleep(100);
        return true;
    }
    return false;
}

inline void StopWebControllerServer() {
    g_httpServerRunning = false;
    if (g_httpServerSocket != INVALID_SOCKET) {
        closesocket(g_httpServerSocket);
        g_httpServerSocket = INVALID_SOCKET;
    }
    if (g_httpServerThread) {
        WaitForSingleObject(g_httpServerThread, 500);
        CloseHandle(g_httpServerThread);
        g_httpServerThread = nullptr;
    }
}

inline bool OpenWebControllerInBrowser(int port = 8088) {
    StartWebControllerServer(port);
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    HINSTANCE res = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(res) > 32;
}

} // namespace WebController
