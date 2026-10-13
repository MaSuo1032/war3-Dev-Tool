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

void ConsoleLog(const char* msg) {
    OutputDebugStringA(msg);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteConsoleA(hConsole, msg, (DWORD)strlen(msg), &w, NULL);
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
int g_MallMode = 1; // 0 = Off, 1 = Whitelist, 2 = All
int g_MapLevel = 50;

int __cdecl Spoof_True() { return 1; }
int __cdecl Spoof_LevelDynamic() { return g_MapLevel; }

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

    // Spoof logic
    extern int g_MallMode;
    int finalResult = originalResult;
    
    if (g_MallMode == 2) {
        finalResult = 1; // Spoof ALL items
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
    char key[64];
    char val[64];
};

void KillSelectedUnit() {
    __try {
        DWORD gameBase = 0x6F000000;
        DWORD selectedUnitList = *(DWORD*)(gameBase + 0xBE4238); // UNIT_LIST_RVA
        if (!selectedUnitList) return;
        DWORD playerId = *(DWORD*)(selectedUnitList + 0x28); // PLAYER_ID_OFF
        if (playerId >= 16) return;
        DWORD player = *(DWORD*)(selectedUnitList + 0x58 + playerId * 4); // PLAYER_TABLE_OFF
        if (!player) return;
        DWORD listRoot = *(DWORD*)(player + 0x34); // UNIT_LIST_ROOT_OFF
        if (!listRoot) return;
        DWORD listHead = *(DWORD*)(listRoot + 0x1F0); // LIST_HEAD_OFF
        if (!listHead) return;
        DWORD listLength = *(DWORD*)(listRoot + 0x1F8); // LIST_LENGTH_OFF
        if (listLength == 0) return;
        DWORD firstNode = *(DWORD*)(listHead + 0x00); // NODE_NEXT_OFF
        if (!firstNode) return;
        DWORD selectedUnit = *(DWORD*)(firstNode + 0x08); // NODE_UNIT_OFF
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
    if (uMsg == WM_APP + 2) {
        KillSelectedUnit();
        return 0;
    }
    
    if (uMsg == WM_APP + 3) {
        SaveData* sd = (SaveData*)lParam;
        DWORD fakeKey = CreateFakeJassString(sd->key);
        DWORD fakeVal = CreateFakeJassString(sd->val);
        
        ConsoleLog("[Invoker] Sending manual SAVE request...\n");
        Monitor_SaveServerValue(sd->player, fakeKey, fakeVal);
        
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
            1, 1024, 1024, 0, NULL);
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
            char buffer[256];
            DWORD bytesRead;
            while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                
                if (strncmp(buffer, "KILL", 4) == 0) {
                    if (g_gameHwnd) {
                        SendMessageA(g_gameHwnd, WM_APP + 2, 0, 0);
                    }
                }
                else if (strncmp(buffer, "SAVE:", 5) == 0) {
                    char* ctx = nullptr;
                    strtok_s(buffer, ":", &ctx); // Skip SAVE
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
        }
        DisconnectNamedPipe(hPipe);
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
        
        g_MallMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 1, iniPathW);
        g_MapLevel = GetPrivateProfileIntW(L"Spoof", L"MapLevel", 50, iniPathW);
        
        char cfgLog[256];
        sprintf_s(cfgLog, sizeof(cfgLog), "  -> MallMode: %d (0=Off, 1=Whitelist, 2=All)\n  -> MapLevel: %d\n", g_MallMode, g_MapLevel);
        ConsoleLog(cfgLog);

        LoadMallWhitelist();

        ConsoleLog("[Spoofer] Applying VIP / Mall / Level Spoofs...\n");
        if (g_MallMode > 0) {
            InstallSpoofHook("DzAPI_Map_IsVIP", (void*)Spoof_True);
        }
        InstallSpoofHook("DzAPI_Map_GetMapLevel", (void*)Spoof_LevelDynamic);
        
        ConsoleLog("[Spoofer] Applying ServerValue & MallItem Spoofer/Dumper...\n");
        InstallTrampolineHook("DzAPI_Map_HasMallItem", (void*)Monitor_HasMallItem, hkHasMallItem);
        InstallTrampolineHook("DzAPI_Map_SaveServerValue", (void*)Monitor_SaveServerValue, hkSave);
        InstallTrampolineHook("DzAPI_Map_GetServerValue", (void*)Monitor_GetServerValue, hkGet);
        InstallTrampolineHook("DzAPI_Map_Stat_SetStat", (void*)Monitor_Stat_SetStat, hkSetStat);
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
