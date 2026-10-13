#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace War3Dev {

struct NativeInfo {
    char name[64];
    char signature[64];
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

    size_t GetCount() const { return m_nativeCount; }

private:
    NativeRegistry() = default;

    static void __fastcall HookedRegisterNative(void* func_ptr, const char* name, const char* signature);

    static constexpr size_t MAX_NATIVES = 2048;
    NativeInfo m_natives[MAX_NATIVES];
    size_t m_nativeCount = 0;

    RegisterNative_t m_origRegisterNative = nullptr;
    BYTE m_origBytes[5] = { 0 };
    void* m_hookTarget = nullptr;
};

} // namespace War3Dev
