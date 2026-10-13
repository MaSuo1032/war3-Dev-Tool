#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <string>
#include <fstream>
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
// Advanced Hero & Unit Manipulation (Grounded in DzAPI_Structures.h)
// =========================================================================
void HandleHeroAction(int action, int param) {
    __try {
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
        
        // 2. If no selected unit, try unit under mouse cursor (CGameUI + 0x3BC -> +0x244)
        if (!targetUnit) {
            DWORD* pUI = (DWORD*)(gameBase + 0xBE6350);
            if (pUI && *pUI) {
                DWORD worldFrame = *(DWORD*)(*pUI + 0x3BC);
                if (worldFrame) {
                    targetUnit = *(DWORD*)(worldFrame + 0x244);
                }
            }
        }
        
        if (!targetUnit) {
            ConsoleLog("[HeroAction] No unit selected or hovered under cursor!\n");
            return;
        }

        // Action 1: Kill Target Unit
        if (action == 1) {
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
            return;
        }

        // Action 2: Heal (Full HP Restore)
        if (action == 2) {
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
            return;
        }

        // Actions 3, 4, 5: CAbilityHero modifications (+0x1F0)
        DWORD heroAbility = *(DWORD*)(targetUnit + 0x1F0);
        if (!heroAbility) {
            ConsoleLog("[HeroAction] Target unit is not a Hero (no CAbilityHero at +0x1F0).\n");
            return;
        }

        if (action == 3) {
            // Add Skill Points (+0x90)
            int* pSp = (int*)(heroAbility + 0x90);
            *pSp += param;
            char log[128];
            sprintf_s(log, sizeof(log), "[HeroAction] Added %d Skill Points! (Total: %d)\n", param, *pSp);
            ConsoleLog(log);
        }
        else if (action == 4) {
            // Boost STR (+0x94) and AGI (+0xA8)
            int* pStr = (int*)(heroAbility + 0x94);
            int* pAgi = (int*)(heroAbility + 0xA8);
            *pStr += param;
            *pAgi += param;
            char log[128];
            sprintf_s(log, sizeof(log), "[HeroAction] Boosted Stats +%d! (STR: %d, AGI: %d)\n", param, *pStr, *pAgi);
            ConsoleLog(log);
        }
        else if (action == 5) {
            // Add Experience XP (+0x8C)
            int* pXp = (int*)(heroAbility + 0x8C);
            *pXp += param;
            char log[128];
            sprintf_s(log, sizeof(log), "[HeroAction] Added %d XP! (Total: %d)\n", param, *pXp);
            ConsoleLog(log);
        }

        // Action 6: Teleport Selected Unit to Mouse Position (CWidget::SetCoord @ Game.dll + 0x635DE0)
        if (action == 6) {
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
            return;
        }

        // Action 7: Make Unit Invulnerable (God Mode - Official ability + 1 Billion HP)
        if (action == 7) {
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
            return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[HeroAction] Exception caught during hero manipulation.\n");
    }
}

// -- Main Thread Execution via WndProc Hook --
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // In-game Hotkeys (Ctrl + M: Teleport, Ctrl + G: God Mode, Ctrl + H: Heal, Ctrl + K: Kill)
    if (uMsg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrlDown) {
            if (wParam == 'M') {
                HandleHeroAction(6, 0); // Ctrl+M: Teleport to mouse
                return 0;
            }
            else if (wParam == 'G') {
                HandleHeroAction(7, 0); // Ctrl+G: God Mode (Invulnerable)
                return 0;
            }
            else if (wParam == 'H') {
                HandleHeroAction(2, 0); // Ctrl+H: Heal
                return 0;
            }
            else if (wParam == 'K') {
                HandleHeroAction(1, 0); // Ctrl+K: Kill
                return 0;
            }
        }
    }

    if (uMsg == WM_APP + 2) {
        HandleHeroAction(1, 0); // Kill
        return 0;
    }
    
    if (uMsg == WM_APP + 4) {
        HandleHeroAction((int)wParam, (int)lParam);
        return 0;
    }


    
    if (uMsg == WM_APP + 3) {
        SaveData* sd = (SaveData*)lParam;
        DWORD fakeKey = CreateFakeJassString(sd->key);
        DWORD fakeVal = CreateFakeJassString(sd->val);
        
        char logBuf[512];
        sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Sending manual SAVE request -> Player: %d | Key: \"%s\" | Value: \"%s\"\n", 
                  sd->player, sd->key, sd->val);
        ConsoleLog(logBuf);
        
        int ret = Monitor_SaveServerValue(sd->player, fakeKey, fakeVal);
        sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Manual SAVE returned: %d\n", ret);
        ConsoleLog(logBuf);
        
        FreeFakeJassString(fakeKey);
        FreeFakeJassString(fakeVal);
        delete sd;
        return 0;
    }
    
    if (uMsg == WM_APP + 1) {
        InvokeData* data = (InvokeData*)lParam;
        char logBuf[256];
        void* func_ptr = data->func_ptr;
        const char* sig = data->sig;
        
        if (strstr(sig, "(R") != nullptr) {
            float val = data->arg1 ? (float)atof(data->arg1) : 0.0f;
            typedef void(__cdecl *Func1F)(float);
            ((Func1F)func_ptr)(val);
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Execution Success (Float Arg: %f).\n", val);
        }
        else if (data->arg1 && data->arg2 && data->arg3) {
            int v1 = atoi(data->arg1);
            int v2 = atoi(data->arg2);
            int v3 = atoi(data->arg3);
            typedef void(__cdecl *Func3I)(int, int, int);
            ((Func3I)func_ptr)(v1, v2, v3);
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Execution Success (Args: %d, %d, %d).\n", v1, v2, v3);
        }
        else if (data->arg1 && data->arg2) {
            int v1 = atoi(data->arg1);
            int v2 = atoi(data->arg2);
            typedef void(__cdecl *Func2I)(int, int);
            ((Func2I)func_ptr)(v1, v2);
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Execution Success (Args: %d, %d).\n", v1, v2);
        }
        else if (data->arg1) {
            int v1 = atoi(data->arg1);
            typedef void(__cdecl *Func1I)(int);
            ((Func1I)func_ptr)(v1);
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Execution Success (Arg: %d).\n", v1);
        }
        else {
            typedef void(__cdecl *Func0)();
            ((Func0)func_ptr)();
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Execution Success (0 Args).\n");
        }
        
        ConsoleLog(logBuf);
        return 0; // Handled
    }
    
    if (OriginalWndProc) {
        return CallWindowProcA(OriginalWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

// Pipe Thread
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
                
                if (strncmp(buffer, "KILL", 4) == 0) {
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 1, 0); // Action 1 = Kill
                    }
                }
                else if (strncmp(buffer, "HEAL", 4) == 0) {
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 2, 0); // Action 2 = Heal (Full HP)
                    }
                }
                else if (strncmp(buffer, "TELEPORT", 8) == 0 || strncmp(buffer, "BLINK", 5) == 0) {
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 6, 0); // Action 6 = Teleport to mouse
                    }
                }
                else if (strncmp(buffer, "GOD", 3) == 0 || strncmp(buffer, "INVUL", 5) == 0) {
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 7, 0); // Action 7 = Invulnerable (God Mode)
                    }
                }
                else if (strncmp(buffer, "ADD_SP:", 7) == 0) {
                    int amount = atoi(buffer + 7);
                    if (amount <= 0) amount = 10;
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 3, (LPARAM)amount); // Action 3 = Add SP
                    }
                }
                else if (strncmp(buffer, "ADD_STATS:", 10) == 0) {
                    int amount = atoi(buffer + 10);
                    if (amount <= 0) amount = 500;
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 4, (LPARAM)amount); // Action 4 = Add Stats
                    }
                }
                else if (strncmp(buffer, "ADD_XP:", 7) == 0) {
                    int amount = atoi(buffer + 7);
                    if (amount <= 0) amount = 10000;
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 4, 5, (LPARAM)amount); // Action 5 = Add XP
                    }
                }
                else if (strncmp(buffer, "VIP:", 4) == 0) {
                    g_PlatformVIP = atoi(buffer + 4);
                    char log[128];
                    sprintf_s(log, sizeof(log), "[Control] PlatformVIP changed to: %d\n", g_PlatformVIP);
                    ConsoleLog(log);
                }
                else if (strncmp(buffer, "SAVE:", 5) == 0) {
                    char* p1 = strchr(buffer, ':');
                    if (p1) {
                        char* p2 = strchr(p1 + 1, ':');
                        if (p2) {
                            char* p3 = strchr(p2 + 1, ':');
                            if (p3 && g_gameHwnd) {
                                *p2 = '\0';
                                *p3 = '\0';
                                char* pStr = p1 + 1;
                                char* kStr = p2 + 1;
                                char* vStr = p3 + 1;
                                SaveData* sd = new SaveData();
                                sd->player = atoi(pStr);
                                strncpy_s(sd->key, sizeof(sd->key), kStr, _TRUNCATE);
                                strncpy_s(sd->val, sizeof(sd->val), vStr, _TRUNCATE);
                                SendMessageA(g_gameHwnd, WM_APP + 3, 0, (LPARAM)sd);
                            }
                        }
                    }
                }
                else if (strncmp(buffer, "MALL:", 5) == 0) {
                    g_MallMode = atoi(buffer + 5);
                    char log[128];
                    sprintf_s(log, sizeof(log), "[Control] MallMode changed to: %d\n", g_MallMode);
                    ConsoleLog(log);
                }
                else if (strncmp(buffer, "PERCENT:", 8) == 0) {
                    g_MallPercent = atoi(buffer + 8);
                    char log[128];
                    sprintf_s(log, sizeof(log), "[Control] MallPercent changed to: %d%%\n", g_MallPercent);
                    ConsoleLog(log);
                }
                else if (strncmp(buffer, "LEVEL:", 6) == 0) {
                    g_MapLevel = atoi(buffer + 6);
                    char log[128];
                    sprintf_s(log, sizeof(log), "[Control] MapLevel changed to: %d\n", g_MapLevel);
                    ConsoleLog(log);
                }
                else if (strncmp(buffer, "ANTIOVER:", 9) == 0) {
                    g_PreventGameOver = atoi(buffer + 9);
                    char log[128];
                    sprintf_s(log, sizeof(log), "[Control] PreventGameOver changed to: %d\n", g_PreventGameOver);
                    ConsoleLog(log);
                }
                else if (strncmp(buffer, "NATIVE:", 7) == 0) {
                    char* ctx = nullptr;
                    char* nativeTag = strtok_s(buffer, ":", &ctx);
                    char* nativeName = strtok_s(nullptr, ":", &ctx);
                    char* arg1Str = strtok_s(nullptr, ":", &ctx);
                    char* arg2Str = strtok_s(nullptr, ":", &ctx);
                    char* arg3Str = strtok_s(nullptr, ":", &ctx);
                    
                    if (nativeName) {
                        NativeEntry* found = nullptr;
                        for (int i = 0; i < g_nativeCount; ++i) {
                            if (strcmp(g_natives[i].name, nativeName) == 0) {
                                found = &g_natives[i];
                                break;
                            }
                        }
                        
                        char logBuf[256];
                        if (found) {
                            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Requesting Execution: %s | Sig: %s\n", found->name, found->sig);
                            ConsoleLog(logBuf);
                            
                            if (g_gameHwnd) {
                                InvokeData data;
                                data.func_ptr = found->func_ptr; // Points to original function, will trigger thunk if hooked
                                data.sig = found->sig;
                                data.arg1 = arg1Str;
                                data.arg2 = arg2Str;
                                data.arg3 = arg3Str;
                                SendMessageA(g_gameHwnd, WM_APP + 1, 0, (LPARAM)&data);
                            } else {
                                ConsoleLog("[Invoker] Error: Game HWND not found!\n");
                            }
                        } else {
                            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Error: Native '%s' not found in Registry!\n", nativeName);
                            ConsoleLog(logBuf);
                        }
                    }
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
