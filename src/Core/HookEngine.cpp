#include "HookEngine.hpp"
#include "Memory.hpp"
#include "Common/Logger.hpp"

namespace War3Dev {

bool PatchHook::Install(void* target, void* hook) {
    if (!target || !hook) return false;

    m_target = target;
    m_hook = hook;

    // Backup original 5 bytes
    {
        ScopedProtect protect(m_target, 5);
        memcpy(m_originalBytes, m_target, 5);

        BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
        DWORD relAddr = reinterpret_cast<DWORD>(m_hook) - (reinterpret_cast<DWORD>(m_target) + 5);
        memcpy(&jmp[1], &relAddr, 4);
        memcpy(m_target, jmp, 5);
    }

    FlushInstructionCache(GetCurrentProcess(), m_target, 5);
    m_installed = true;
    return true;
}

void PatchHook::Unhook() {
    if (!m_target || !m_installed) return;

    ScopedProtect protect(m_target, 5);
    memcpy(m_target, m_originalBytes, 5);
    FlushInstructionCache(GetCurrentProcess(), m_target, 5);
}

void PatchHook::Rehook() {
    if (!m_target || !m_installed) return;

    ScopedProtect protect(m_target, 5);
    BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
    DWORD relAddr = reinterpret_cast<DWORD>(m_hook) - (reinterpret_cast<DWORD>(m_target) + 5);
    memcpy(&jmp[1], &relAddr, 4);
    memcpy(m_target, jmp, 5);
    FlushInstructionCache(GetCurrentProcess(), m_target, 5);
}

HookEngine& HookEngine::Get() {
    static HookEngine instance;
    return instance;
}

bool HookEngine::InstallSpoof(const std::string& name, void* targetFunc, void* spoofFunc) {
    if (!targetFunc || !spoofFunc) {
        LOG("[-] HookEngine: Target or spoof function null for '%s'\n", name.c_str());
        return false;
    }

    {
        ScopedProtect protect(targetFunc, 5);
        BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
        DWORD relAddr = reinterpret_cast<DWORD>(spoofFunc) - (reinterpret_cast<DWORD>(targetFunc) + 5);
        memcpy(&jmp[1], &relAddr, 4);
        memcpy(targetFunc, jmp, 5);
    }

    FlushInstructionCache(GetCurrentProcess(), targetFunc, 5);
    LOG("[HookEngine] Spoof installed for '%s' (0x%p -> 0x%p)\n", name.c_str(), targetFunc, spoofFunc);
    return true;
}

PatchHook* HookEngine::CreatePatchHook(const std::string& name, void* targetFunc, void* hookFunc) {
    if (!targetFunc || !hookFunc) return nullptr;

    PatchHook& hook = m_patchHooks[name];
    if (hook.Install(targetFunc, hookFunc)) {
        LOG("[HookEngine] PatchHook installed for '%s' (0x%p -> 0x%p)\n", name.c_str(), targetFunc, hookFunc);
        return &hook;
    }
    return nullptr;
}

PatchHook* HookEngine::GetHook(const std::string& name) {
    auto it = m_patchHooks.find(name);
    if (it != m_patchHooks.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace War3Dev
