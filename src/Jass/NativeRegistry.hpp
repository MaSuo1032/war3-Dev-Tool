#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace War3Dev {

struct NativeInfo {
    std::string name;
    std::string signature;
    void* funcPtr;
};

typedef void(__fastcall *RegisterNative_t)(void* func_ptr, const char* name, const char* signature);

class NativeRegistry {
public:
    static NativeRegistry& Get();

    void Initialize(HMODULE hGame);
    void PrepopulateFromModules(HMODULE hGame, HMODULE hPlugin);

    void Register(const char* name, const char* sig, void* funcPtr);
    const NativeInfo* Find(const char* name) const;

    size_t GetCount() const;

private:
    NativeRegistry() = default;

    static void __fastcall HookedRegisterNative(void* func_ptr, const char* name, const char* signature);

    std::unordered_map<std::string, NativeInfo> m_natives;
    mutable std::mutex m_mutex;

    RegisterNative_t m_origRegisterNative = nullptr;
    BYTE m_origBytes[5] = { 0 };
    void* m_hookTarget = nullptr;
};

} // namespace War3Dev
