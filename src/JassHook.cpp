#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <string>
#include <fstream>
#include <sstream>
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

int g_MallMode = 2;         // 0=Off, 1=Whitelist, 2=All, 3=Percent
int g_MallPercent = 80;     // Default 80%
int g_MapLevel = 99;        // Map Level spoof
int g_PlatformVIP = 2;      // 0=Off, 1=Normal, 2=Super VIP
int g_PreventGameOver = 1;  // Prevent defeat

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

void SaveConfigValue(const wchar_t* key, int value) {
    wchar_t iniPathW[MAX_PATH];
    swprintf_s(iniPathW, MAX_PATH, L"%s\\JassHookConfig.ini", g_DllDirectory);
    wchar_t valBuf[32];
    swprintf_s(valBuf, 32, L"%d", value);
    WritePrivateProfileStringW(L"Spoof", key, valBuf, iniPathW);
}

// ==========================================
// Mall Whitelist & Blacklist (Honeypot Trap Protection)
// ==========================================
std::unordered_set<std::string> g_mallWhitelist;
std::unordered_set<std::string> g_mallBlacklist;

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
    sprintf_s(log, sizeof(log), "[MallWhitelist] Loaded %zu items from mall_spoof.txt\n", g_mallWhitelist.size());
    ConsoleLog(log);
}

void LoadMallBlacklist() {
    g_mallBlacklist.clear();
    // Pre-seed known anti-cheat honeypot traps
    g_mallBlacklist.insert("cheat");
    g_mallBlacklist.insert("test");
    g_mallBlacklist.insert("trap");
    g_mallBlacklist.insert("BAN_CHECK");
    g_mallBlacklist.insert("TEST_MALL_ITEM");
    g_mallBlacklist.insert("HONEYPOT_DETECT");
    g_mallBlacklist.insert("DEBUG_MALL_TOKEN");
    g_mallBlacklist.insert("ASDZDF");
    g_mallBlacklist.insert("ecn3gHnB95");
    g_mallBlacklist.insert("J");
    g_mallBlacklist.insert("C");
    g_mallBlacklist.insert("D");
    g_mallBlacklist.insert("E");
    g_mallBlacklist.insert("F");
    g_mallBlacklist.insert("M");
    g_mallBlacklist.insert("P");
    g_mallBlacklist.insert("I");

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

bool IsTrapHoneypot(const char* keyStr) {
    if (!keyStr || strcmp(keyStr, "(null)") == 0 || keyStr[0] == '\0') return true;
    size_t len = strlen(keyStr);
    if (len <= 2) return true; // Single or two letter honeypots
    if (strcmp(keyStr, "ecn3gHnB95") == 0) return true;
    if (g_mallBlacklist.find(keyStr) != g_mallBlacklist.end()) return true;
    
    std::string lower = keyStr;
    for (char& c : lower) c = (char)tolower(c);
    if (lower.find("trap") != std::string::npos ||
        lower.find("test") != std::string::npos ||
        lower.find("cheat") != std::string::npos ||
        lower.find("ban_") != std::string::npos ||
        lower.find("fake") != std::string::npos ||
        lower.find("honeypot") != std::string::npos) {
        return true;
    }
    return false;
}

struct InvokeData {
    void* func_ptr;
    const char* sig;
    char* arg1;
    char* arg2;
    char* arg3;
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
    
    thunk[16] = 0xE8; // call GenericDzTraceHandler
    DWORD callAddr = (DWORD)GenericDzTraceHandler - (DWORD)(&thunk[16]) - 5;
    *(DWORD*)(&thunk[17]) = callAddr;
    
    thunk[21] = 0x83; thunk[22] = 0xC4; thunk[23] = 0x10; // add esp, 16
    thunk[24] = 0x9D; // popfd
    thunk[25] = 0x61; // popad
    
    thunk[26] = 0xE9; // jmp original_func
    DWORD jmpAddr = (DWORD)original_func - (DWORD)(&thunk[26]) - 5;
    *(DWORD*)(&thunk[27]) = jmpAddr;
    
    return thunk;
}

// ==========================================
// Trampoline Hook Engine
// ==========================================
struct HookData {
    void* targetPtr = nullptr;
    BYTE originalBytes[5];
    bool isHooked = false;
};

HookData hkHasMallItem;
HookData hkSave;
HookData hkGet;
HookData hkSetStat;
HookData hkGameEnd;

void RemoveTrampoline(HookData& data) {
    if (!data.isHooked || !data.targetPtr) return;
    DWORD oldProtect;
    VirtualProtect(data.targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(data.targetPtr, data.originalBytes, 5);
    VirtualProtect(data.targetPtr, 5, oldProtect, &oldProtect);
    data.isHooked = false;
}

void RestoreTrampoline(HookData& data, void* hookFunc) {
    if (data.isHooked || !data.targetPtr) return;
    DWORD oldProtect;
    VirtualProtect(data.targetPtr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    DWORD relAddr = ((DWORD)hookFunc - (DWORD)data.targetPtr) - 5;
    memcpy(&jmp[1], &relAddr, 4);
    memcpy(data.targetPtr, jmp, 5);
    VirtualProtect(data.targetPtr, 5, oldProtect, &oldProtect);
    data.isHooked = true;
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
        memcpy(outData.originalBytes, targetPtr, 5);
        BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
        DWORD relAddr = ((DWORD)hookFunc - (DWORD)targetPtr) - 5;
        memcpy(&jmp[1], &relAddr, 4);
        memcpy(targetPtr, jmp, 5);
        VirtualProtect(targetPtr, 5, oldProtect, &oldProtect);
        outData.isHooked = true;
        
        char log[256];
        sprintf_s(log, sizeof(log), "[Trampoline] Hooked %s -> Success!\n", nativeName);
        ConsoleLog(log);
    } else {
        char log[256];
        sprintf_s(log, sizeof(log), "[Trampoline] Failed to hook %s (Not found)\n", nativeName);
        ConsoleLog(log);
    }
}

// ==========================================
// Native Monitors & Spoofers
// ==========================================
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

    RemoveTrampoline(hkGet);
    typedef DWORD(__cdecl *FuncType)(int, DWORD);
    DWORD result = ((FuncType)hkGet.targetPtr)(player, key_ptr);
    RestoreTrampoline(hkGet, (void*)Monitor_GetServerValue);

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

    // 1. Trap / Honeypot Blacklist check (Prevents save loading failure)
    if (IsTrapHoneypot(keyStr)) {
        char log[256];
        sprintf_s(log, sizeof(log), "[Trap Neutralized] Item \"%s\" is Honeypot/Trap! Spoofed to 0 (Denied).\n", keyStr ? keyStr : "(null)");
        ConsoleLog(log);
        return 0;
    }

    // 2. Spoof logic based on MallMode
    int finalResult = originalResult;
    
    if (g_MallMode == 0) {
        finalResult = originalResult;
    } else if (g_MallMode == 1) {
        // Whitelist mode (mall_spoof.txt)
        if (!g_mallWhitelist.empty() && keyStr) {
            if (g_mallWhitelist.find(keyStr) != g_mallWhitelist.end()) {
                finalResult = 1;
            }
        }
    } else if (g_MallMode == 2) {
        // All items mode
        finalResult = 1;
    } else if (g_MallMode == 3) {
        // Percentage mode (Deterministic hash based on item key)
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

int __cdecl Spoof_True() {
    return 1;
}

int __cdecl Spoof_PlatformVIP(int player) {
    return g_PlatformVIP;
}

int __cdecl Spoof_LevelDynamic() {
    return g_MapLevel;
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
    const char* chars;
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
    char key[128];
    char val[256];
};

// ==========================================
// Original Kill Unit (via HP Index)
// ==========================================
void KillSelectedUnit() {
    __try {
        HMODULE hGame = GetModuleHandleA("Game.dll");
        if (!hGame) return;
        DWORD gameBase = (DWORD)hGame;
        
        DWORD selTable = *(DWORD*)(gameBase + 0xBE6398); // SELECT_TABLE_RVA
        if (!selTable) return;
        DWORD selectedUnit = *(DWORD*)(selTable + 0x28); // SELECTED_UNIT_OFFSET
        if (!selectedUnit) return;
        
        DWORD hpIndex = *(DWORD*)(selectedUnit + 0xA0); // UNIT_HP_INDEX_OFF
        
        DWORD thisGame = *(DWORD*)(gameBase + 0xBE40A8); // THIS_GAME_RVA
        if (!thisGame) return;
        DWORD gameMemory = *(DWORD*)(thisGame + 0x0C); // GAME_TABLE_OFF
        if (!gameMemory) return;
        
        DWORD entryAddress = gameMemory + hpIndex * 8 + 4;
        DWORD address1 = *(DWORD*)entryAddress;
        if (!address1) return;
        
        float* hpValue = (float*)(address1 + 0x78);
        *hpValue = 0.0f; // INSTAKILL
        
        ConsoleLog("[Hack] Instantly killed selected unit!\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[Hack] Failed to kill selected unit (Exception).\n");
    }
}

// -- Main Thread Execution via WndProc Hook --
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // In-game Hotkey (Ctrl + K: Instant Kill)
    if (uMsg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrlDown && wParam == 'K') {
            KillSelectedUnit();
            return 0;
        }
    }

    if (uMsg == WM_APP + 2) {
        KillSelectedUnit();
        return 0;
    }
    
    if (uMsg == WM_APP + 3) {
        SaveData* sd = (SaveData*)lParam;
        if (sd) {
            DWORD fakeKey = CreateFakeJassString(sd->key);
            DWORD fakeVal = CreateFakeJassString(sd->val);
            
            char logBuf[512];
            sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Manual SAVE request -> Player: %d | Key: \"%s\" | Value: \"%s\"\n", 
                      sd->player, sd->key, sd->val);
            ConsoleLog(logBuf);
            
            Monitor_SaveServerValue(sd->player, fakeKey, fakeVal);
            delete sd;
        }
        return 0;
    }
    
    if (uMsg == WM_APP + 1) {
        InvokeData* data = (InvokeData*)lParam;
        if (data) {
            char logBuf[256];
            void* func_ptr = data->func_ptr;
            const char* sig = data->sig ? data->sig : "";
            
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
            if (data->arg1) free(data->arg1);
            if (data->arg2) free(data->arg2);
            if (data->arg3) free(data->arg3);
            delete data;
        }
        return 0;
    }
    
    if (OriginalWndProc) {
        return CallWindowProcA(OriginalWndProc, hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

// Process single incoming command from Named Pipe
void ProcessSingleCommand(const std::string& cmd) {
    if (cmd.empty()) return;
    
    if (cmd == "KILL" || cmd == "!kill" || cmd == "!killselected") {
        if (g_gameHwnd) {
            SendMessageA(g_gameHwnd, WM_APP + 2, 0, 0);
        } else {
            KillSelectedUnit();
        }
    }
    else if (cmd.rfind("SAVE:", 0) == 0) {
        char buf[512];
        strncpy_s(buf, cmd.c_str(), sizeof(buf));
        char* ctx = nullptr;
        strtok_s(buf, ":", &ctx); // Skip SAVE
        char* pStr = strtok_s(nullptr, ":", &ctx);
        char* kStr = strtok_s(nullptr, ":", &ctx);
        char* vStr = strtok_s(nullptr, ":", &ctx);
        if (pStr && kStr && vStr && g_gameHwnd) {
            SaveData* sd = new SaveData();
            sd->player = atoi(pStr);
            strncpy_s(sd->key, kStr, sizeof(sd->key));
            strncpy_s(sd->val, vStr, sizeof(sd->val));
            SendMessageA(g_gameHwnd, WM_APP + 3, 0, (LPARAM)sd);
        }
    }
    else if (cmd.rfind("MALL:", 0) == 0) {
        int mode = atoi(cmd.substr(5).c_str());
        g_MallMode = mode;
        SaveConfigValue(L"MallMode", g_MallMode);
        char log[128];
        sprintf_s(log, sizeof(log), "[Control] MallMode changed to: %d (Saved to INI)\n", g_MallMode);
        ConsoleLog(log);
    }
    else if (cmd.rfind("PERCENT:", 0) == 0) {
        int pct = atoi(cmd.substr(8).c_str());
        if (pct < 1) pct = 1;
        if (pct > 100) pct = 100;
        g_MallPercent = pct;
        SaveConfigValue(L"MallPercent", g_MallPercent);
        char log[128];
        sprintf_s(log, sizeof(log), "[Control] MallPercent changed to: %d%% (Saved to INI)\n", g_MallPercent);
        ConsoleLog(log);
    }
    else if (cmd == "RELOAD_LISTS") {
        LoadMallWhitelist();
        LoadMallBlacklist();
        ConsoleLog("[Control] Whitelist & Blacklist reloaded successfully.\n");
    }
    else if (cmd.rfind("NATIVE:", 0) == 0) {
        char buf[512];
        strncpy_s(buf, cmd.c_str(), sizeof(buf));
        char* ctx = nullptr;
        char* nativeTag = strtok_s(buf, ":", &ctx);
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
                    InvokeData* data = new InvokeData();
                    data->func_ptr = found->func_ptr;
                    data->sig = found->sig;
                    data->arg1 = arg1Str ? _strdup(arg1Str) : nullptr;
                    data->arg2 = arg2Str ? _strdup(arg2Str) : nullptr;
                    data->arg3 = arg3Str ? _strdup(arg3Str) : nullptr;
                    SendMessageA(g_gameHwnd, WM_APP + 1, 0, (LPARAM)data);
                } else {
                    ConsoleLog("[Invoker] Error: Game HWND not found!\n");
                }
            } else {
                sprintf_s(logBuf, sizeof(logBuf), "[Invoker] Error: Native '%s' not found in Registry!\n", nativeName);
                ConsoleLog(logBuf);
            }
        }
    }
    else {
        // Fallback: check if it matches a native name with no arguments
        NativeEntry* found = nullptr;
        for (int i = 0; i < g_nativeCount; ++i) {
            if (strcmp(g_natives[i].name, cmd.c_str()) == 0) {
                found = &g_natives[i];
                break;
            }
        }
        if (found && g_gameHwnd) {
            InvokeData* data = new InvokeData();
            data->func_ptr = found->func_ptr;
            data->sig = found->sig;
            data->arg1 = nullptr;
            data->arg2 = nullptr;
            data->arg3 = nullptr;
            SendMessageA(g_gameHwnd, WM_APP + 1, 0, (LPARAM)data);
        } else {
            char logBuf[256];
            sprintf_s(logBuf, sizeof(logBuf), "[Pipe] Received unhandled command: %s\n", cmd.c_str());
            ConsoleLog(logBuf);
        }
    }
}

// Pipe Thread (Duplex Named Pipe)
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
            ConsoleLog("[IPC] Client connected to Named Pipe successfully.\n");
            
            char buffer[4096];
            DWORD bytesRead = 0;
            std::string leftover = "";
            
            while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                leftover.append(buffer, bytesRead);
                
                size_t nl;
                while ((nl = leftover.find('\n')) != std::string::npos) {
                    std::string line = leftover.substr(0, nl);
                    leftover.erase(0, nl + 1);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        ProcessSingleCommand(line);
                    }
                }
            }
            
            if (!leftover.empty()) {
                while (!leftover.empty() && (leftover.back() == '\r' || leftover.back() == ' ')) {
                    leftover.pop_back();
                }
                if (!leftover.empty()) {
                    ProcessSingleCommand(leftover);
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
        char title[256];
        GetClassNameA(hwnd, className, sizeof(className));
        GetWindowTextA(hwnd, title, sizeof(title));
        if (strcmp(className, "Warcraft III") == 0 || strcmp(className, "OsWindow") == 0 || 
            strstr(className, "Warcraft") != NULL || strstr(title, "Warcraft") != NULL) {
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
            g_natives[g_nativeCount].func_ptr = original_ptr;
            g_nativeCount++;
        }
        
        ConsoleLog("----------------------------------------\n");
        ConsoleLog("[Spoofer] Loading Config (JassHookConfig.ini)...\n");
        wchar_t iniPathW[MAX_PATH];
        swprintf_s(iniPathW, MAX_PATH, L"%s\\JassHookConfig.ini", g_DllDirectory);
        
        g_MallMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 2, iniPathW);
        g_MallPercent = GetPrivateProfileIntW(L"Spoof", L"MallPercent", 80, iniPathW);
        g_MapLevel = GetPrivateProfileIntW(L"Spoof", L"MapLevel", 99, iniPathW);
        g_PreventGameOver = GetPrivateProfileIntW(L"Spoof", L"PreventGameOver", 1, iniPathW);
        g_PlatformVIP = GetPrivateProfileIntW(L"Spoof", L"PlatformVIP", 2, iniPathW);
        
        char cfgLog[256];
        sprintf_s(cfgLog, sizeof(cfgLog), "  -> MallMode: %d (0=Off, 1=Whitelist, 2=All, 3=Percent)\n  -> MallPercent: %d%%\n  -> MapLevel: %d\n  -> PreventGameOver: %d\n  -> PlatformVIP: %d\n", 
                  g_MallMode, g_MallPercent, g_MapLevel, g_PreventGameOver, g_PlatformVIP);
        ConsoleLog(cfgLog);

        LoadMallWhitelist();
        LoadMallBlacklist();

        ConsoleLog("[Spoofer] Applying VIP / Mall / Level Spoofs...\n");
        if (g_MallMode > 0) {
            InstallSpoofHook("DzAPI_Map_IsVIP", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_GetPlatformVIP", (void*)Spoof_PlatformVIP);
            InstallSpoofHook("DzAPI_Map_IsRedVIP", (void*)Spoof_True);
            InstallSpoofHook("DzAPI_Map_IsBlueVIP", (void*)Spoof_True);
            // Note: DzAPI_Map_GetServerArchiveEquip and DzAPI_Map_GetServerArchiveDrop
            // are platform save archives, NOT mall items. Never hook them!
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
    for (int retry = 0; retry < 40 && !edata.hWnd; ++retry) {
        EnumWindows(EnumWindowsProc, (LPARAM)&edata);
        if (!edata.hWnd) {
            Sleep(500);
        }
    }
    if (edata.hWnd) {
        g_gameHwnd = edata.hWnd;
        OriginalWndProc = (WNDPROC)SetWindowLongA(edata.hWnd, GWL_WNDPROC, (LONG)(LONG_PTR)HookedWndProc);
        char log[256];
        sprintf_s(log, sizeof(log), "[JassHook] Game window found (0x%p) and WndProc hooked successfully. E2E system ready!\n", g_gameHwnd);
        ConsoleLog(log);
    } else {
        ConsoleLog("[JassHook] Warning: Could not find game window to hook WndProc. Commands will run in fallback mode.\n");
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
