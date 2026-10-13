#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace War3Dev {

class ScopedProtect {
public:
    ScopedProtect(void* address, size_t size, DWORD newProtect = PAGE_EXECUTE_READWRITE)
        : m_address(address), m_size(size), m_oldProtect(0) {
        VirtualProtect(m_address, m_size, newProtect, &m_oldProtect);
    }

    ~ScopedProtect() {
        DWORD temp;
        VirtualProtect(m_address, m_size, m_oldProtect, &temp);
    }

private:
    void* m_address;
    size_t m_size;
    DWORD m_oldProtect;
};

class Memory {
public:
    template <typename T>
    static T Read(uintptr_t address, T defaultValue = T()) {
        __try {
            if (address >= 0x10000 && address <= 0x7FFFFFFF) {
                return *reinterpret_cast<T*>(address);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return defaultValue;
    }

    static bool Write(void* address, const void* data, size_t size);
    static bool IsValidReadPtr(const void* ptr, size_t size = 1);
    static std::string ReadString(uintptr_t address, size_t maxLen = 256);
};

} // namespace War3Dev
