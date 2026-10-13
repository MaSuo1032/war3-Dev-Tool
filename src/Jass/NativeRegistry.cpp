#include "NativeRegistry.hpp"
#include "Game/GameDefines.hpp"
#include "Core/Memory.hpp"
#include "Common/Logger.hpp"

namespace War3Dev {

static NativeRegistry* s_instance = nullptr;

NativeRegistry& NativeRegistry::Get() {
    static NativeRegistry instance;
    s_instance = &instance;
    return instance;
}

void __fastcall NativeRegistry::HookedRegisterNative(void* func_ptr, const char* name, const char* signature) {
    if (s_instance) {
        // Temporarily unhook to invoke real RegisterNative
        {
            ScopedProtect protect(s_instance->m_hookTarget, 5);
            memcpy(s_instance->m_hookTarget, s_instance->m_origBytes, 5);
        }

        s_instance->Register(name, signature, func_ptr);

        // Call original RegisterNative
        s_instance->m_origRegisterNative(func_ptr, name, signature);

        // Rehook
        {
            ScopedProtect protect(s_instance->m_hookTarget, 5);
            BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
            DWORD relAddr = reinterpret_cast<DWORD>(HookedRegisterNative) - (reinterpret_cast<DWORD>(s_instance->m_hookTarget) + 5);
            memcpy(&jmp[1], &relAddr, 4);
            memcpy(s_instance->m_hookTarget, jmp, 5);
        }
    }
}

void NativeRegistry::Initialize(HMODULE hGame) {
    if (!hGame) return;

    m_hookTarget = reinterpret_cast<void*>(reinterpret_cast<DWORD>(hGame) + REGISTER_NATIVE_RVA);
    m_origRegisterNative = reinterpret_cast<RegisterNative_t>(m_hookTarget);

    memcpy(m_origBytes, m_hookTarget, 5);

    {
        ScopedProtect protect(m_hookTarget, 5);
        BYTE jmp[5] = { 0xE9, 0x00, 0x00, 0x00, 0x00 };
        DWORD relAddr = reinterpret_cast<DWORD>(HookedRegisterNative) - (reinterpret_cast<DWORD>(m_hookTarget) + 5);
        memcpy(&jmp[1], &relAddr, 4);
        memcpy(m_hookTarget, jmp, 5);
    }

    FlushInstructionCache(GetCurrentProcess(), m_hookTarget, 5);
    LOG("[NativeRegistry] RegisterNative hooked at 0x%p\n", m_hookTarget);
}

void NativeRegistry::PrepopulateFromModules(HMODULE hGame, HMODULE hPlugin) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (hGame) {
        for (size_t i = 0; i < g_blizzardNativesCount; ++i) {
            void* ptr = reinterpret_cast<void*>(reinterpret_cast<DWORD>(hGame) + g_blizzardNatives[i].rva);
            m_natives[g_blizzardNatives[i].name] = { g_blizzardNatives[i].name, g_blizzardNatives[i].sig, ptr };
        }
    }

    if (hPlugin) {
        for (size_t i = 0; i < g_dzApiNativesCount; ++i) {
            void* ptr = reinterpret_cast<void*>(reinterpret_cast<DWORD>(hPlugin) + g_dzApiNatives[i].rva);
            m_natives[g_dzApiNatives[i].name] = { g_dzApiNatives[i].name, g_dzApiNatives[i].sig, ptr };
        }
    }

    LOG("[NativeRegistry] Prepopulated with %zu Blizzard & %zu DzAPI entries.\n", 
        g_blizzardNativesCount, hPlugin ? g_dzApiNativesCount : 0);
}

void NativeRegistry::Register(const char* name, const char* sig, void* funcPtr) {
    if (!name || !funcPtr) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_natives[name] = { name, sig ? sig : "", funcPtr };
}

const NativeInfo* NativeRegistry::Find(const char* name) const {
    if (!name) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_natives.find(name);
    if (it != m_natives.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t NativeRegistry::GetCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_natives.size();
}

} // namespace War3Dev
