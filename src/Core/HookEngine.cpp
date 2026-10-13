#include "HookEngine.hpp"
#include "Memory.hpp"
#include "Common/Logger.hpp"

namespace War3Dev {

HookEngine& HookEngine::Get() {
    static HookEngine instance;
    return instance;
}

void* HookEngine::InstallTrampoline(const std::string& name, void* targetFunc, void* hookFunc, size_t stolenBytes) {
    if (!targetFunc || !hookFunc || stolenBytes < 5) {
        LOG("[-] HookEngine: Invalid parameters for hook '%s'\n", name.c_str());
        return nullptr;
    }

    // Allocate memory for stolen bytes + JMP back (5 bytes)
    size_t trampSize = stolenBytes + 5;
    BYTE* trampoline = static_cast<BYTE*>(VirtualAlloc(NULL, trampSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        LOG("[-] HookEngine: Failed to allocate trampoline for '%s'\n", name.c_str());
        return nullptr;
    }

    // 1. Copy stolen instructions
    memcpy(trampoline, targetFunc, stolenBytes);

    // 2. Append JMP back to targetFunc + stolenBytes
    trampoline[stolenBytes] = 0xE9;
    DWORD relBack = (reinterpret_cast<DWORD>(targetFunc) + static_cast<DWORD>(stolenBytes)) - 
                    (reinterpret_cast<DWORD>(trampoline) + static_cast<DWORD>(stolenBytes) + 5);
    memcpy(&trampoline[stolenBytes + 1], &relBack, 4);

    // 3. Patch targetFunc entry with JMP to hookFunc
    {
        ScopedProtect protect(targetFunc, stolenBytes);
        BYTE* target = static_cast<BYTE*>(targetFunc);
        target[0] = 0xE9;
        DWORD relHook = reinterpret_cast<DWORD>(hookFunc) - (reinterpret_cast<DWORD>(targetFunc) + 5);
        memcpy(&target[1], &relHook, 4);

        for (size_t i = 5; i < stolenBytes; ++i) {
            target[i] = 0x90; // NOP
        }
    }

    FlushInstructionCache(GetCurrentProcess(), targetFunc, stolenBytes);
    FlushInstructionCache(GetCurrentProcess(), trampoline, trampSize);

    HookRecord record;
    record.name = name;
    record.target = targetFunc;
    record.hook = hookFunc;
    record.trampoline = trampoline;
    record.stolenBytes = stolenBytes;
    m_hooks[name] = record;

    LOG("[HookEngine] Trampoline installed for '%s' (0x%p -> Trampoline 0x%p)\n", name.c_str(), targetFunc, trampoline);
    return trampoline;
}

bool HookEngine::InstallDirectJmp(const std::string& name, void* targetFunc, void* hookFunc) {
    if (!targetFunc || !hookFunc) return false;

    {
        ScopedProtect protect(targetFunc, 5);
        BYTE* target = static_cast<BYTE*>(targetFunc);
        target[0] = 0xE9;
        DWORD relAddr = reinterpret_cast<DWORD>(hookFunc) - (reinterpret_cast<DWORD>(targetFunc) + 5);
        memcpy(&target[1], &relAddr, 4);
    }

    FlushInstructionCache(GetCurrentProcess(), targetFunc, 5);

    HookRecord record;
    record.name = name;
    record.target = targetFunc;
    record.hook = hookFunc;
    record.trampoline = nullptr;
    record.stolenBytes = 5;
    m_hooks[name] = record;

    LOG("[HookEngine] Direct JMP installed for '%s' (0x%p -> 0x%p)\n", name.c_str(), targetFunc, hookFunc);
    return true;
}

const HookRecord* HookEngine::FindHook(const std::string& name) const {
    auto it = m_hooks.find(name);
    if (it != m_hooks.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace War3Dev
