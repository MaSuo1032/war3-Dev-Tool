#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <fstream>
#include <sstream>
#include <functional>
#include "dz_registry.h"

HMODULE g_hModule = NULL;
wchar_t g_DllDirectory[MAX_PATH] = {0};


const DWORD GAME_DLL_BASE = 0x6F000000;
const DWORD REGISTER_NATIVE_RVA = 0x7E3710;

typedef void(__fastcall *RegisterNative_t)(void* func_ptr, const char* name, const char* signature);
RegisterNative_t OriginalRegisterNative = nullptr;

BYTE OriginalBytes[5];
void* HookTarget = nullptr;

struct NativeEntry {
    char name[64];
    char sig[64];
    void* func_ptr;
};
NativeEntry g_natives[2048];
int g_nativeCount = 0;

HWND g_gameHwnd = NULL;
WNDPROC OriginalWndProc = NULL;
HANDLE g_activePipe = INVALID_HANDLE_VALUE;

void ConsoleLog(const char* msg) {
    OutputDebugStringA(msg);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteConsoleA(hConsole, msg, (DWORD)strlen(msg), &w, NULL);
    }
    
    // Real-time bidirectional streaming to connected DevTool UI / CLI
    if (g_activePipe != INVALID_HANDLE_VALUE) {
        DWORD bw;
        if (!WriteFile(g_activePipe, msg, (DWORD)strlen(msg), &bw, NULL)) {
            g_activePipe = INVALID_HANDLE_VALUE;
        }
    }
    
    // Also save to a log file in the game directory
    FILE* fp = nullptr;
    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, MAX_PATH, L"%s\\dztrace.log", g_DllDirectory);
    if (_wfopen_s(&fp, logPath, L"a") == 0 && fp) {
        fputs(msg, fp);
        fclose(fp);
    }
}


std::unordered_set<std::string> g_mallWhitelist;

void LoadMallWhitelist() {
    g_mallWhitelist.clear();
    wchar_t mallPath[MAX_PATH];
    swprintf_s(mallPath, MAX_PATH, L"%s\\mall_spoof.txt", g_DllDirectory);
    std::ifstream file(mallPath);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            size_t last = line.find_last_not_of(" \n\r\t");
            if (last != std::string::npos) {
                line.erase(last + 1);
            }
            g_mallWhitelist.insert(line);
        }
    }
    
    char log[256];
    sprintf_s(log, sizeof(log), "[MallWhitelist] Loaded %zu spoofable items from mall_spoof.txt\n", g_mallWhitelist.size());
    ConsoleLog(log);
}

std::unordered_set<std::string> g_mallBlacklist;

void LoadMallBlacklist() {
    g_mallBlacklist.clear();
    g_mallBlacklist.insert("cheat");
    g_mallBlacklist.insert("test");
    g_mallBlacklist.insert("trap");
    g_mallBlacklist.insert("BAN_CHECK");
    g_mallBlacklist.insert("TEST_MALL_ITEM");

    wchar_t blPath[MAX_PATH];
    swprintf_s(blPath, MAX_PATH, L"%s\\mall_blacklist.txt", g_DllDirectory);
    std::ifstream file(blPath);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            size_t last = line.find_last_not_of(" \n\r\t");
            if (last != std::string::npos) {
                line.erase(last + 1);
            }
            g_mallBlacklist.insert(line);
        }
    }
    
    char log[256];
    sprintf_s(log, sizeof(log), "[MallBlacklist] Loaded %zu trap items from blacklist\n", g_mallBlacklist.size());
    ConsoleLog(log);
}

struct InvokeData {
    void* func_ptr;
    const char* sig;
    const char* arg1;
    const char* arg2;
    const char* arg3;
};

// ==========================================
// DzTrace (Dynamic Native Logger)
// ==========================================
void* g_thunkBlock = nullptr;

void InitThunks() {
    if (!g_thunkBlock) {
        g_thunkBlock = VirtualAlloc(NULL, 2048 * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    }
}

void __cdecl GenericDzTraceHandler(int index, DWORD* orig_esp) {
    if (index >= 0 && index < g_nativeCount) {
        DWORD arg1 = orig_esp[10];
        DWORD arg2 = orig_esp[11];
        DWORD arg3 = orig_esp[12];
        
        char parsedArgs[256] = "";
        
        // Heuristic to detect and parse JASS strings automatically!
        auto tryParseString = [](DWORD ptr, char* out, size_t maxLen) -> bool {
            __try {
                if (ptr > 0x10000 && ptr < 0x7FFFFFFF) {
                    DWORD stringData = *(DWORD*)(ptr + 8);
                    if (stringData > 0x10000 && stringData < 0x7FFFFFFF) {
                        char* chars = *(char**)(stringData + 0x1C);
                        if (chars && (DWORD)chars > 0x10000 && (DWORD)chars < 0x7FFFFFFF) {
                            strncpy_s(out, maxLen, chars, maxLen - 1);
                            return true;
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            return false;
        };
        
        char str1[100] = "";
        char str2[100] = "";
        bool hasStr1 = tryParseString(arg2, str1, sizeof(str1));
        bool hasStr2 = tryParseString(arg3, str2, sizeof(str2));
        
        if (hasStr1 && hasStr2) {
            sprintf_s(parsedArgs, sizeof(parsedArgs), " | Str: \"%s\", \"%s\"", str1, str2);
        } else if (hasStr1) {
            sprintf_s(parsedArgs, sizeof(parsedArgs), " | Str: \"%s\"", str1);
        }
        
        char log[512];
        sprintf_s(log, sizeof(log), "[DzTrace] %s (Arg1: %d, Arg2: 0x%X, Arg3: 0x%X)%s\n", 
                  g_natives[index].name, arg1, arg2, arg3, parsedArgs);
        ConsoleLog(log);
    }
}

void* CreateThunk(int index, void* original_func) {
    InitThunks();
    BYTE* thunk = (BYTE*)g_thunkBlock + (index * 32);
    
    thunk[0] = 0x60; // pushad
    thunk[1] = 0x9C; // pushfd
    thunk[2] = 0x89; thunk[3] = 0xE0; // mov eax, esp
    thunk[4] = 0x83; thunk[5] = 0xE4; thunk[6] = 0xF0; // and esp, 0xFFFFFFF0
    thunk[7] = 0x83; thunk[8] = 0xEC; thunk[9] = 0x08; // sub esp, 8
    thunk[10] = 0x50; // push eax
    thunk[11] = 0x68; 
    *(DWORD*)(&thunk[12]) = (DWORD)index; // push index
    
    thunk[16] = 0xE8;
    DWORD relCall = (DWORD)GenericDzTraceHandler - (DWORD)(&thunk[16] + 5);
    *(DWORD*)(&thunk[17]) = relCall; // call
    
    thunk[21] = 0x89; thunk[22] = 0xC4; // mov esp, eax
    thunk[23] = 0x9D; // popfd
    thunk[24] = 0x61; // popad
    
    thunk[25] = 0xE9;
    DWORD relJmp = (DWORD)original_func - (DWORD)(&thunk[25] + 5);
    *(DWORD*)(&thunk[26]) = relJmp; // jmp
    
    return thunk;
}

// ==========================================
// Native API Spoofer (VIP / Level / Mall)
// ==========================================
int g_MallMode = 2; // 0 = Off, 1 = Whitelist, 2 = All, 3 = Percentage
int g_MallPercent = 30; // 1-100%
int g_MapLevel = 99;
int g_PreventGameOver = 1; // 1 = Block Defeat, 0 = Off
int g_PlatformVIP = 2; // 0 = Normal, 1 = VIP, 2 = Super VIP (Platform Rewards)

int __cdecl Spoof_True() { return 1; }
int __cdecl Spoof_LevelDynamic() { return g_MapLevel; }
int __cdecl Spoof_PlatformVIP() { return g_PlatformVIP; }


// ==========================================
// Trampoline Hooks (Monitor Only)
// ==========================================
struct HookData {
    void* targetPtr;
    BYTE originalBytes[5];
};

HookData hkSave;
HookData hkGet;
HookData hkSetStat;
HookData hkHasMallItem;
HookData hkGameEnd;

void RemoveTrampoline(HookData& data) {
    if (!data.targetPtr) return;
    DWORD oldProtect;
    VirtualProtect(data.targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(data.targetPtr, data.originalBytes, 5);
    VirtualProtect(data.targetPtr, 5, oldProtect, &oldProtect);
}

void RestoreTrampoline(HookData& data, void* hookFunc) {
    if (!data.targetPtr) return;
    DWORD oldProtect;
    VirtualProtect(data.targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    DWORD relAddr = ((DWORD)hookFunc - (DWORD)data.targetPtr) - 5;
    memcpy(&jmp[1], &relAddr, 4);
    memcpy(data.targetPtr, jmp, 5);
    VirtualProtect(data.targetPtr, 5, oldProtect, &oldProtect);
}

void InstallTrampolineHook(const char* nativeName, void* hookFunc, HookData& outData) {
    void* targetPtr = nullptr;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, nativeName) == 0) {
            targetPtr = g_natives[i].func_ptr;
            break;
        }
    }
    
    if (targetPtr) {
        outData.targetPtr = targetPtr;
        DWORD oldProtect;
        VirtualProtect(targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(outData.originalBytes, targetPtr, 5); // Save original bytes
        VirtualProtect(targetPtr, 5, oldProtect, &oldProtect);
        
        RestoreTrampoline(outData, hookFunc); // Apply hook
        
        char log[256];
        sprintf_s(log, sizeof(log), "[Monitor] Hooked %s -> Success!\n", nativeName);
        ConsoleLog(log);
    }
}

int __cdecl Monitor_SaveServerValue(int player, DWORD key_ptr, DWORD value_ptr) {
    char log[256];
    const char* keyStr = "(null)";
    const char* valStr = "(null)";
    
    __try {
        if (key_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(key_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) keyStr = chars;
            }
        }
        if (value_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(value_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) valStr = chars;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    sprintf_s(log, sizeof(log), "[SaveServerValue] Player: %d | Key: \"%s\" | Value: \"%s\"\n", player, keyStr, valStr);
    ConsoleLog(log);
    
    FILE* fp = nullptr;
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
        fprintf(fp, "Player %d SAVED: %s = %s\n", player, keyStr, valStr);
        fclose(fp);
    }

    // Call Original
    RemoveTrampoline(hkSave);
    typedef int(__cdecl *FuncType)(int, DWORD, DWORD);
    int result = ((FuncType)hkSave.targetPtr)(player, key_ptr, value_ptr);
    RestoreTrampoline(hkSave, (void*)Monitor_SaveServerValue);
    return result;
}

void __cdecl Monitor_Stat_SetStat(int player, DWORD key_ptr, DWORD value_ptr) {
    char log[256];
    const char* keyStr = "(null)";
    const char* valStr = "(null)";
    
    __try {
        if (key_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(key_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) keyStr = chars;
            }
        }
        if (value_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(value_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) valStr = chars;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    sprintf_s(log, sizeof(log), "[Stat_SetStat] Player: %d | Key: \"%s\" | Value: \"%s\"\n", player, keyStr, valStr);
    ConsoleLog(log);
    
    FILE* fp = nullptr;
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
        fprintf(fp, "Player %d SET_STAT: %s = %s\n", player, keyStr, valStr);
        fclose(fp);
    }

    // Call Original
    RemoveTrampoline(hkSetStat);
    typedef void(__cdecl *FuncType)(int, DWORD, DWORD);
    ((FuncType)hkSetStat.targetPtr)(player, key_ptr, value_ptr);
    RestoreTrampoline(hkSetStat, (void*)Monitor_Stat_SetStat);
}

DWORD __cdecl Monitor_GetServerValue(int player, DWORD key_ptr) {
    const char* keyStr = "(null)";
    __try {
        if (key_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(key_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) keyStr = chars;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Call Original FIRST to get the official server's answer
    RemoveTrampoline(hkGet);
    typedef DWORD(__cdecl *FuncType)(int, DWORD);
    DWORD result = ((FuncType)hkGet.targetPtr)(player, key_ptr);
    RestoreTrampoline(hkGet, (void*)Monitor_GetServerValue);

    // Decode the returned string
    const char* valStr = "(null/empty)";
    char idBuf[64];
    __try {
        if (result > 0x10000) {
            DWORD stringData = *(DWORD*)(result + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars && (DWORD)chars > 0x10000) {
                    valStr = chars;
                }
            }
        } else if (result > 0) {
            sprintf_s(idBuf, sizeof(idBuf), "<StringID: %u>", result);
            valStr = idBuf;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (result > 0 && result <= 0x10000) {
            sprintf_s(idBuf, sizeof(idBuf), "<StringID: %u>", result);
            valStr = idBuf;
        }
    }

    char log[512];
    sprintf_s(log, sizeof(log), "[GetServerValue] Player: %d | Key: \"%s\" | Value(From Server): \"%s\"\n", player, keyStr, valStr);
    ConsoleLog(log);
    
    FILE* fp = nullptr;
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
        fprintf(fp, "Player %d LOADED: %s = %s\n", player, keyStr, valStr);
        fclose(fp);
    }

    return result;
}

const char* SafeReadJassString(DWORD key_ptr) {
    __try {
        if (key_ptr > 0x10000) {
            DWORD stringData = *(DWORD*)(key_ptr + 8);
            if (stringData > 0x10000) {
                char* chars = *(char**)(stringData + 0x1C);
                if (chars) return chars;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return "(null)";
}

int __cdecl Monitor_HasMallItem(int player, DWORD key_ptr) {
    const char* keyStr = SafeReadJassString(key_ptr);

    // Call Original FIRST
    RemoveTrampoline(hkHasMallItem);
    typedef int(__cdecl *FuncType)(int, DWORD);
    int originalResult = ((FuncType)hkHasMallItem.targetPtr)(player, key_ptr);
    RestoreTrampoline(hkHasMallItem, (void*)Monitor_HasMallItem);

    // 1. Trap / Honeypot Blacklist check
    if (keyStr && strcmp(keyStr, "(null)") != 0) {
        std::string itemKey(keyStr);
        if (g_mallBlacklist.find(itemKey) != g_mallBlacklist.end()) {
            char log[256];
            sprintf_s(log, sizeof(log), "[Trap Neutralized] Item \"%s\" is in blacklist! Spoofed to 0 (Denied).\n", keyStr);
            ConsoleLog(log);
            return 0;
        }
    }

    // 2. Spoof logic
    extern int g_MallMode;
    extern int g_MallPercent;
    int finalResult = originalResult;
    
    if (g_MallMode == 2) {
        finalResult = 1; // Spoof ALL items
    } else if (g_MallMode == 3) {
        // Percentage mode
        if (keyStr) {
            uint32_t hash = 2166136261u;
            for (const char* p = keyStr; *p; ++p) {
                hash ^= (uint8_t)*p;
                hash *= 16777619u;
            }
            if ((int)(hash % 100) < g_MallPercent) {
                finalResult = 1;
            }
        }
    } else if (g_MallMode == 1) {
        // Whitelist mode
        if (!g_mallWhitelist.empty() && keyStr) {
            if (g_mallWhitelist.find(keyStr) != g_mallWhitelist.end()) {
                finalResult = 1;
            }
        }
    }

    char log[512];
    sprintf_s(log, sizeof(log), "[HasMallItem] Player: %d | Item: \"%s\" | Server: %d | Spoofed: %d\n", player, keyStr, originalResult, finalResult);
    ConsoleLog(log);
    
    FILE* fp = nullptr;
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
        fprintf(fp, "Player %d MALL ITEM: %s = %d (Spoofed to %d)\n", player, keyStr, originalResult, finalResult);
        fclose(fp);
    }

    return finalResult;
}

void __cdecl Monitor_DzPlatform_GameEnd(DWORD player, DWORD result) {
    extern int g_PreventGameOver;
    if (g_PreventGameOver > 0) {
        char log[256];
        sprintf_s(log, sizeof(log), "[AntiDefeat] BLOCKED DzPlatform_GameEnd(player: %u, result: %u)! Prevented map defeat.\n", player, result);
        ConsoleLog(log);
        return;
    }
    RemoveTrampoline(hkGameEnd);
    typedef void(__cdecl *FuncType)(DWORD, DWORD);
    if (hkGameEnd.targetPtr) {
        ((FuncType)hkGameEnd.targetPtr)(player, result);
    }
    RestoreTrampoline(hkGameEnd, (void*)Monitor_DzPlatform_GameEnd);
}

void InstallSpoofHook(const char* nativeName, void* spoofFunc) {
    void* targetPtr = nullptr;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, nativeName) == 0) {
            targetPtr = g_natives[i].func_ptr;
            break;
        }
    }
    
    if (targetPtr) {
        DWORD oldProtect;
        VirtualProtect(targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
        DWORD relAddr = ((DWORD)spoofFunc - (DWORD)targetPtr) - 5;
        memcpy(&jmp[1], &relAddr, 4);
        memcpy(targetPtr, jmp, 5);
        VirtualProtect(targetPtr, 5, oldProtect, &oldProtect);
        
        char log[256];
        sprintf_s(log, sizeof(log), "[Spoofer] Hooked %s -> Success!\n", nativeName);
        ConsoleLog(log);
    } else {
        char log[256];
        sprintf_s(log, sizeof(log), "[Spoofer] Failed to hook %s (Not found)\n", nativeName);
        ConsoleLog(log);
    }
}
// ==========================================
// Fake JassString Generator for Native Invoker
// ==========================================
struct FakeJassStringData {
    char pad[0x1C];
    char* chars;
};

struct FakeJassString {
    char pad[0x08];
    FakeJassStringData* data;
};

DWORD CreateFakeJassString(const char* str) {
    FakeJassStringData* data = new FakeJassStringData();
    memset(data, 0, sizeof(FakeJassStringData));
    
    char* strCopy = new char[strlen(str) + 1];
    strcpy_s(strCopy, strlen(str) + 1, str);
    data->chars = strCopy;
    
    FakeJassString* jStr = new FakeJassString();
    memset(jStr, 0, sizeof(FakeJassString));
    jStr->data = data;
    
    return (DWORD)jStr;
}

struct SaveData {
    int player;
    char key[256];
    char val[512];
};

void FreeFakeJassString(DWORD jassStr) {
    if (!jassStr) return;
    FakeJassString* jStr = (FakeJassString*)jassStr;
    if (jStr->data) {
        if (jStr->data->chars) {
            delete[] jStr->data->chars;
        }
        delete jStr->data;
    }
    delete jStr;
}

// =========================================================================
// Advanced Command Registry & Dispatcher (Command Pattern Architecture)
// =========================================================================
typedef void (*CommandHandler)(const std::string& args);
std::unordered_map<std::string, CommandHandler> g_commandMap;

void RegisterCommand(const std::string& name, CommandHandler handler) {
    g_commandMap[name] = handler;
}

DWORD GetTargetUnit() {
    DWORD gameBase = 0x6F000000;
    DWORD targetUnit = 0;
    
    // 1. Try selected unit first (CGameWar3 + 0x58 -> CPlayerWar3 -> selection list)
    DWORD* pGame = (DWORD*)(gameBase + 0xBE4238);
    if (pGame && *pGame) {
        DWORD pGameWar3 = *pGame;
        DWORD playerId = *(DWORD*)(pGameWar3 + 0x28);
        if (playerId < 16) {
            DWORD player = *(DWORD*)(pGameWar3 + 0x58 + playerId * 4);
            if (player) {
                DWORD listRoot = *(DWORD*)(player + 0x34);
                if (listRoot) {
                    DWORD listHead = *(DWORD*)(listRoot + 0x1F0);
                    if (listHead) {
                        DWORD listLength = *(DWORD*)(listRoot + 0x1F8);
                        if (listLength > 0) {
                            DWORD firstNode = *(DWORD*)(listHead + 0x00);
                            if (firstNode) {
                                targetUnit = *(DWORD*)(firstNode + 0x08);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 2. Try unit under mouse cursor (CGameUI + 0x3BC -> +0x244)
    if (!targetUnit) {
        DWORD* pUI = (DWORD*)(gameBase + 0xBE6350);
        if (pUI && *pUI) {
            DWORD worldFrame = *(DWORD*)(*pUI + 0x3BC);
            if (worldFrame) {
                targetUnit = *(DWORD*)(worldFrame + 0x244);
            }
        }
    }
    return targetUnit;
}

// -------------------------------------------------------------------------
// Standalone Modular Action Handlers
// -------------------------------------------------------------------------

void Cmd_Kill(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD gameBase = 0x6F000000;
    DWORD hpIndex = *(DWORD*)(targetUnit + 0xA0);
    DWORD thisGame = *(DWORD*)(gameBase + 0xBE40A8);
    if (thisGame) {
        DWORD gameMemory = *(DWORD*)(thisGame + 0x0C);
        if (gameMemory) {
            DWORD entryAddress = gameMemory + hpIndex * 8 + 4;
            DWORD address1 = *(DWORD*)entryAddress;
            if (address1) {
                float* hpValue = (float*)(address1 + 0x78);
                *hpValue = 0.0f;
                ConsoleLog("[HeroAction] Instantly killed target unit!\n");
            }
        }
    }
}

void Cmd_Heal(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD gameBase = 0x6F000000;
    DWORD hpIndex = *(DWORD*)(targetUnit + 0xA0);
    DWORD thisGame = *(DWORD*)(gameBase + 0xBE40A8);
    if (thisGame) {
        DWORD gameMemory = *(DWORD*)(thisGame + 0x0C);
        if (gameMemory) {
            DWORD entryAddress = gameMemory + hpIndex * 8 + 4;
            DWORD address1 = *(DWORD*)entryAddress;
            if (address1) {
                float maxHp = *(float*)(address1 + 0x74);
                float* hpValue = (float*)(address1 + 0x78);
                *hpValue = (maxHp > 0.0f) ? maxHp : 999999.0f;
                ConsoleLog("[HeroAction] Restored target unit HP to maximum!\n");
            }
        }
    }
}

void Cmd_Teleport(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[Teleport] No unit selected or hovered under cursor!\n");
        return;
    }
    HMODULE hPlugin = GetModuleHandleA("War3Plugin.dll");
    if (!hPlugin) {
        ConsoleLog("[Teleport] War3Plugin.dll not ready!\n");
        return;
    }
    float mouseX = *(float*)((BYTE*)hPlugin + 0x2656D8);
    float mouseY = *(float*)((BYTE*)hPlugin + 0x2656DC);
    if (mouseX == 0.0f && mouseY == 0.0f) {
        ConsoleLog("[Teleport] Mouse cursor is outside map or terrain coords not ready.\n");
        return;
    }
    DWORD gameBase = 0x6F000000;
    DWORD fnSetCoord = gameBase + 0x635DE0;
    __try {
        __asm {
            mov ecx, targetUnit
            push 0
            push -1
            push 0
            push 0
            push 0
            push 0
            push -1
            push 1
            push 1
            lea eax, mouseY
            push eax
            lea eax, mouseX
            push eax
            call fnSetCoord
        }
        char log[128];
        sprintf_s(log, sizeof(log), "[Teleport] Teleported unit to mouse position: (X: %.1f, Y: %.1f)!\n", mouseX, mouseY);
        ConsoleLog(log);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[Teleport] Exception caught during CWidget::SetCoord.\n");
    }
}

void Cmd_God(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[GodMode] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD gameBase = 0x6F000000;
    // 1. Call Game.dll CGameState::SetUnitInvulnerable(BOOL flag, CUnit* unit) @ 0x210CB0
    DWORD pGameWar3 = *(DWORD*)(gameBase + 0xBE4238);
    if (pGameWar3) {
        DWORD gameState = *(DWORD*)(pGameWar3 + 0x1C);
        if (gameState) {
            typedef void(__thiscall *SetUnitInvul_t)(DWORD thisPtr, BOOL flag, DWORD unit);
            SetUnitInvul_t fnSetInvul = (SetUnitInvul_t)(gameBase + 0x210CB0);
            fnSetInvul(gameState, TRUE, targetUnit);
        }
    }
    // 2. Maximize and lock HP to 999,999,999.0f
    DWORD hpIndex = *(DWORD*)(targetUnit + 0xA0);
    DWORD thisGame = *(DWORD*)(gameBase + 0xBE40A8);
    if (thisGame) {
        DWORD gameMemory = *(DWORD*)(thisGame + 0x0C);
        if (gameMemory) {
            DWORD entryAddress = gameMemory + hpIndex * 8 + 4;
            DWORD address1 = *(DWORD*)entryAddress;
            if (address1) {
                float* maxHp = (float*)(address1 + 0x74);
                float* hpValue = (float*)(address1 + 0x78);
                *maxHp = 999999999.0f;
                *hpValue = 999999999.0f;
            }
        }
    }
    ConsoleLog("[HeroAction] Target unit is now officially INVULNERABLE (God Mode Active)!\n");
}

void Cmd_AddSP(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD heroAbility = *(DWORD*)(targetUnit + 0x1F0);
    if (!heroAbility) {
        ConsoleLog("[HeroAction] Target unit is not a Hero (no CAbilityHero at +0x1F0).\n");
        return;
    }
    int amount = atoi(args.c_str());
    if (amount <= 0) amount = 10;
    int* pSp = (int*)(heroAbility + 0x90);
    *pSp += amount;
    char log[128];
    sprintf_s(log, sizeof(log), "[HeroAction] Added %d Skill Points! (Total: %d)\n", amount, *pSp);
    ConsoleLog(log);
}

void Cmd_AddStats(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD heroAbility = *(DWORD*)(targetUnit + 0x1F0);
    if (!heroAbility) {
        ConsoleLog("[HeroAction] Target unit is not a Hero (no CAbilityHero at +0x1F0).\n");
        return;
    }
    int amount = atoi(args.c_str());
    if (amount <= 0) amount = 500;
    int* pStr = (int*)(heroAbility + 0x94);
    int* pAgi = (int*)(heroAbility + 0xA8);
    *pStr += amount;
    *pAgi += amount;
    char log[128];
    sprintf_s(log, sizeof(log), "[HeroAction] Boosted Stats +%d! (STR: %d, AGI: %d)\n", amount, *pStr, *pAgi);
    ConsoleLog(log);
}

void Cmd_AddXP(const std::string& args) {
    DWORD targetUnit = GetTargetUnit();
    if (!targetUnit) {
        ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
        return;
    }
    DWORD heroAbility = *(DWORD*)(targetUnit + 0x1F0);
    if (!heroAbility) {
        ConsoleLog("[HeroAction] Target unit is not a Hero (no CAbilityHero at +0x1F0).\n");
        return;
    }
    int amount = atoi(args.c_str());
    if (amount <= 0) amount = 10000;
    int* pXp = (int*)(heroAbility + 0x8C);
    *pXp += amount;
    char log[128];
    sprintf_s(log, sizeof(log), "[HeroAction] Added %d XP! (Total: %d)\n", amount, *pXp);
    ConsoleLog(log);
}

void Cmd_VIP(const std::string& args) {
    g_PlatformVIP = atoi(args.c_str());
    char log[128];
    sprintf_s(log, sizeof(log), "[Control] PlatformVIP changed to: %d\n", g_PlatformVIP);
    ConsoleLog(log);
}

void Cmd_Mall(const std::string& args) {
    g_MallMode = atoi(args.c_str());
    char log[128];
    sprintf_s(log, sizeof(log), "[Control] MallMode changed to: %d\n", g_MallMode);
    ConsoleLog(log);
}

void Cmd_Percent(const std::string& args) {
    g_MallPercent = atoi(args.c_str());
    char log[128];
    sprintf_s(log, sizeof(log), "[Control] MallPercent changed to: %d%%\n", g_MallPercent);
    ConsoleLog(log);
}

void Cmd_Level(const std::string& args) {
    g_MapLevel = atoi(args.c_str());
    char log[128];
    sprintf_s(log, sizeof(log), "[Control] MapLevel changed to: %d\n", g_MapLevel);
    ConsoleLog(log);
}

void Cmd_AntiOver(const std::string& args) {
    g_PreventGameOver = atoi(args.c_str());
    char log[128];
    sprintf_s(log, sizeof(log), "[Control] PreventGameOver changed to: %d\n", g_PreventGameOver);
    ConsoleLog(log);
}

void Cmd_Gold(const std::string& args) {
    int amount = atoi(args.c_str());
    if (amount <= 0) amount = 100000;
    DWORD gameBase = 0x6F000000;
    DWORD* pGame = (DWORD*)(gameBase + 0xBE4238);
    if (pGame && *pGame) {
        DWORD pGameWar3 = *pGame;
        DWORD playerId = *(DWORD*)(pGameWar3 + 0x28);
        if (playerId < 16) {
            DWORD player = *(DWORD*)(pGameWar3 + 0x58 + playerId * 4);
            if (player) {
                DWORD* pGold = (DWORD*)(player + 0x78);
                *pGold += amount;
                char log[128];
                sprintf_s(log, sizeof(log), "[Resources] Added %d Gold! (Current: %u)\n", amount, *pGold);
                ConsoleLog(log);
                return;
            }
        }
    }
    ConsoleLog("[Resources] Error: Player object not found.\n");
}

void Cmd_Lumber(const std::string& args) {
    int amount = atoi(args.c_str());
    if (amount <= 0) amount = 100000;
    DWORD gameBase = 0x6F000000;
    DWORD* pGame = (DWORD*)(gameBase + 0xBE4238);
    if (pGame && *pGame) {
        DWORD pGameWar3 = *pGame;
        DWORD playerId = *(DWORD*)(pGameWar3 + 0x28);
        if (playerId < 16) {
            DWORD player = *(DWORD*)(pGameWar3 + 0x58 + playerId * 4);
            if (player) {
                DWORD* pLumber = (DWORD*)(player + 0x7C);
                *pLumber += amount;
                char log[128];
                sprintf_s(log, sizeof(log), "[Resources] Added %d Lumber! (Current: %u)\n", amount, *pLumber);
                ConsoleLog(log);
                return;
            }
        }
    }
    ConsoleLog("[Resources] Error: Player object not found.\n");
}

void Cmd_Fog(const std::string& args) {
    bool enable = (args == "1" || args == "ON" || args == "on");
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, "FogEnable") == 0) {
            typedef void(__cdecl *FogEnable_t)(BOOL);
            ((FogEnable_t)g_natives[i].func_ptr)(enable ? TRUE : FALSE);
        }
        else if (strcmp(g_natives[i].name, "FogMaskEnable") == 0) {
            typedef void(__cdecl *FogMaskEnable_t)(BOOL);
            ((FogMaskEnable_t)g_natives[i].func_ptr)(enable ? TRUE : FALSE);
        }
    }
    char log[128];
    sprintf_s(log, sizeof(log), "[Map] Fog of War %s!\n", enable ? "Enabled" : "Disabled (Full Vision Active)");
    ConsoleLog(log);
}

void Cmd_Speed(const std::string& args) {
    float speed = (float)atof(args.c_str());
    if (speed <= 0.0f) speed = 1.0f;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, "SetGameSpeed") == 0) {
            typedef void(__cdecl *SetGameSpeed_t)(int);
            int val = (speed >= 2.0f) ? 4 : 3;
            ((SetGameSpeed_t)g_natives[i].func_ptr)(val);
        }
    }
    char log[128];
    sprintf_s(log, sizeof(log), "[GameSpeed] Game Speed adjusted (Factor: %.1fx)\n", speed);
    ConsoleLog(log);
}

void Cmd_Save(const std::string& args) {
    size_t c1 = args.find(':');
    if (c1 != std::string::npos) {
        size_t c2 = args.find(':', c1 + 1);
        if (c2 != std::string::npos) {
            int player = atoi(args.substr(0, c1).c_str());
            std::string key = args.substr(c1 + 1, c2 - (c1 + 1));
            std::string val = args.substr(c2 + 1);
            
            DWORD fakeKey = CreateFakeJassString(key.c_str());
            DWORD fakeVal = CreateFakeJassString(val.c_str());
            
            char logBuf[512];
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Sending manual SAVE request -> Player: %d | Key: \"%s\" | Value: \"%s\"\n", 
                      player, key.c_str(), val.c_str());
            ConsoleLog(logBuf);
            
            int ret = Monitor_SaveServerValue(player, fakeKey, fakeVal);
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Manual SAVE returned: %d\n", ret);
            ConsoleLog(logBuf);
            
            FreeFakeJassString(fakeKey);
            FreeFakeJassString(fakeVal);
        }
    }
}

void Cmd_Native(const std::string& args) {
    std::stringstream ss(args);
    std::string nativeName, arg1, arg2, arg3;
    std::getline(ss, nativeName, ':');
    std::getline(ss, arg1, ':');
    std::getline(ss, arg2, ':');
    std::getline(ss, arg3, ':');
    
    if (nativeName.empty()) return;
    NativeEntry* found = nullptr;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, nativeName.c_str()) == 0) {
            found = &g_natives[i];
            break;
        }
    }
    
    if (found) {
        char logBuf[256];
        sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Executing Native: %s | Sig: %s\n", found->name, found->sig);
        ConsoleLog(logBuf);
        
        const char* p1 = arg1.empty() ? nullptr : arg1.c_str();
        const char* p2 = arg2.empty() ? nullptr : arg2.c_str();
        const char* p3 = arg3.empty() ? nullptr : arg3.c_str();
        
        if (strstr(found->sig, "(R") != nullptr) {
            float val = p1 ? (float)atof(p1) : 0.0f;
            typedef void(__cdecl *Func1F)(float);
            ((Func1F)found->func_ptr)(val);
        }
        else if (p1 && p2 && p3) {
            typedef void(__cdecl *Func3I)(int, int, int);
            ((Func3I)found->func_ptr)(atoi(p1), atoi(p2), atoi(p3));
        }
        else if (p1 && p2) {
            typedef void(__cdecl *Func2I)(int, int);
            ((Func2I)found->func_ptr)(atoi(p1), atoi(p2));
        }
        else if (p1) {
            typedef void(__cdecl *Func1I)(int);
            ((Func1I)found->func_ptr)(atoi(p1));
        }
        else {
            typedef void(__cdecl *Func0)();
            ((Func0)found->func_ptr)();
        }
        ConsoleLog("[Invoker] Native executed successfully.\n");
    } else {
        char err[128];
        sprintf_s(err, sizeof(err), "[Invoker] Error: Native '%s' not found!\n", nativeName.c_str());
        ConsoleLog(err);
    }
}

static bool SafeInvokeHandler(CommandHandler handler, const std::string* pArgs) {
    __try {
        handler(*pArgs);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void DispatchCommand(const std::string& rawCmd) {
    if (rawCmd.empty()) return;
    std::string cmdName;
    std::string args;
    size_t colon = rawCmd.find(':');
    if (colon != std::string::npos) {
        cmdName = rawCmd.substr(0, colon);
        args = rawCmd.substr(colon + 1);
    } else {
        cmdName = rawCmd;
    }
    for (char& c : cmdName) c = (char)toupper(c);
    
    auto it = g_commandMap.find(cmdName);
    if (it != g_commandMap.end()) {
        if (!SafeInvokeHandler(it->second, &args)) {
            char err[128];
            sprintf_s(err, sizeof(err), "[Dispatcher] Exception executing command '%s'!\n", cmdName.c_str());
            ConsoleLog(err);
        }
    } else {
        char err[128];
        sprintf_s(err, sizeof(err), "[Dispatcher] Unregistered command: '%s'\n", cmdName.c_str());
        ConsoleLog(err);
    }
}

void InitCommandRegistry() {
    RegisterCommand("KILL", Cmd_Kill);
    RegisterCommand("HEAL", Cmd_Heal);
    RegisterCommand("TELEPORT", Cmd_Teleport);
    RegisterCommand("BLINK", Cmd_Teleport);
    RegisterCommand("GOD", Cmd_God);
    RegisterCommand("INVUL", Cmd_God);
    RegisterCommand("ADD_SP", Cmd_AddSP);
    RegisterCommand("ADD_STATS", Cmd_AddStats);
    RegisterCommand("ADD_XP", Cmd_AddXP);
    RegisterCommand("VIP", Cmd_VIP);
    RegisterCommand("MALL", Cmd_Mall);
    RegisterCommand("PERCENT", Cmd_Percent);
    RegisterCommand("LEVEL", Cmd_Level);
    RegisterCommand("ANTIOVER", Cmd_AntiOver);
    RegisterCommand("GOLD", Cmd_Gold);
    RegisterCommand("LUMBER", Cmd_Lumber);
    RegisterCommand("FOG", Cmd_Fog);
    RegisterCommand("SPEED", Cmd_Speed);
    RegisterCommand("SAVE", Cmd_Save);
    RegisterCommand("NATIVE", Cmd_Native);
    ConsoleLog("[Dispatcher] Command Registry initialized (20 plug-and-play actions registered).\n");
}

// -- Main Thread Execution via WndProc Hook --
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // In-game Hotkeys (Ctrl + M: Teleport, Ctrl + G: God Mode, Ctrl + H: Heal, Ctrl + K: Kill)
    if (uMsg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrlDown) {
            if (wParam == 'M') { DispatchCommand("TELEPORT"); return 0; }
            if (wParam == 'G') { DispatchCommand("GOD"); return 0; }
            if (wParam == 'H') { DispatchCommand("HEAL"); return 0; }
            if (wParam == 'K') { DispatchCommand("KILL"); return 0; }
        }
    }

    if (uMsg == WM_APP + 5) {
        std::string* pCmd = (std::string*)lParam;
        if (pCmd) {
            DispatchCommand(*pCmd);
            delete pCmd;
        }
        return 0;
    }

    // Backwards compatibility for legacy messages
    if (uMsg == WM_APP + 2) {
        DispatchCommand("KILL");
        return 0;
    }
    if (uMsg == WM_APP + 4) {
        int act = (int)wParam;
        int p = (int)lParam;
        if (act == 1) DispatchCommand("KILL");
        else if (act == 2) DispatchCommand("HEAL");
        else if (act == 3) DispatchCommand("ADD_SP:" + std::to_string(p));
        else if (act == 4) DispatchCommand("ADD_STATS:" + std::to_string(p));
        else if (act == 5) DispatchCommand("ADD_XP:" + std::to_string(p));
        else if (act == 6) DispatchCommand("TELEPORT");
        else if (act == 7) DispatchCommand("GOD");
        return 0;
    }
    
    if (OriginalWndProc) {
        return CallWindowProcA(OriginalWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

// Pipe Thread (Ultra-lightweight forwarder to Game Thread Dispatcher)
DWORD WINAPI PipeServerThread(LPVOID lpParam) {
    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\War3JassDevTool",
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 65536, 65536, 0, NULL);
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            g_activePipe = hPipe;
            ConsoleLog("[IPC] DevTool UI client connected to Named Pipe successfully.\n");
            
            while (g_activePipe != INVALID_HANDLE_VALUE) {
                DWORD bytesAvail = 0;
                if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &bytesAvail, NULL)) {
                    break;
                }
                
                if (bytesAvail == 0) {
                    Sleep(20);
                    continue;
                }
                
                char buffer[1024];
                DWORD bytesRead = 0;
                if (!ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0) {
                    break;
                }
                buffer[bytesRead] = '\0';
                
                // Forward command to Game Thread Dispatcher
                if (g_gameHwnd) {
                    std::string* pCmd = new std::string(buffer);
                    SendMessageA(g_gameHwnd, WM_APP + 5, 0, (LPARAM)pCmd);
                } else {
                    DispatchCommand(buffer);
                }
            }
            g_activePipe = INVALID_HANDLE_VALUE;
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(hPipe);
    }
    return 0;
}

void __fastcall HookedRegisterNative(void* func_ptr, const char* name, const char* signature) {
    DWORD oldProtect;
    VirtualProtect(HookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(HookTarget, OriginalBytes, 5);
    VirtualProtect(HookTarget, 5, oldProtect, &oldProtect);

    int index = g_nativeCount;
    if (g_nativeCount < 2048) {
        strncpy_s(g_natives[g_nativeCount].name, name, 63);
        strncpy_s(g_natives[g_nativeCount].sig, signature, 63);
        g_natives[g_nativeCount].func_ptr = func_ptr;
        g_nativeCount++;
    }

    void* final_ptr = func_ptr;

    // Apply DzTrace dynamically as they are registered!
    if (strstr(name, "Dz") != nullptr || strstr(name, "dz") != nullptr) {
        final_ptr = CreateThunk(index, func_ptr);
    }

    OriginalRegisterNative(final_ptr, name, signature);

    VirtualProtect(HookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    DWORD jmpAddr = ((DWORD)HookedRegisterNative - (DWORD)HookTarget) - 5;
    memcpy(&jmp[1], &jmpAddr, 4);
    memcpy(HookTarget, jmp, 5);
    VirtualProtect(HookTarget, 5, oldProtect, &oldProtect);
}

void InstallHook() {
    HookTarget = (void*)OriginalRegisterNative;
    DWORD oldProtect;
    VirtualProtect(HookTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(OriginalBytes, HookTarget, 5);
    DWORD jmpAddr = ((DWORD)HookedRegisterNative - (DWORD)HookTarget) - 5;
    BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    memcpy(&jmp[1], &jmpAddr, 4);
    memcpy(HookTarget, jmp, 5);
    VirtualProtect(HookTarget, 5, oldProtect, &oldProtect);
}

struct EnumData {
    DWORD processId;
    HWND hWnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumData* data = (EnumData*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->processId) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "Warcraft III") == 0 || strcmp(className, "OsWindow") == 0 || strstr(className, "Warcraft") != NULL) {
            data->hWnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    
    // Clear local_save.txt on startup
    FILE* fp = nullptr;
    if (fopen_s(&fp, "local_save.txt", "w") == 0 && fp) {
        fprintf(fp, "=== Warcraft III Local Save Log ===\n");
        fclose(fp);
    }
    
    ConsoleLog("========================================\n");
    ConsoleLog("[JassHook] E2E Dynamic Native Invoker + DzTrace Activated\n");
    ConsoleLog("========================================\n");

    HMODULE hGame = GetModuleHandleA("Game.dll");
    while (!hGame) {
        Sleep(100);
        hGame = GetModuleHandleA("Game.dll");
    }

    OriginalRegisterNative = (RegisterNative_t)((DWORD)hGame + REGISTER_NATIVE_RVA);
    InstallHook();

    HMODULE hPlugin = GetModuleHandleA("War3Plugin.dll");
    if (hPlugin) {
        ConsoleLog("[JassHook] Pre-populating DzAPI handles from static registry...\n");
        for (int i = 0; i < g_dzTableCount && g_nativeCount < 2048; ++i) {
            strncpy_s(g_natives[g_nativeCount].name, g_dzTable[i].name, 63);
            strncpy_s(g_natives[g_nativeCount].sig, g_dzTable[i].sig, 63);
            
            void* original_ptr = (BYTE*)hPlugin + g_dzTable[i].rva;
            
            // For pre-populated ones, we cannot pass thunk to Game.dll because it already registered them!
            // But if the user injects at main menu, HookedRegisterNative will catch them naturally,
            // and this fallback won't be used (or rather, it will add duplicates but harmlessly).
            g_natives[g_nativeCount].func_ptr = original_ptr;
            g_nativeCount++;
        }
        
        ConsoleLog("----------------------------------------\n");
        ConsoleLog("[Spoofer] Loading Config (JassHookConfig.ini)...\n");
        wchar_t iniPathW[MAX_PATH];
        swprintf_s(iniPathW, MAX_PATH, L"%s\\JassHookConfig.ini", g_DllDirectory);
        
        g_MallMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 2, iniPathW);
        g_MapLevel = GetPrivateProfileIntW(L"Spoof", L"MapLevel", 99, iniPathW);
        g_MallPercent = GetPrivateProfileIntW(L"Spoof", L"MallPercent", 30, iniPathW);
        g_PreventGameOver = GetPrivateProfileIntW(L"Spoof", L"PreventGameOver", 1, iniPathW);
        g_PlatformVIP = GetPrivateProfileIntW(L"Spoof", L"PlatformVIP", 2, iniPathW);
        
        char cfgLog[256];
        sprintf_s(cfgLog, sizeof(cfgLog), "  -> MallMode: %d (0=Off, 1=Whitelist, 2=All, 3=Percent)\n  -> MallPercent: %d%%\n  -> MapLevel: %d\n  -> PreventGameOver: %d\n  -> PlatformVIP: %d\n", 
                  g_MallMode, g_MallPercent, g_MapLevel, g_PreventGameOver, g_PlatformVIP);
        ConsoleLog(cfgLog);

        LoadMallWhitelist();
        LoadMallBlacklist();

        ConsoleLog("[Spoofer] Applying VIP / Mall / Level Spoofs (Grounded in DzAPI Research)...\n");
        if (g_MallMode > 0) {
            InstallSpoofHook("DzAPI_Map_IsVIP", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_GetPlatformVIP", (void*)Spoof_PlatformVIP);
            InstallSpoofHook("DzAPI_Map_IsRedVIP", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_IsBlueVIP", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_GetServerArchiveEquip", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_GetServerArchiveDrop", (void*)Spoof_True);
        }
        InstallSpoofHook("DzAPI_Map_GetMapLevel", (void*)Spoof_LevelDynamic);
        InstallSpoofHook("DzAPI_Map_GetLadderLevel", (void*)Spoof_LevelDynamic);
        InstallSpoofHook("DzAPI_Map_GetLadderRank", (void*)Spoof_True);
        InstallSpoofHook("DzAPI_Map_GetMapLevelRank", (void*)Spoof_True);

        
        ConsoleLog("[Spoofer] Applying ServerValue & MallItem Spoofer/Dumper...\n");
        InstallTrampolineHook("DzAPI_Map_HasMallItem", (void*)Monitor_HasMallItem, hkHasMallItem);
        InstallTrampolineHook("DzAPI_Map_SaveServerValue", (void*)Monitor_SaveServerValue, hkSave);
        InstallTrampolineHook("DzAPI_Map_GetServerValue", (void*)Monitor_GetServerValue, hkGet);
        InstallTrampolineHook("DzAPI_Map_Stat_SetStat", (void*)Monitor_Stat_SetStat, hkSetStat);
        InstallTrampolineHook("DzPlatform_GameEnd", (void*)Monitor_DzPlatform_GameEnd, hkGameEnd);
        ConsoleLog("----------------------------------------\n");
    }

    InitCommandRegistry();

    EnumData edata = { GetCurrentProcessId(), NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&edata);
    if (edata.hWnd) {
        g_gameHwnd = edata.hWnd;
        OriginalWndProc = (WNDPROC)SetWindowLongA(edata.hWnd, GWL_WNDPROC, (LONG)(LONG_PTR)HookedWndProc);
        ConsoleLog("[JassHook] WndProc hooked successfully. E2E system ready!\n");
    } else {
        ConsoleLog("[JassHook] Error: Could not find game window to hook WndProc.\n");
    }

    CreateThread(NULL, 0, PipeServerThread, NULL, 0, NULL);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        GetModuleFileNameW(hModule, g_DllDirectory, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(g_DllDirectory, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        
        DisableThreadLibraryCalls(hModule);
        CreateThread(0, 0, MainThread, hModule, 0, 0);
    }
    return TRUE;
}
