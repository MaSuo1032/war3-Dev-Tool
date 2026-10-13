#include "NativeInvoker.hpp"
#include "Common/Logger.hpp"
#include <cstdlib>
#include <cstring>

namespace War3Dev {

void NativeInvoker::Execute(const InvokePayload* payload) {
    if (!payload || !payload->funcPtr) return;

    void* func = payload->funcPtr;
    const char* sig = payload->sig;
    const char* a1 = payload->arg1[0] ? payload->arg1 : nullptr;
    const char* a2 = payload->arg2[0] ? payload->arg2 : nullptr;
    const char* a3 = payload->arg3[0] ? payload->arg3 : nullptr;

    __try {
        if (strstr(sig, "(R") != nullptr) {
            float val = a1 ? static_cast<float>(atof(a1)) : 0.0f;
            typedef void(__cdecl *Func1F)(float);
            reinterpret_cast<Func1F>(func)(val);
            LOG("[Invoker] Executed successfully (Float Arg: %f)\n", val);
        }
        else if (a1 && a2 && a3) {
            int v1 = atoi(a1);
            int v2 = atoi(a2);
            int v3 = atoi(a3);
            typedef void(__cdecl *Func3I)(int, int, int);
            reinterpret_cast<Func3I>(func)(v1, v2, v3);
            LOG("[Invoker] Executed successfully (Args: %d, %d, %d)\n", v1, v2, v3);
        }
        else if (a1 && a2) {
            int v1 = atoi(a1);
            int v2 = atoi(a2);
            typedef void(__cdecl *Func2I)(int, int);
            reinterpret_cast<Func2I>(func)(v1, v2);
            LOG("[Invoker] Executed successfully (Args: %d, %d)\n", v1, v2);
        }
        else if (a1) {
            int v1 = atoi(a1);
            typedef void(__cdecl *Func1I)(int);
            reinterpret_cast<Func1I>(func)(v1);
            LOG("[Invoker] Executed successfully (Arg: %d)\n", v1);
        }
        else {
            typedef void(__cdecl *Func0)();
            reinterpret_cast<Func0>(func)();
            LOG("[Invoker] Executed successfully (No args)\n");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LOG("[Invoker] Exception occurred while executing function 0x%p!\n", func);
    }
}

} // namespace War3Dev
