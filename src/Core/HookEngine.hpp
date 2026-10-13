#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace War3Dev {

struct HookRecord {
    std::string name;
    void* target;
    void* hook;
    void* trampoline;
    size_t stolenBytes;
};

class HookEngine {
public:
    static HookEngine& Get();

    // Creates a permanent trampoline and redirects target to hookFunc
    void* InstallTrampoline(const std::string& name, void* targetFunc, void* hookFunc, size_t stolenBytes = 5);

    // Direct 5-byte JMP overwrite without trampoline
    bool InstallDirectJmp(const std::string& name, void* targetFunc, void* hookFunc);

    const HookRecord* FindHook(const std::string& name) const;

private:
    HookEngine() = default;

    std::unordered_map<std::string, HookRecord> m_hooks;
};

} // namespace War3Dev
