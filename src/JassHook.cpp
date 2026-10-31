#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include "dz_registry.h"
#include "SaveParser.h"
#include "SaveCommand.h"
#include "HookSafety.h"

HMODULE g_hModule = NULL;
wchar_t g_DllDirectory[MAX_PATH] = {0};

const DWORD GAME_DLL_BASE = 0x6F000000;
const DWORD REGISTER_NATIVE_RVA = 0x7E3710;

typedef void(__fastcall *RegisterNative_t)(void* func_ptr, const char* name, const char* signature);
RegisterNative_t OriginalRegisterNative = nullptr;

constexpr int kNativeCapacity = 2048;
constexpr size_t kThunkStride = 48;
SRWLOCK g_registryLock = SRWLOCK_INIT;

struct NativeEntry {
    char name[64];
    char sig[64];
    void* func_ptr;
};
NativeEntry g_natives[kNativeCapacity];
int g_nativeCount = 0;

HWND g_gameHwnd = NULL;
WNDPROC OriginalWndProc = NULL;

int g_MallMode = 2;         // 0=Off, 1=Whitelist, 2=All, 3=Percent
int g_MallPercent = 80;     // Default 80%
int g_MapLevel = 50;        // Map Level spoof
int g_lastPlayerHandle = 0;  // Last detected player handle
volatile LONG g_luaTraceRequested = 0;

void AppendLuaSaveTrace(int player, const char* key, const char* value, int accepted) {
    if (InterlockedCompareExchange(&g_luaTraceRequested, 0, 0) == 0) return;
    wchar_t gamePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, gamePath, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(gamePath, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - gamePath), L"war3dev_death_trace.log");
    FILE* file = nullptr;
    if (_wfopen_s(&file, gamePath, L"a") != 0 || !file) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u\tSAVE\tplayer=%d key=%s value=%s result=%d\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            player, key, value, accepted);
    fclose(file);
}

void ConsoleLog(const char* msg) {
    OutputDebugStringA(msg);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteConsoleA(hConsole, msg, (DWORD)strlen(msg), &w, NULL);
    }
    
    // Save to dztrace.log (External tools read this asynchronously without blocking the game)
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
// Mall Whitelist & Blacklist
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
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[MallWhitelist] Loaded %zu items from mall_spoof.txt\n", g_mallWhitelist.size());
    ConsoleLog(log);
}

void LoadMallBlacklist() {
    g_mallBlacklist.clear();
    // Built-in trap / honeypots
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
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[MallBlacklist] Loaded %zu trap items from blacklist\n", g_mallBlacklist.size());
    ConsoleLog(log);
}

bool IsTrapHoneypot(const char* keyStr) {
    if (!keyStr || strcmp(keyStr, "(null)") == 0 || keyStr[0] == '\0') return true;
    if (strcmp(keyStr, "ecn3gHnB95") == 0) return true;
    if (g_mallBlacklist.find(keyStr) != g_mallBlacklist.end()) return true;

    // Real mall items are ALL_CAPS + digits + underscore only.
    // Any lowercase letter → honeypot / non-mall key.
    bool allUpperOrDigit = true;
    for (const char* p = keyStr; *p; ++p) {
        if (!isupper((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '_') {
            allUpperOrDigit = false;
            break;
        }
    }
    if (!allUpperOrDigit) return true;

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

// ==========================================
// Dynamic Map Save Keys Auto-Discovery (Zero map.j Dependency)
// ==========================================
CRITICAL_SECTION g_keyCs;
bool g_keyCsInitialized = false;
INIT_ONCE g_keyInitOnce = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK InitializeKeyLock(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g_keyCs);
    g_keyCsInitialized = true;
    return TRUE;
}
void EnsureKeyLock() { InitOnceExecuteOnce(&g_keyInitOnce, InitializeKeyLock, nullptr, nullptr); }
std::set<std::string> g_autoCapturedKeys;

void RecordDiscoveredKey(const char* keyStr) {
    if (!keyStr || keyStr[0] == '\0' || strcmp(keyStr, "(null)") == 0) return;
    if (IsTrapHoneypot(keyStr)) return;

    EnsureKeyLock();

    std::string k = keyStr;
    bool isNew = false;
    EnterCriticalSection(&g_keyCs);
    if (g_autoCapturedKeys.find(k) == g_autoCapturedKeys.end()) {
        g_autoCapturedKeys.insert(k);
        isNew = true;
    }
    LeaveCriticalSection(&g_keyCs);

    if (isNew) {
        wchar_t keyPath[MAX_PATH];
        swprintf_s(keyPath, MAX_PATH, L"%s\\discovered_keys.txt", g_DllDirectory);
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, keyPath, L"a") == 0 && fp) {
            fprintf(fp, "%s\n", k.c_str());
            fclose(fp);
        }
        char log[256];
        _snprintf_s(log, sizeof(log), _TRUNCATE, "[AutoDiscovery] Discovered save key: \"%s\" (Total: %zu)\n", k.c_str(), g_autoCapturedKeys.size());
        ConsoleLog(log);
    }
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
void* g_traceThunks[kNativeCapacity] = {};
void* g_traceOriginals[kNativeCapacity] = {};

// Called under g_registryLock. Existing code slots are immutable, so an old
// map can finish a call while a new registration supplies a different handler.
int FindOrAddNative(const char* name, const char* signature, void* handler) {
    if (!name || !signature || !handler || strlen(name) >= sizeof(g_natives[0].name) || strlen(signature) >= sizeof(g_natives[0].sig)) return -1;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, name) == 0) {
            strncpy_s(g_natives[i].sig, signature, _TRUNCATE);
            g_natives[i].func_ptr = handler;
            return i;
        }
    }
    if (g_nativeCount >= kNativeCapacity) return -1;
    int index = g_nativeCount++;
    strncpy_s(g_natives[index].name, name, _TRUNCATE);
    strncpy_s(g_natives[index].sig, signature, _TRUNCATE);
    g_natives[index].func_ptr = handler;
    return index;
}

void __cdecl GenericDzTraceHandler(int index, DWORD* orig_esp) {
    // Passive trace & save monitoring disabled per user configuration
}

void* CreateThunk(int index, void* original_func) {
    if (index < 0 || index >= kNativeCapacity || !original_func) return original_func;
    if (g_traceThunks[index]) {
        // A changed handler is called directly rather than rewriting executable
        // code that a previous map may still be using.
        return g_traceOriginals[index] == original_func ? g_traceThunks[index] : original_func;
    }
    if (!g_thunkBlock) {
        g_thunkBlock = VirtualAlloc(NULL, kNativeCapacity * kThunkStride,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_thunkBlock) { ConsoleLog("[DzTrace] Thunk allocation failed; using original handler.\n"); return original_func; }
    }
    BYTE* thunk = static_cast<BYTE*>(g_thunkBlock) + index * kThunkStride;
    size_t length = EmitTraceThunk(thunk, static_cast<unsigned>(index),
        reinterpret_cast<void*>(GenericDzTraceHandler), original_func);
    if (!FlushInstructionCache(GetCurrentProcess(), thunk, length)) {
        ConsoleLog("[DzTrace] Instruction-cache flush failed; using original handler.\n");
        return original_func;
    }
    g_traceOriginals[index] = original_func;
    g_traceThunks[index] = thunk;
    return thunk;
}

struct HookData {
    void* targetPtr = nullptr;
    void* originalCallable = nullptr;
    BYTE originalBytes[16] = {};
    size_t patchSize = 0;
    bool isHooked = false;
};
HookData hkRegister, hkHasMallItem, hkSave, hkGet, hkLevel;

bool IsExecutableAddress(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD access = info.Protect & 0xff;
    if (access != PAGE_EXECUTE && access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
    return reinterpret_cast<uintptr_t>(address) + size <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}

bool InstallCodeHook(void* target, void* replacement, HookData& outData, void** publishCallable = nullptr) {
    if (outData.isHooked) return outData.targetPtr == target;
    if (!IsExecutableAddress(target, 16) || !IsExecutableAddress(replacement, 1)) return false;
    BYTE* source = static_cast<BYTE*>(target);
    size_t span = X86PatchSpan(source);
    if (!span) { ConsoleLog("[Hook] Unsupported entry instructions; hook refused.\n"); return false; }
    BYTE* gateway = static_cast<BYTE*>(VirtualAlloc(NULL, span + 5, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!gateway) return false;
    memcpy(gateway, source, span);
    size_t cursor = 0;
    while (cursor < span) {
        bool relative = false;
        size_t length = X86EntryInstructionLength(source + cursor, relative);
        if (relative) {
            int32_t oldDelta; memcpy(&oldDelta, source + cursor + 1, 4);
            intptr_t destination = reinterpret_cast<intptr_t>(source + cursor + length) + oldDelta;
            int32_t delta = static_cast<int32_t>(destination - reinterpret_cast<intptr_t>(gateway + cursor + length));
            memcpy(gateway + cursor + 1, &delta, 4);
        }
        cursor += length;
    }
    WriteRelativeJump(gateway + span, source + span);
    DWORD oldGatewayProtect = 0;
    if (!VirtualProtect(gateway, span + 5, PAGE_EXECUTE_READ, &oldGatewayProtect) ||
        !FlushInstructionCache(GetCurrentProcess(), gateway, span + 5)) {
        VirtualFree(gateway, 0, MEM_RELEASE); return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(source, span, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(gateway, 0, MEM_RELEASE); return false;
    }
    memcpy(outData.originalBytes, source, span);
    outData.targetPtr = target;
    outData.originalCallable = gateway;
    outData.patchSize = span;
    if (publishCallable) *publishCallable = gateway;
    BYTE patch[16]; memset(patch, 0x90, sizeof(patch));
    WriteRelativeJump(patch, replacement);
    // The displacement was calculated for the stack buffer; rebase to target.
    int32_t delta = static_cast<int32_t>(reinterpret_cast<intptr_t>(replacement) - reinterpret_cast<intptr_t>(source + 5));
    memcpy(patch + 1, &delta, 4);
    memcpy(source, patch, span);
    bool flushed = FlushInstructionCache(GetCurrentProcess(), source, span) != FALSE;
    if (!flushed) {
        memcpy(source, outData.originalBytes, span);
        FlushInstructionCache(GetCurrentProcess(), source, span);
        DWORD ignoredRestore = 0;
        VirtualProtect(source, span, oldProtect, &ignoredRestore);
        if (publishCallable) *publishCallable = target;
        outData.originalCallable = target;
        ConsoleLog("[Hook] Cache flush failed; original entry restored.\n");
        // The gateway is kept alive in case a concurrent caller already entered it.
        return false;
    }
    DWORD ignored = 0;
    bool restored = VirtualProtect(source, span, oldProtect, &ignored) != FALSE;
    outData.isHooked = true;
    if (!flushed || !restored) ConsoleLog("[Hook] Warning: cache/protection finalization failed.\n");
    return true;
}

void InstallTrampolineHook(const char* nativeName, void* hookFunc, HookData& outData) {
    void* target = nullptr;
    AcquireSRWLockShared(&g_registryLock);
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, nativeName) == 0) { target = g_natives[i].func_ptr; break; }
    }
    ReleaseSRWLockShared(&g_registryLock);
    char log[256];
    bool installed = target && InstallCodeHook(target, hookFunc, outData);
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[Monitor] %s: %s\n", nativeName, installed ? "hook ready" : "unavailable or unsafe entry; skipped");
    ConsoleLog(log);
}

// Copy while protected by SEH. Pointers into the game's string pool must not
// escape the guarded read into logging, parsing or the key registry.
bool CopyJassString(DWORD value, char* output, size_t capacity) {
    if (!output || capacity == 0) return false;
    output[0] = '\0';
    __try {
        if (value <= 0x10000) return false;
        DWORD data = *reinterpret_cast<const DWORD*>(value + 8);
        if (data <= 0x10000) return false;
        const char* text = *reinterpret_cast<const char* const*>(data + 0x1c);
        if (reinterpret_cast<uintptr_t>(text) <= 0x10000) return false;
        strncpy_s(output, capacity, text, _TRUNCATE);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output[0] = '\0'; return false;
    }
}

// ==========================================
// Native Handlers & Spoofers
// ==========================================
void* FindNativeHandler(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, name) == 0) return g_natives[i].func_ptr;
    }
    return nullptr;
}

int __cdecl Monitor_SaveServerValue(int player, DWORD key_ptr, DWORD value_ptr) {
    if (player != 0) g_lastPlayerHandle = player;
    char keyBuffer[512], valueBuffer[2048];
    const char* keyStr = CopyJassString(key_ptr, keyBuffer, sizeof(keyBuffer)) ? keyBuffer : "(null)";
    const char* valStr = CopyJassString(value_ptr, valueBuffer, sizeof(valueBuffer)) ? valueBuffer : "(null)";

    if (keyStr && strcmp(keyStr, "(null)") != 0) {
        RecordDiscoveredKey(keyStr);
    }

    char log[512];

    void* fn = hkSave.originalCallable;
    if (!fn) fn = FindNativeHandler("DzAPI_Map_SaveServerValue");
    if (!fn) { ConsoleLog("[Save] Handler not ready; load a map first.\n"); return 0; }
    typedef int(__cdecl *FuncType)(int, DWORD, DWORD);
    int accepted = ((FuncType)fn)(player, key_ptr, value_ptr);
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[SaveServerValue] Player: %d | Key: \"%s\" | Value: \"%s\" | Native result: %d\n", player, keyStr, valStr, accepted);
    ConsoleLog(log);
    AppendLuaSaveTrace(player, keyStr, valStr, accepted);
    if (!accepted) return accepted;
    FILE* fp = nullptr;
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
        fprintf(fp, "Player %d SAVED: %s = %s\n", player, keyStr, valStr);
        fclose(fp);
    }

    return accepted;
}

DWORD __cdecl Monitor_GetServerValue(int player, DWORD key_ptr) {
    if (player != 0) g_lastPlayerHandle = player;
    char keyBuffer[512];
    const char* keyStr = CopyJassString(key_ptr, keyBuffer, sizeof(keyBuffer)) ? keyBuffer : "(null)";

    if (keyStr && strcmp(keyStr, "(null)") != 0) {
        RecordDiscoveredKey(keyStr);
    }

    void* fn = hkGet.originalCallable;
    if (!fn) fn = FindNativeHandler("DzAPI_Map_GetServerValue");
    if (!fn) fn = FindNativeHandler("DzAPI_Map_GetServerValueOrg");
    if (!fn) { ConsoleLog("[Get] Handler not ready; load a map first.\n"); return 0; }
    typedef DWORD(__cdecl *FuncType)(int, DWORD);
    DWORD result = ((FuncType)fn)(player, key_ptr);
    int cacheErrorCode = -1;
    if (auto errorFn = FindNativeHandler("DzAPI_Map_GetServerValueErrorCode")) {
        cacheErrorCode = ((int(__cdecl *)(int))errorFn)(player);
    }

    char valueBuffer[2048];
    const char* valStr = "";
    if (CopyJassString(result, valueBuffer, sizeof(valueBuffer))) {
        valStr = valueBuffer;
    } else if (result > 0 && result <= 0x10000) {
        _snprintf_s(valueBuffer, sizeof(valueBuffer), _TRUNCATE, "%u", result);
        valStr = valueBuffer;
    }

    char log[512];
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[GetServerValue] Player: %d | Key: \"%s\" | Cached Value: \"%s\" | Cache error code: %d\n", player, keyStr, valStr, cacheErrorCode);
    ConsoleLog(log);

    if (valStr[0] != '\0') {
        FILE* fp = nullptr;
        wchar_t savePath[MAX_PATH];
        swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
        if (_wfopen_s(&fp, savePath, L"a") == 0 && fp) {
            fprintf(fp, "Player %d LOADED: %s = %s\n", player, keyStr, valStr);
            fclose(fp);
        }
    }
    return result;
}

int __cdecl Monitor_HasMallItem(int player, DWORD key_ptr) {
    char keyBuffer[512];
    const char* keyStr = CopyJassString(key_ptr, keyBuffer, sizeof(keyBuffer)) ? keyBuffer : "(null)";

    // Call Original FIRST
    if (!hkHasMallItem.originalCallable) return 0;
    typedef int(__cdecl *FuncType)(int, DWORD);
    int originalResult = ((FuncType)hkHasMallItem.originalCallable)(player, key_ptr);

    // 1. Trap / Honeypot Blacklist check
    if (IsTrapHoneypot(keyStr)) {
        char log[256];
        _snprintf_s(log, sizeof(log), _TRUNCATE, "[Trap Neutralized] Item \"%s\" is Honeypot/Trap! Spoofed to 0 (Denied).\n", keyStr ? keyStr : "(null)");
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
    _snprintf_s(log, sizeof(log), _TRUNCATE, "[HasMallItem] Player: %d | Item: \"%s\" | Server: %d | Spoofed: %d\n", player, keyStr, originalResult, finalResult);
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

int __cdecl Spoof_True() {
    return 1;
}

int __cdecl Spoof_LevelDynamic() {
    return g_MapLevel;
}

void InstallSpoofHook(const char* nativeName, void* spoofFunc) {
    // Only the level hook exists in the current API. Its verified gateway is
    // kept so the installed code has the same lifetime rules as other hooks.
    if (strcmp(nativeName, "DzAPI_Map_GetMapLevel") == 0)
        InstallTrampolineHook(nativeName, spoofFunc, hkLevel);
}

// ==========================================
// Fake JassString Generator
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

// The installed alpha jass.dll's exec-lua handler loads a module via require.
// Its internal entry is used only after exact PE and instruction checks; never
// call init_lua_engine a second time in an already running game.
void RunLuaTraceModule(bool stop) {
    HMODULE engine = GetModuleHandleA("jass.dll");
    if (!engine) { ConsoleLog("[LuaTrace] jass.dll is not loaded.\n"); return; }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(engine);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(engine) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.TimeDateStamp != 0x68110ecb || nt->OptionalHeader.SizeOfImage != 0x737000 ||
        nt->OptionalHeader.CheckSum != 0x733a1d) {
        ConsoleLog("[LuaTrace] Unrecognized jass.dll; Lua entry refused.\n"); return;
    }
    BYTE* entry = reinterpret_cast<BYTE*>(engine) + 0x296530;
    const BYTE expected[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x10,0x56,0x57,0x8b,0x7d,0x08};
    if (!IsExecutableAddress(entry, sizeof(expected)) || memcmp(entry, expected, sizeof(expected)) != 0) {
        ConsoleLog("[LuaTrace] Lua entry changed; call refused.\n"); return;
    }
    void* originalCheat = *reinterpret_cast<void**>(reinterpret_cast<BYTE*>(engine) + 0x6dd6a0);
    if (!IsExecutableAddress(originalCheat, 1)) {
        ConsoleLog("[LuaTrace] Lua Cheat bridge is not initialized for this map.\n"); return;
    }
    const char* command = stop ? "exec-lua:death_trace_stop" : "exec-lua:death_trace";
    DWORD value = CreateFakeJassString(command);
    __try {
        (reinterpret_cast<void (__cdecl *)(DWORD)>(entry))(value);
        InterlockedExchange(&g_luaTraceRequested, stop ? 0 : 1);
        ConsoleLog(stop ? "[LuaTrace] Stop module requested.\n" : "[LuaTrace] Start module requested; inspect war3dev_death_trace.log for READY.\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[LuaTrace] Lua bridge raised an exception; request failed.\n");
    }
    FakeJassString* js = reinterpret_cast<FakeJassString*>(value);
    delete[] js->data->chars;
    delete js->data;
    delete js;
}

struct SaveData {
    int player;
    char key[128];
    char val[256];
};

struct BlinkData {
    float screenX; // normalized screen ratio 0.0 ~ 1.0
    float screenY;
};

// ==========================================
// Game.dll 1.27 KillUnit native. Invoke the engine's death path, never a raw
// health-field write (which can leave a live zero-HP unit).
// ==========================================
void* FindBlinkNative(const char* name, const char* signature);
using SelectedUnitFn = DWORD (__cdecl *)();
using KillUnitFn = void (__cdecl *)(DWORD);

bool DispatchSelectedKill(SelectedUnitFn selected, KillUnitFn kill) {
    if (!selected || !kill) return false;
    DWORD handle = selected();
    if (!handle) return false;
    kill(handle);
    return true;
}

void KillSelectedUnit() {
    __try {
        auto selected = reinterpret_cast<SelectedUnitFn>(FindBlinkNative("DzGetSelectedLeaderUnit", "()Hunit;"));
        if (!selected) { ConsoleLog("[Kill] Selected-unit native unavailable; no action.\n"); return; }
        HMODULE hGame = GetModuleHandleA("Game.dll");
        if (!hGame) { ConsoleLog("[Kill] Game.dll unavailable.\n"); return; }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hGame);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(hGame) + dos->e_lfanew);
        // Matches the Game.dll shipped in this workspace and verified in the
        // DzAPI research. A different patch must be researched before calling.
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.TimeDateStamp != 0x56bd0e1c ||
            nt->OptionalHeader.SizeOfImage != 0xcd8000 || nt->OptionalHeader.CheckSum != 0xc96841) {
            ConsoleLog("[Kill] Unrecognized Game.dll; KillUnit native call refused.\n"); return;
        }
        BYTE* entry = reinterpret_cast<BYTE*>(hGame) + 0x1efcd0;
        const BYTE expected[] = {0x55,0x8b,0xec,0x8b,0x4d,0x08};
        if (memcmp(entry, expected, sizeof(expected)) != 0) {
            ConsoleLog("[Kill] KillUnit entry modified; no action.\n"); return;
        }
        if (DispatchSelectedKill(selected, reinterpret_cast<KillUnitFn>(entry)))
            ConsoleLog("[Kill] Engine KillUnit called; check map death/drop logic for reward outcome.\n");
        else ConsoleLog("[Kill] No selected unit; no action.\n");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[Kill] Native call raised an exception; operation failed.\n");
    }
}

// Native wrappers verified against the registration table in the bundled
// 1.27 Game.dll. Keep all calls on the game's window thread, as with !kill.
using GroupCreateFn = DWORD (__cdecl *)();
using GroupDestroyFn = void (__cdecl *)(DWORD);
using GroupEnumRectFn = void (__cdecl *)(DWORD, DWORD, DWORD);
using GroupFirstFn = DWORD (__cdecl *)(DWORD);
using GroupRemoveFn = void (__cdecl *)(DWORD, DWORD);
using RectRemoveFn = void (__cdecl *)(DWORD);
using UnitHandleFn = DWORD (__cdecl *)(DWORD);
using PlayerEnemyFn = int (__cdecl *)(DWORD, DWORD);
using WidgetLifeFn = DWORD (__cdecl *)(DWORD);

struct MassKillNatives {
    GroupCreateFn createGroup;
    GroupDestroyFn destroyGroup;
    GroupEnumRectFn enumRect;
    GroupFirstFn first;
    GroupRemoveFn remove;
    RectRemoveFn removeRect;
    SelectedUnitFn worldBounds;
    SelectedUnitFn localPlayer;
    UnitHandleFn owner;
    PlayerEnemyFn isEnemy;
    WidgetLifeFn life;
    UnitHandleFn typeId;
    KillUnitFn kill;
};
struct MassKillTarget { DWORD handle, owner, typeId; };
static MassKillNatives g_massKill = {};
static std::vector<MassKillTarget> g_massTargets;
static size_t g_massCursor = 0;
static unsigned g_massKilled = 0;
static DWORD g_massPlayer = 0;
static HMODULE g_massGame = nullptr;
constexpr UINT_PTR kMassKillTimer = 0x574B;
constexpr size_t kMassKillBatch = 16;
constexpr size_t kMassKillMaxUnits = 32768;

static bool MassKillAlive(DWORD lifeBits) {
    float life;
    memcpy(&life, &lifeBits, sizeof(life));
    return life > 0.405f; // Warcraft's native life threshold.
}

static bool MassKillEligible(const MassKillNatives& n, const MassKillTarget& target, DWORD player) {
    return target.handle && target.owner && target.typeId &&
           n.typeId(target.handle) == target.typeId &&
           n.owner(target.handle) == target.owner &&
           n.isEnemy(target.owner, player) &&
           MassKillAlive(n.life(target.handle));
}

static void StopMassKill(HWND hwnd, const char* reason) {
    KillTimer(hwnd, kMassKillTimer);
    if (reason) {
        char log[192];
        _snprintf_s(log, sizeof(log), _TRUNCATE,
                    "[KillAll] %s Killed %u / %u queued units.\n", reason,
                    g_massKilled, static_cast<unsigned>(g_massTargets.size()));
        ConsoleLog(log);
    }
    g_massTargets.clear();
    g_massCursor = 0;
    g_massKilled = 0;
    g_massPlayer = 0;
    g_massGame = nullptr;
    g_massKill = {};
}

static bool ResolveMassKillNatives(HMODULE game, MassKillNatives& n) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (!game || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->FileHeader.TimeDateStamp != 0x56bd0e1c || nt->OptionalHeader.SizeOfImage != 0xcd8000 ||
        nt->OptionalHeader.CheckSum != 0xc96841) return false;
    BYTE* base = reinterpret_cast<BYTE*>(game);
    const BYTE frame[] = {0x55, 0x8b, 0xec};
    auto entry = [&](DWORD rva) -> void* {
        BYTE* p = base + rva;
        return IsExecutableAddress(p, sizeof(frame)) && memcmp(p, frame, sizeof(frame)) == 0 ? p : nullptr;
    };
    // GetLocalPlayer uses a different prologue in this build.
    const BYTE localPrefix[] = {0x56, 0x8b, 0x35};
    BYTE* local = base + 0x1e3150;
    if (!IsExecutableAddress(local, sizeof(localPrefix)) || memcmp(local, localPrefix, sizeof(localPrefix))) return false;
    n.createGroup = reinterpret_cast<GroupCreateFn>(entry(0x1de080));
    n.destroyGroup = reinterpret_cast<GroupDestroyFn>(entry(0x1df640));
    n.enumRect = reinterpret_cast<GroupEnumRectFn>(entry(0x1e6a50));
    n.first = reinterpret_cast<GroupFirstFn>(entry(0x1e0850));
    n.remove = reinterpret_cast<GroupRemoveFn>(entry(0x1e6e30));
    n.removeRect = reinterpret_cast<RectRemoveFn>(entry(0x1f2dd0));
    n.worldBounds = reinterpret_cast<SelectedUnitFn>(entry(0x1e6810));
    n.localPlayer = reinterpret_cast<SelectedUnitFn>(local);
    n.owner = reinterpret_cast<UnitHandleFn>(entry(0x1e3ba0));
    n.isEnemy = reinterpret_cast<PlayerEnemyFn>(entry(0x1e8090));
    n.life = reinterpret_cast<WidgetLifeFn>(entry(0x1e6730));
    n.typeId = reinterpret_cast<UnitHandleFn>(entry(0x1e6670));
    n.kill = reinterpret_cast<KillUnitFn>(entry(0x1efcd0));
    return n.createGroup && n.destroyGroup && n.enumRect && n.first && n.remove && n.removeRect &&
           n.worldBounds && n.localPlayer && n.owner && n.isEnemy && n.life && n.typeId && n.kill;
}

static void StartMassKill(HWND hwnd) {
    if (!g_massTargets.empty()) StopMassKill(hwnd, "Restarted.");
    __try {
        HMODULE game = GetModuleHandleA("Game.dll");
        MassKillNatives n = {};
        if (!ResolveMassKillNatives(game, n)) {
            ConsoleLog("[KillAll] Unsupported or modified Game.dll; no action.\n"); return;
        }
        DWORD player = n.localPlayer();
        DWORD rect = n.worldBounds();
        if (!player || !rect) { ConsoleLog("[KillAll] No active local player or map.\n"); return; }
        DWORD group = n.createGroup();
        if (!group) { n.removeRect(rect); ConsoleLog("[KillAll] Could not create unit group.\n"); return; }
        // A null boolexpr includes every unit. GetWorldBounds creates a rect
        // handle, so release both temporary handles after enumeration.
        n.enumRect(group, rect, 0);
        bool truncated = false;
        for (size_t i = 0; i < kMassKillMaxUnits; ++i) {
            DWORD unit = n.first(group);
            if (!unit) break;
            n.remove(group, unit);
            DWORD owner = n.owner(unit);
            if (owner && n.isEnemy(owner, player) && MassKillAlive(n.life(unit))) {
                DWORD typeId = n.typeId(unit);
                if (typeId) g_massTargets.push_back({unit, owner, typeId});
            }
            if (i + 1 == kMassKillMaxUnits && n.first(group)) truncated = true;
        }
        n.destroyGroup(group);
        n.removeRect(rect);
        if (g_massTargets.empty()) { ConsoleLog("[KillAll] No living enemy units found.\n"); return; }
        g_massKill = n;
        g_massGame = game;
        g_massPlayer = player;
        g_massCursor = 0;
        g_massKilled = 0;
        if (!SetTimer(hwnd, kMassKillTimer, 50, nullptr)) {
            StopMassKill(hwnd, "Timer unavailable."); return;
        }
        char log[192];
        _snprintf_s(log, sizeof(log), _TRUNCATE,
                    "[KillAll] Queued %u living enemy units%s; 16 per timer tick.\n",
                    static_cast<unsigned>(g_massTargets.size()), truncated ? " (scan limit reached)" : "");
        ConsoleLog(log);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        StopMassKill(hwnd, "Native call failed.");
    }
}

static void StepMassKill(HWND hwnd) {
    if (g_massTargets.empty()) return;
    __try {
        if (GetModuleHandleA("Game.dll") != g_massGame || g_massKill.localPlayer() != g_massPlayer) {
            StopMassKill(hwnd, "Game or local player changed."); return;
        }
        size_t end = g_massCursor + kMassKillBatch;
        if (end > g_massTargets.size()) end = g_massTargets.size();
        for (; g_massCursor < end; ++g_massCursor) {
            const MassKillTarget& target = g_massTargets[g_massCursor];
            if (MassKillEligible(g_massKill, target, g_massPlayer)) {
                g_massKill.kill(target.handle);
                ++g_massKilled;
            }
        }
        if (g_massCursor == g_massTargets.size()) StopMassKill(hwnd, "Finished.");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        StopMassKill(hwnd, "Native call failed; some units may already be dead.");
    }
}

// SetHeroStr/Agi/Int were matched to the Game.dll native-registration table
// in the shipped 1.27 build. Use their wrappers so the game updates derived
// stats instead of changing CAbilityHero fields directly.
void SetSelectedHeroAttributes() {
    constexpr int kAttributeValue = 100000000;
    __try {
        auto selected = reinterpret_cast<SelectedUnitFn>(FindBlinkNative("DzGetSelectedLeaderUnit", "()Hunit;"));
        if (!selected) { ConsoleLog("[HeroStats] Selected-unit native unavailable; load a map first.\n"); return; }
        DWORD unit = selected();
        if (!unit) { ConsoleLog("[HeroStats] No selected unit.\n"); return; }

        HMODULE hGame = GetModuleHandleA("Game.dll");
        if (!hGame) { ConsoleLog("[HeroStats] Game.dll unavailable.\n"); return; }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hGame);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(hGame) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.TimeDateStamp != 0x56bd0e1c || nt->OptionalHeader.SizeOfImage != 0xcd8000 ||
            nt->OptionalHeader.CheckSum != 0xc96841) {
            ConsoleLog("[HeroStats] Unrecognized Game.dll; attributes unchanged.\n"); return;
        }

        BYTE* base = reinterpret_cast<BYTE*>(hGame);
        const BYTE selectedTypeEntry[] = {0x55,0x8b,0xec,0x8b,0x4d,0x08};
        const BYTE heroTypeEntry[] = {0x55,0x8b,0xec,0x8b,0x45,0x08};
        const BYTE statEntry[] = {0x55,0x8b,0xec,0x8b,0x4d,0x08,0x56,0xe8};
        auto matches = [&](DWORD rva, const BYTE* bytes, size_t size) {
            BYTE* entry = base + rva;
            return IsExecutableAddress(entry, size) && memcmp(entry, bytes, size) == 0;
        };
        if (!matches(0x1e6670, selectedTypeEntry, sizeof(selectedTypeEntry)) ||
            !matches(0x1e7b50, heroTypeEntry, sizeof(heroTypeEntry)) ||
            !matches(0x1f5890, statEntry, sizeof(statEntry)) ||
            !matches(0x1f5720, statEntry, sizeof(statEntry)) ||
            !matches(0x1f5780, statEntry, sizeof(statEntry))) {
            ConsoleLog("[HeroStats] Native entry differs from verified build; attributes unchanged.\n"); return;
        }

        using GetUnitTypeIdFn = DWORD (__cdecl *)(DWORD);
        using IsHeroUnitIdFn = int (__cdecl *)(DWORD);
        using SetHeroStatFn = void (__cdecl *)(DWORD, int, int);
        DWORD typeId = reinterpret_cast<GetUnitTypeIdFn>(base + 0x1e6670)(unit);
        if (!typeId || !reinterpret_cast<IsHeroUnitIdFn>(base + 0x1e7b50)(typeId)) {
            ConsoleLog("[HeroStats] Selected unit is not a hero; attributes unchanged.\n"); return;
        }
        reinterpret_cast<SetHeroStatFn>(base + 0x1f5890)(unit, kAttributeValue, TRUE);
        reinterpret_cast<SetHeroStatFn>(base + 0x1f5720)(unit, kAttributeValue, TRUE);
        reinterpret_cast<SetHeroStatFn>(base + 0x1f5780)(unit, kAttributeValue, TRUE);
        char log[160];
        _snprintf_s(log, sizeof(log), _TRUNCATE,
                    "[HeroStats] Set Str/Agi/Int to %d for hero handle %u; inspect in-game display.\n",
                    kAttributeValue, unit);
        ConsoleLog(log);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[HeroStats] Native call raised an exception; result may be partial.\n");
    }
}


// Resolve actual JASS natives instead of private plugin globals / CWidget ABI.
void* FindBlinkNative(const char* name, const char* signature) {
    void* result = nullptr;
    AcquireSRWLockShared(&g_registryLock);
    for (int i = 0; i < g_nativeCount; ++i) {
        if (strcmp(g_natives[i].name, name) == 0 && strcmp(g_natives[i].sig, signature) == 0) {
            result = g_natives[i].func_ptr; break;
        }
    }
    ReleaseSRWLockShared(&g_registryLock);
    return result;
}

bool g_blinkArmed = false;
void BlinkSelectedUnit(float screenX, float screenY) {
    if (screenX < 0 && screenY < 0) {
        g_blinkArmed = true;
        ConsoleLog("[Blink] Armed: move the cursor onto game terrain and press Ctrl+B (Esc cancels).\n");
        return;
    }
    g_blinkArmed = false;
    __try {
        typedef DWORD (__cdecl *GetHandle)();
        typedef DWORD (__cdecl *GetReal)();
        typedef void (__cdecl *SetPosition)(DWORD, const float*, const float*);
        auto selected = reinterpret_cast<GetHandle>(FindBlinkNative("DzGetSelectedLeaderUnit", "()Hunit;"));
        auto mouseX = reinterpret_cast<GetReal>(FindBlinkNative("DzGetMouseTerrainX", "()R"));
        auto mouseY = reinterpret_cast<GetReal>(FindBlinkNative("DzGetMouseTerrainY", "()R"));
        auto move = reinterpret_cast<SetPosition>(FindBlinkNative("DzSetUnitPosition", "(Hunit;RR)V"));
        if (!selected || !mouseX || !mouseY || !move) {
            ConsoleLog("[Blink] Required natives unavailable; load a map / update the verified plugin registry.\n");
            return;
        }
        DWORD unit = selected();
        if (!unit) { ConsoleLog("[Blink] No selected unit.\n"); return; }
        DWORD bitsX = mouseX(), bitsY = mouseY();
        float x, y;
        memcpy(&x, &bitsX, sizeof(x)); memcpy(&y, &bitsY, sizeof(y));
        // Zero is a valid map coordinate; NaN / infinity are not.
        if (!(x >= -99999.0f && x <= 99999.0f && y >= -99999.0f && y <= 99999.0f)) {
            ConsoleLog("[Blink] Invalid terrain coordinates; move cancelled.\n"); return;
        }
        move(unit, &x, &y);
        char log[160];
        _snprintf_s(log, sizeof(log), _TRUNCATE, "[Blink] Position native called: handle %u -> (%.1f, %.1f)\n", unit, x, y);
        ConsoleLog(log);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ConsoleLog("[Blink] Native call failed; check plugin compatibility.\n");
    }
}
// -- Main Thread Execution via WndProc Hook --
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // In-game Hotkeys:
    //   Ctrl + K: Instant Kill selected unit
    //   Ctrl + Shift + K: Kill every living enemy unit on this client
    //   Ctrl + Shift + X: Cancel the queued mass kill
    //   Ctrl + B: Instant Blink selected unit to mouse cursor
    //   Ctrl + H: Set selected hero's three attributes to 100 million
    if (uMsg == WM_KEYDOWN) {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrlDown && shiftDown && wParam == 'K') {
            if ((static_cast<DWORD>(lParam) & (1u << 30)) == 0) StartMassKill(hWnd);
            return 0;
        }
        if (ctrlDown && shiftDown && wParam == 'X') {
            StopMassKill(hWnd, "Cancelled.");
            return 0;
        }
        if (ctrlDown && wParam == 'K') {
            KillSelectedUnit();
            return 0;
        }
        if (wParam == VK_ESCAPE) g_blinkArmed = false;
        if (ctrlDown && wParam == 'B') {
            BlinkSelectedUnit(0.0f, 0.0f);
            return 0;
        }
        if (ctrlDown && wParam == 'H') {
            if ((static_cast<DWORD>(lParam) & (1u << 30)) == 0) SetSelectedHeroAttributes();
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
            DWORD fakeVal = 0;
            bool isAllocatedVal = false;
            unsigned int strId = 0;
            if (sscanf_s(sd->val, "<StringID: %u>", &strId) == 1) {
                fakeVal = strId;
            } else {
                fakeVal = CreateFakeJassString(sd->val);
                isAllocatedVal = true;
            }

            char logBuf[512];
            _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Manual SAVE request -> Player: %d | Key: \"%s\" | Value: \"%s\"\n",
                      sd->player, sd->key, sd->val);
            ConsoleLog(logBuf);

            Monitor_SaveServerValue(sd->player, fakeKey, fakeVal);

            auto freeJassStr = [](DWORD ptr) {
                FakeJassString* js = reinterpret_cast<FakeJassString*>(ptr);
                if (js) { delete[] js->data->chars; delete js->data; delete js; }
            };
            freeJassStr(fakeKey);
            if (isAllocatedVal) freeJassStr(fakeVal);
        }
        return 0;
    }

    if (uMsg == WM_APP + 4) {
        SaveData* sd = (SaveData*)lParam;
        if (sd) {
            int targetPlayer = sd->player;
            if (targetPlayer == 0 && g_lastPlayerHandle != 0) {
                targetPlayer = g_lastPlayerHandle;
            }
            DWORD fakeKey = CreateFakeJassString(sd->key);

            Monitor_GetServerValue(targetPlayer, fakeKey);

            FakeJassString* js = reinterpret_cast<FakeJassString*>(fakeKey);
            if (js) { delete[] js->data->chars; delete js->data; delete js; }
        }
        return 0;
    }

    if (uMsg == WM_APP + 5) {
        BlinkData* bd = (BlinkData*)lParam;
        if (bd) BlinkSelectedUnit(bd->screenX, bd->screenY);
        else    BlinkSelectedUnit(0.0f, 0.0f);
        return 0;
    }

    if (uMsg == WM_APP + 6) {
        RunLuaTraceModule(wParam != 0);
        return 0;
    }

    if (uMsg == WM_APP + 7) {
        SetSelectedHeroAttributes();
        return 0;
    }

    if (uMsg == WM_APP + 8) {
        if (wParam) StopMassKill(hWnd, "Cancelled.");
        else StartMassKill(hWnd);
        return 0;
    }

    if (uMsg == WM_TIMER && wParam == kMassKillTimer) {
        StepMassKill(hWnd);
        return 0;
    }

    if (uMsg == WM_DESTROY && !g_massTargets.empty()) StopMassKill(hWnd, "Window closed.");

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
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Execution Success (Float Arg: %f).\n", val);
            }
            else if (data->arg1 && data->arg2 && data->arg3) {
                int v1 = atoi(data->arg1);
                int v2 = atoi(data->arg2);
                int v3 = atoi(data->arg3);
                typedef void(__cdecl *Func3I)(int, int, int);
                ((Func3I)func_ptr)(v1, v2, v3);
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Execution Success (Args: %d, %d, %d).\n", v1, v2, v3);
            }
            else if (data->arg1 && data->arg2) {
                int v1 = atoi(data->arg1);
                int v2 = atoi(data->arg2);
                typedef void(__cdecl *Func2I)(int, int);
                ((Func2I)func_ptr)(v1, v2);
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Execution Success (Args: %d, %d).\n", v1, v2);
            }
            else if (data->arg1) {
                int v1 = atoi(data->arg1);
                typedef void(__cdecl *Func1I)(int);
                ((Func1I)func_ptr)(v1);
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Execution Success (Arg: %d).\n", v1);
            }
            else {
                typedef void(__cdecl *Func0)();
                ((Func0)func_ptr)();
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Execution Success (0 Args).\n");
            }
            
            ConsoleLog(logBuf);
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

    if (cmd == "LUA_TRACE_START" || cmd == "LUA_TRACE_STOP") {
        if (g_gameHwnd) SendMessageA(g_gameHwnd, WM_APP + 6, cmd == "LUA_TRACE_STOP", 0);
        else ConsoleLog("[LuaTrace] Game window unavailable; request skipped.\n");
    }
    else if (cmd == "HERO_100M") {
        if (g_gameHwnd) SendMessageA(g_gameHwnd, WM_APP + 7, 0, 0);
        else ConsoleLog("[HeroStats] Game window unavailable; request skipped.\n");
    }
    else if (cmd == "KILL_ALL_LOCAL" || cmd == "KILL_ALL_STOP") {
        if (g_gameHwnd) SendMessageA(g_gameHwnd, WM_APP + 8, cmd == "KILL_ALL_STOP", 0);
        else ConsoleLog("[KillAll] Game window unavailable; request skipped.\n");
    }
    else if (cmd == "KILL" || cmd == "!kill" || cmd == "!killselected") {
        if (g_gameHwnd) {
            SendMessageA(g_gameHwnd, WM_APP + 2, 0, 0);
        } else {
            KillSelectedUnit();
        }
    }
    else if (cmd.rfind("BLINK:", 0) == 0) {
        // Format: BLINK:<screenX>:<screenY>  (both 0.0~1.0 normalized)
        const char* rest = cmd.c_str() + 6;
        char buf[64];
        strncpy_s(buf, rest, _TRUNCATE);
        char* ctx = nullptr;
        char* tokX = strtok_s(buf, ":", &ctx);
        char* tokY = strtok_s(nullptr, ":", &ctx);
        if (tokX && tokY && g_gameHwnd) {
            BlinkData bd;
            bd.screenX = (float)atof(tokX);
            bd.screenY = (float)atof(tokY);
            SendMessageA(g_gameHwnd, WM_APP + 5, 0, (LPARAM)&bd);
        }
    }
    else if (cmd.rfind("SAVE:", 0) == 0 || (cmd.find("Player") != std::string::npos && cmd.find("Key") != std::string::npos)) {
        std::string p, k, v;
        if (ParseSaveLine(cmd, "1048602", p, k, v) && g_gameHwnd && ValidateSaveCommand(p,k,v,true)) {
            SaveData sd;
            sd.player = atoi(p.c_str());
            strncpy_s(sd.key, k.c_str(), _TRUNCATE);
            strncpy_s(sd.val, v.c_str(), _TRUNCATE);
            SendMessageA(g_gameHwnd, WM_APP + 3, 0, (LPARAM)&sd);
            char logBuf[256];
            _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[SaveHook] Dispatched Save to game thread: Player %d | %s = %s\n", sd.player, sd.key, sd.val);
            ConsoleLog(logBuf);
        } else ConsoleLog("[SaveHook] Rejected malformed/oversized save or game window unavailable.\n");
    }
    else if (cmd.rfind("GET:", 0) == 0 || cmd.rfind("READ:", 0) == 0) {
        char buf[512];
        strncpy_s(buf, cmd.c_str(), _TRUNCATE);
        char* ctx = nullptr;
        strtok_s(buf, ":", &ctx);
        char* pStr = strtok_s(nullptr, ":", &ctx);
        char* kStr = strtok_s(nullptr, ":", &ctx);
        
        std::string p, k;
        if (kStr) {
            p = pStr;
            k = kStr;
        } else if (pStr) {
            k = pStr;
            p = std::to_string(g_lastPlayerHandle != 0 ? g_lastPlayerHandle : 1048602);
        }

        if (g_gameHwnd && ValidateSaveCommand(p,k,"",false)) {
            SaveData sd;
            sd.player = atoi(p.c_str());
            if (sd.player == 0 && g_lastPlayerHandle != 0) sd.player = g_lastPlayerHandle;
            strncpy_s(sd.key, k.c_str(), _TRUNCATE);
            sd.val[0] = '\0';
            SendMessageA(g_gameHwnd, WM_APP + 4, 0, (LPARAM)&sd);
        } else ConsoleLog("[Get] Rejected malformed/oversized key or game window unavailable.\n");
    }
    else if (cmd == "READ_ALL" || cmd.rfind("READ_ALL:", 0) == 0) {
        int targetPlayer = g_lastPlayerHandle != 0 ? g_lastPlayerHandle : 1048602;
        if (cmd.rfind("READ_ALL:", 0) == 0) {
            int p = atoi(cmd.substr(9).c_str());
            if (p != 0) targetPlayer = p;
        }

        std::set<std::string> allKeysToQuery;

        // 1. Load from map_keys.txt if available
        wchar_t keyPath[MAX_PATH];
        swprintf_s(keyPath, MAX_PATH, L"%s\\map_keys.txt", g_DllDirectory);
        std::ifstream kfile(keyPath);
        if (!kfile.is_open()) kfile.open("map_keys.txt");
        if (kfile.is_open()) {
            std::string line;
            while (std::getline(kfile, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
                while (!line.empty() && line.front() == ' ') line.erase(line.begin());
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                allKeysToQuery.insert(line);
            }
            kfile.close();
        }

        // 2. Load from discovered_keys.txt (automatically captured history)
        wchar_t discPath[MAX_PATH];
        swprintf_s(discPath, MAX_PATH, L"%s\\discovered_keys.txt", g_DllDirectory);
        std::ifstream dfile(discPath);
        if (!dfile.is_open()) dfile.open("discovered_keys.txt");
        if (dfile.is_open()) {
            std::string line;
            while (std::getline(dfile, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
                while (!line.empty() && line.front() == ' ') line.erase(line.begin());
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                allKeysToQuery.insert(line);
            }
            dfile.close();
        }

        // 3. Merge in-memory discovered keys
        EnsureKeyLock();
        EnterCriticalSection(&g_keyCs);
        for (const auto& k : g_autoCapturedKeys) {
            allKeysToQuery.insert(k);
        }
        LeaveCriticalSection(&g_keyCs);

        if (!allKeysToQuery.empty()) {
            int count = 0;
            char startLog[256];
            _snprintf_s(startLog, sizeof(startLog), _TRUNCATE, "[Control] Starting READ_ALL for Player %d (Total: %zu keys across map_keys + auto-discovery)...\n",
                      targetPlayer, allKeysToQuery.size());
            ConsoleLog(startLog);
            for (const auto& k : allKeysToQuery) {
                if (g_gameHwnd) {
                    SaveData sd;
                    sd.player = targetPlayer;
                    if (!ValidateSaveCommand(std::to_string(targetPlayer), k, "", false)) continue;
                    strncpy_s(sd.key, k.c_str(), _TRUNCATE);
                    sd.val[0] = '\0';
                    SendMessageA(g_gameHwnd, WM_APP + 4, 0, (LPARAM)&sd);
                    count++;
                    Sleep(15);
                }
            }
            char endLog[256];
            _snprintf_s(endLog, sizeof(endLog), _TRUNCATE, "[Control] READ_ALL completed: queried %d keys from server.\n", count);
            ConsoleLog(endLog);
        } else {
            ConsoleLog("[Control] READ_ALL failed: No keys found in map_keys.txt or auto-discovery cache!\n");
        }
    }
    else if (cmd.rfind("MALL:", 0) == 0) {
        int mode = atoi(cmd.substr(5).c_str());
        g_MallMode = mode;
        SaveConfigValue(L"MallMode", g_MallMode);
        char log[128];
        _snprintf_s(log, sizeof(log), _TRUNCATE, "[Control] MallMode changed to: %d (Saved to INI)\n", g_MallMode);
        ConsoleLog(log);
    }
    else if (cmd.rfind("PERCENT:", 0) == 0) {
        int pct = atoi(cmd.substr(8).c_str());
        if (pct < 1) pct = 1;
        if (pct > 100) pct = 100;
        g_MallPercent = pct;
        SaveConfigValue(L"MallPercent", g_MallPercent);
        char log[128];
        _snprintf_s(log, sizeof(log), _TRUNCATE, "[Control] MallPercent changed to: %d%% (Saved to INI)\n", g_MallPercent);
        ConsoleLog(log);
    }
    else if (cmd == "RELOAD_LISTS") {
        LoadMallWhitelist();
        LoadMallBlacklist();
        ConsoleLog("[Control] Whitelist & Blacklist reloaded successfully.\n");
    }
    else if (cmd.rfind("NATIVE:", 0) == 0) {
        char buf[512];
        strncpy_s(buf, cmd.c_str(), _TRUNCATE);
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
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Requesting Execution: %s | Sig: %s\n", found->name, found->sig);
                ConsoleLog(logBuf);
                
                if (g_gameHwnd) {
                    InvokeData data;
                    data.func_ptr = found->func_ptr;
                    data.sig = found->sig;
                    data.arg1 = arg1Str;
                    data.arg2 = arg2Str;
                    data.arg3 = arg3Str;
                    SendMessageA(g_gameHwnd, WM_APP + 1, 0, (LPARAM)&data);
                } else {
                    ConsoleLog("[Invoker] Error: Game HWND not found!\n");
                }
            } else {
                _snprintf_s(logBuf, sizeof(logBuf), _TRUNCATE, "[Invoker] Error: Native '%s' not found in Registry!\n", nativeName);
                ConsoleLog(logBuf);
            }
        }
    }
}

// Pipe Thread (Pure Inbound Command Receiver - NEVER blocks the game engine)
DWORD WINAPI PipeServerThread(LPVOID lpParam) {
    while (true) {
        HANDLE hPipe = CreateNamedPipeA(
            "\\\\.\\pipe\\War3JassDevTool",
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 65536, 65536, 0, NULL);
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        bool connected = ConnectNamedPipe(hPipe, NULL) ? true : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected) {
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
            
            DisconnectNamedPipe(hPipe);
        }
        CloseHandle(hPipe);
    }
    return 0;
}

void __fastcall HookedRegisterNative(void* func_ptr, const char* name, const char* signature) {
    AcquireSRWLockExclusive(&g_registryLock);
    FindOrAddNative(name, signature, func_ptr);
    void* final_ptr = func_ptr;
    if (name) {
        if (strcmp(name, "DzAPI_Map_SaveServerValue") == 0 && !hkSave.isHooked) {
            hkSave.targetPtr = func_ptr; hkSave.originalCallable = func_ptr; final_ptr = reinterpret_cast<void*>(Monitor_SaveServerValue);
        } else if (strcmp(name, "DzAPI_Map_GetServerValue") == 0 && !hkGet.isHooked) {
            hkGet.targetPtr = func_ptr; hkGet.originalCallable = func_ptr; final_ptr = reinterpret_cast<void*>(Monitor_GetServerValue);
        } else if (strcmp(name, "DzAPI_Map_HasMallItem") == 0 && !hkHasMallItem.isHooked) {
            hkHasMallItem.targetPtr = func_ptr; hkHasMallItem.originalCallable = func_ptr; final_ptr = reinterpret_cast<void*>(Monitor_HasMallItem);
        } else if (strcmp(name, "DzAPI_Map_GetMapLevel") == 0) {
            final_ptr = reinterpret_cast<void*>(Spoof_LevelDynamic);
        }
    }
    ReleaseSRWLockExclusive(&g_registryLock);
    OriginalRegisterNative(final_ptr, name, signature);
}

bool InstallHook(void* registerAddress) {
    // Publish the callable gateway before exposing the jump to the game thread.
    if (!InstallCodeHook(registerAddress, reinterpret_cast<void*>(HookedRegisterNative), hkRegister, reinterpret_cast<void**>(&OriginalRegisterNative))) return false;
    OriginalRegisterNative = reinterpret_cast<RegisterNative_t>(hkRegister.originalCallable);
    return true;
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

bool MatchesPluginRegistry(HMODULE plugin) {
    if (!plugin) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(plugin);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const BYTE*>(plugin) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt->FileHeader.TimeDateStamp == kDzPluginTimeDateStamp && nt->OptionalHeader.SizeOfImage == kDzPluginSizeOfImage &&
        nt->OptionalHeader.CheckSum == kDzPluginCheckSum;
}

DWORD WINAPI MainThread(LPVOID lpParam) {
    AllocConsole();
    EnsureKeyLock();
    wchar_t savePath[MAX_PATH];
    swprintf_s(savePath, MAX_PATH, L"%s\\local_save.txt", g_DllDirectory);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, savePath, L"w") == 0 && fp) {
        fprintf(fp, "=== Warcraft III Local Save Log ===\n"); fclose(fp);
    }
    ConsoleLog("========================================\n[JassHook] Safe initialization started.\n========================================\n");
    wchar_t iniPathW[MAX_PATH];
    swprintf_s(iniPathW, MAX_PATH, L"%s\\JassHookConfig.ini", g_DllDirectory);
    g_MallMode = GetPrivateProfileIntW(L"Spoof", L"MallMode", 2, iniPathW);
    g_MallPercent = GetPrivateProfileIntW(L"Spoof", L"MallPercent", 80, iniPathW);
    g_MapLevel = GetPrivateProfileIntW(L"Spoof", L"MapLevel", 50, iniPathW);
    LoadMallWhitelist(); LoadMallBlacklist();

    HANDLE pipeThread = CreateThread(NULL, 0, PipeServerThread, NULL, 0, NULL);
    if (pipeThread) CloseHandle(pipeThread);
    else ConsoleLog("[IPC] Failed to create pipe server thread.\n");

    HMODULE hGame = nullptr;
    for (int attempt = 0; attempt < 300 && !hGame; ++attempt) {
        hGame = GetModuleHandleA("Game.dll");
        if (!hGame) Sleep(100);
    }
    if (!hGame) { ConsoleLog("[JassHook] Game.dll did not load within 30 seconds; initialization stopped.\n"); return 1; }
    const BYTE expectedGameEntry[] = {0x55,0x8b,0xec,0x81,0xec,0x0c,0x01,0x00,0x00};
    void* registerAddress = reinterpret_cast<BYTE*>(hGame) + REGISTER_NATIVE_RVA;
    if (!IsExecutableAddress(registerAddress, 16) || memcmp(registerAddress, expectedGameEntry, sizeof(expectedGameEntry)) != 0) {
        ConsoleLog("[JassHook] Unsupported or already modified Game.dll register entry; no patch applied.\n"); return 1;
    }
    if (!InstallHook(registerAddress)) { ConsoleLog("[JassHook] Native registration hook installation failed.\n"); return 1; }
    ConsoleLog("[JassHook] Native registration gateway installed (complete instructions).\n");

    HMODULE plugin = nullptr;
    for (int attempt = 0; attempt < 100 && !plugin; ++attempt) {
        plugin = GetModuleHandleA("War3Plugin.dll");
        if (!plugin) Sleep(100);
    }
    if (plugin && MatchesPluginRegistry(plugin)) {
        ConsoleLog("[JassHook] Plugin identity verified; loading matching native registry for late injection.\n");
        AcquireSRWLockExclusive(&g_registryLock);
        for (int i = 0; i < g_dzTableCount; ++i) {
            void* handler = reinterpret_cast<BYTE*>(plugin) + g_dzTable[i].rva;
            if (IsExecutableAddress(handler, 16)) FindOrAddNative(g_dzTable[i].name, g_dzTable[i].sig, handler);
        }
        ReleaseSRWLockExclusive(&g_registryLock);
        InstallSpoofHook("DzAPI_Map_GetMapLevel", reinterpret_cast<void*>(Spoof_LevelDynamic));
        InstallTrampolineHook("DzAPI_Map_HasMallItem", reinterpret_cast<void*>(Monitor_HasMallItem), hkHasMallItem);
        InstallTrampolineHook("DzAPI_Map_SaveServerValue", reinterpret_cast<void*>(Monitor_SaveServerValue), hkSave);
        InstallTrampolineHook("DzAPI_Map_GetServerValue", reinterpret_cast<void*>(Monitor_GetServerValue), hkGet);
    } else {
        ConsoleLog(plugin ? "[JassHook] Unrecognized War3Plugin.dll version; static offsets disabled. Load a map to capture real native registrations.\n"
            : "[JassHook] War3Plugin.dll not loaded; waiting for actual native registrations on map load.\n");
    }
    EnumData edata = {GetCurrentProcessId(), NULL};
    for (int attempt = 0; attempt < 50 && !edata.hWnd; ++attempt) {
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&edata));
        if (!edata.hWnd) Sleep(100);
    }
    if (edata.hWnd) {
        g_gameHwnd = edata.hWnd;
        SetLastError(ERROR_SUCCESS);
        LONG previous = SetWindowLongA(edata.hWnd, GWL_WNDPROC, static_cast<LONG>(reinterpret_cast<LONG_PTR>(HookedWndProc)));
        if (!previous && GetLastError() != ERROR_SUCCESS) {
            g_gameHwnd = NULL; ConsoleLog("[JassHook] WndProc installation failed; game-thread commands unavailable.\n");
        } else {
            OriginalWndProc = reinterpret_cast<WNDPROC>(static_cast<LONG_PTR>(previous));
            ConsoleLog("[JassHook] Game window connected; initialization ready.\n");
        }
    } else ConsoleLog("[JassHook] Game window not found; game-thread commands unavailable.\n");
    return 0;
}

#ifndef WAR3DEVTOOL_HOOK_TEST
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

#endif
