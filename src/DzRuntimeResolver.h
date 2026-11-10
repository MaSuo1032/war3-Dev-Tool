#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "dz_registry.h"

// Decode only registration forms verified in the bundled KK plugin. Every
// result must also match a known native name/signature and an executable
// handler. An unfamiliar layout fails closed and the live gateway still works.
struct RuntimeDzNative {
    char name[64];
    char signature[64];
    void* handler;
};

struct RuntimeDzRegistry {
    std::vector<RuntimeDzNative> natives;
    unsigned direct = 0;
    unsigned table = 0;
    DWORD registrarRva = 0;
};

namespace DzRuntimeDetail {
struct Section {
    const BYTE* data;
    size_t size;
    uintptr_t begin;
};
struct Direct {
    RuntimeDzNative native;
    uintptr_t registrar;
};

inline DWORD Read32(const BYTE* address) {
    DWORD result;
    memcpy(&result, address, sizeof(result));
    return result;
}
inline int32_t ReadSigned32(const BYTE* address) {
    int32_t result;
    memcpy(&result, address, sizeof(result));
    return result;
}
inline bool Executable(uintptr_t address, const std::vector<Section>& sections) {
    for (const auto& section : sections)
        if (address >= section.begin && address - section.begin < section.size) return true;
    return false;
}
inline const char* ImageString(uintptr_t address, uintptr_t base, size_t imageSize) {
    if (address < base || address - base >= imageSize) return nullptr;
    size_t remaining = imageSize - (address - base);
    const char* text = reinterpret_cast<const char*>(address);
    size_t limit = remaining < 64 ? remaining : 64;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(text, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        address + limit > reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize)
        return nullptr;
    for (size_t i = 0; i < limit; ++i)
        if (text[i] == '\0') return i ? text : nullptr;
    return nullptr;
}
inline bool KnownPair(const char* name, const char* signature) {
    if (!name || !signature) return false;
    for (int i = 0; i < g_dzTableCount; ++i)
        if (strcmp(g_dzTable[i].name, name) == 0 && strcmp(g_dzTable[i].sig, signature) == 0)
            return true;
    return false;
}
inline bool DecodeNative(uintptr_t handler, uintptr_t nameAddress, uintptr_t signatureAddress,
                         uintptr_t base, size_t imageSize, const std::vector<Section>& sections,
                         RuntimeDzNative& output) {
    const char* name = ImageString(nameAddress, base, imageSize);
    const char* signature = ImageString(signatureAddress, base, imageSize);
    if (!KnownPair(name, signature) || !Executable(handler, sections)) return false;
    strcpy_s(output.name, name);
    strcpy_s(output.signature, signature);
    output.handler = reinterpret_cast<void*>(handler);
    return true;
}
inline bool AddNative(RuntimeDzRegistry& registry, const RuntimeDzNative& native) {
    for (const auto& old : registry.natives) {
        if (strcmp(old.name, native.name) != 0) continue;
        return strcmp(old.signature, native.signature) == 0 && old.handler == native.handler;
    }
    registry.natives.push_back(native);
    return true;
}
inline bool DecodeTableStore(const BYTE* code, size_t length, size_t offset,
                             int& baseRegister, int32_t& displacement, DWORD& value, size_t& width) {
    if (offset + 7 <= length && code[offset] == 0xc7 && code[offset + 1] == 0x45) {
        baseRegister = 1;
        displacement = static_cast<int8_t>(code[offset + 2]);
        value = Read32(code + offset + 3);
        width = 7;
        return true;
    }
    if (offset + 10 <= length && code[offset] == 0xc7 && code[offset + 1] == 0x85) {
        baseRegister = 1;
        displacement = ReadSigned32(code + offset + 2);
        value = Read32(code + offset + 6);
        width = 10;
        return true;
    }
    if (offset + 8 <= length && code[offset] == 0xc7 && code[offset + 1] == 0x44 && code[offset + 2] == 0x24) {
        baseRegister = 2;
        displacement = code[offset + 3];
        value = Read32(code + offset + 4);
        width = 8;
        return true;
    }
    return false;
}
} // namespace DzRuntimeDetail

inline bool ResolveDzNativesFromLoadedPlugin(HMODULE plugin, RuntimeDzRegistry& result) {
    using namespace DzRuntimeDetail;
    result = {};
    if (!plugin) return false;
    const BYTE* image = reinterpret_cast<const BYTE*>(plugin);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
        dos->e_lfanew > 0x100000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.SizeOfImage < 0x10000 || nt->OptionalHeader.SizeOfImage > 0x4000000 ||
        nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 32) return false;
    const uintptr_t base = reinterpret_cast<uintptr_t>(plugin);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    std::vector<Section> sections;
    const auto* header = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(header[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        DWORD rva = header[i].VirtualAddress;
        size_t size = header[i].Misc.VirtualSize;
        if (rva >= imageSize || !size || size > imageSize - rva) return false;
        sections.push_back({image + rva, size, base + rva});
    }
    if (sections.empty()) return false;

    std::vector<Direct> direct;
    std::vector<RuntimeDzNative> tables;
    for (const auto& section : sections) {
        const BYTE* code = section.data;
        for (size_t i = 0; i + 20 <= section.size; ++i) {
            // push signature; mov edx, name; mov ecx, handler; call registrar
            if (code[i] == 0x68 && code[i + 5] == 0xba && code[i + 10] == 0xb9 && code[i + 15] == 0xe8) {
                RuntimeDzNative native{};
                uintptr_t name = Read32(code + i + 6);
                uintptr_t signature = Read32(code + i + 1);
                uintptr_t handler = Read32(code + i + 11);
                intptr_t next = static_cast<intptr_t>(section.begin + i + 20);
                uintptr_t registrar = static_cast<uintptr_t>(next + ReadSigned32(code + i + 16));
                if (DecodeNative(handler, name, signature, base, imageSize, sections, native) &&
                    Executable(registrar, sections)) direct.push_back({native, registrar});
            }
            // One known registration initializes local stack data between the
            // name, handler and registrar call.
            if (i + 38 <= section.size && code[i] == 0x68 && code[i + 5] == 0xba &&
                code[i + 10] == 0xc7 && code[i + 11] == 0x45 && code[i + 17] == 0xb9 &&
                code[i + 22] == 0xc7 && code[i + 23] == 0x45 && code[i + 29] == 0xc6 &&
                code[i + 30] == 0x45 && code[i + 33] == 0xe8) {
                RuntimeDzNative native{};
                uintptr_t name = Read32(code + i + 6);
                uintptr_t signature = Read32(code + i + 1);
                uintptr_t handler = Read32(code + i + 18);
                intptr_t next = static_cast<intptr_t>(section.begin + i + 38);
                uintptr_t registrar = static_cast<uintptr_t>(next + ReadSigned32(code + i + 34));
                if (DecodeNative(handler, name, signature, base, imageSize, sections, native) &&
                    Executable(registrar, sections)) direct.push_back({native, registrar});
            }
            // Three consecutive stack writes: handler, name, signature.
            int firstBase = 0, secondBase = 0, thirdBase = 0;
            int32_t firstDisp = 0, secondDisp = 0, thirdDisp = 0;
            DWORD handler = 0, name = 0, signature = 0;
            size_t firstWidth = 0, secondWidth = 0, thirdWidth = 0;
            if (!DecodeTableStore(code, section.size, i, firstBase, firstDisp, handler, firstWidth) ||
                !DecodeTableStore(code, section.size, i + firstWidth, secondBase, secondDisp, name, secondWidth) ||
                !DecodeTableStore(code, section.size, i + firstWidth + secondWidth, thirdBase, thirdDisp, signature, thirdWidth) ||
                firstBase != secondBase || firstBase != thirdBase ||
                secondDisp != firstDisp + 4 || thirdDisp != secondDisp + 4) continue;
            RuntimeDzNative native{};
            if (DecodeNative(handler, name, signature, base, imageSize, sections, native))
                tables.push_back(native);
        }
    }
    if (direct.size() < 300) return false;
    uintptr_t registrar = 0;
    size_t registrarCount = 0;
    for (const auto& candidate : direct) {
        size_t count = 0;
        for (const auto& other : direct) if (candidate.registrar == other.registrar) ++count;
        if (count > registrarCount) { registrarCount = count; registrar = candidate.registrar; }
    }
    if (registrarCount < 300 || registrarCount * 10 < direct.size() * 9) return false;
    result.registrarRva = static_cast<DWORD>(registrar - base);
    for (const auto& candidate : direct) {
        if (candidate.registrar != registrar) continue;
        if (!AddNative(result, candidate.native)) { result = {}; return false; }
        ++result.direct;
    }
    for (const auto& native : tables) {
        size_t before = result.natives.size();
        if (!AddNative(result, native)) { result = {}; return false; }
        if (result.natives.size() != before) ++result.table;
    }
    return result.natives.size() >= 450;
}
