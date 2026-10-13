#include "Memory.hpp"

namespace War3Dev {

static bool SafeCopyString(const char* src, char* dst, size_t maxLen) {
    __try {
        for (size_t i = 0; i < maxLen; ++i) {
            dst[i] = src[i];
            if (src[i] == '\0') {
                return true;
            }
        }
        dst[maxLen - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Memory::Write(void* address, const void* data, size_t size) {
    if (!address || !data || size == 0) return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    bool success = false;
    __try {
        memcpy(address, data, size);
        FlushInstructionCache(GetCurrentProcess(), address, size);
        success = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
    }

    DWORD temp = 0;
    VirtualProtect(address, size, oldProtect, &temp);
    return success;
}

bool Memory::IsValidReadPtr(const void* ptr, size_t size) {
    if (!ptr) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr < 0x10000 || addr >= 0x7FFFFFFF) return false;

    __try {
        volatile const char* p = reinterpret_cast<const char*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            char dummy = p[i];
            (void)dummy;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string Memory::ReadString(uintptr_t address, size_t maxLen) {
    if (!IsValidReadPtr(reinterpret_cast<const void*>(address), 1)) {
        return "";
    }

    char buf[1024] = { 0 };
    size_t limit = (maxLen < sizeof(buf)) ? maxLen : sizeof(buf);
    if (SafeCopyString(reinterpret_cast<const char*>(address), buf, limit)) {
        return std::string(buf);
    }
    return "";
}

} // namespace War3Dev
