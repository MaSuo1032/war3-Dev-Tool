#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace War3Dev {

#pragma pack(push, 1)
struct JassStringData {
    uint32_t refCount;
    uint32_t pad[6];
    char* chars;
};

struct JassString {
    uint32_t vtable;
    uint32_t handleId;
    JassStringData* data;
};
#pragma pack(pop)

class JassTypes {
public:
    static const char* ReadJassString(uintptr_t jassStrPtr);
    static std::string DecodeServerString(uintptr_t resultPtr);

    static uintptr_t CreateFakeJassString(const char* str);
    static void FreeFakeJassString(uintptr_t jassStrPtr);
};

} // namespace War3Dev
