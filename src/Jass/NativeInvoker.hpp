#pragma once
#include <windows.h>
#include <string>

namespace War3Dev {

struct InvokePayload {
    void* funcPtr;
    char sig[64];
    char arg1[64];
    char arg2[64];
    char arg3[64];
};

class NativeInvoker {
public:
    static void Execute(const InvokePayload* payload);
};

} // namespace War3Dev
