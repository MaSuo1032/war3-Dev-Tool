#pragma once
#include <windows.h>
#include <string>
#include <unordered_map>

namespace War3Dev {

class PatchHook {
public:
    PatchHook() = default;

    bool Install(void* target, void* hook);
    void Unhook();
    void Rehook();

    void* GetTarget() const { return m_target; }
    bool IsInstalled() const { return m_installed; }

private:
    void* m_target = nullptr;
    void* m_hook = nullptr;
    BYTE m_originalBytes[5] = { 0 };
    bool m_installed = false;
};

class HookEngine {
public:
    static HookEngine& Get();

    // Direct 5-byte JMP overwrite for permanent spoofing
    bool InstallSpoof(const std::string& name, void* targetFunc, void* spoofFunc);

    // Safe Unhook/Rehook monitor
    PatchHook* CreatePatchHook(const std::string& name, void* targetFunc, void* hookFunc);

    PatchHook* GetHook(const std::string& name);

private:
    HookEngine() = default;

    std::unordered_map<std::string, PatchHook> m_patchHooks;
};

} // namespace War3Dev
