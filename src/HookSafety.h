#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

// Deliberately conservative x86 entry decoder. Unknown instructions are refused,
// never guessed; supported entries are copied through complete instructions.
inline size_t X86EntryInstructionLength(const BYTE* code, bool& relativeCall) {
    relativeCall = false;
    BYTE op = code[0];
    if ((op >= 0x50 && op <= 0x5f) || op == 0x90) return 1;
    if (op == 0x68 || (op >= 0xb8 && op <= 0xbf) || op == 0xa1 || op == 0xa3) return 5;
    if (op == 0x6a) return 2;
    if (op == 0xe8 || op == 0xe9) { relativeCall = true; return 5; }
    if (op != 0x8b && op != 0x89 && op != 0x8d && op != 0x83 && op != 0x81 && op != 0x33 && op != 0x85 && op != 0xff) return 0;
    BYTE modrm = code[1];
    unsigned mod = modrm >> 6, rm = modrm & 7;
    // Indirect jumps/returns before the patch boundary are not safe gateways.
    if (op == 0xff && ((modrm >> 3) & 7) >= 4) return 0;
    size_t length = 2;
    if (mod != 3 && rm == 4) {
        BYTE sib = code[length++];
        if (mod == 0 && (sib & 7) == 5) length += 4;
    }
    if (mod == 0 && rm == 5) length += 4;
    if (mod == 1) ++length;
    if (mod == 2) length += 4;
    if (op == 0x83) ++length;
    if (op == 0x81) length += 4;
    return length;
}

inline size_t X86PatchSpan(const BYTE* code) {
    size_t span = 0;
    while (span < 5) {
        bool relative = false;
        size_t length = X86EntryInstructionLength(code + span, relative);
        if (!length || span + length > 16) return 0;
        span += length;
    }
    return span;
}

inline void WriteRelativeJump(BYTE* destination, const void* target) {
    destination[0] = 0xe9;
    intptr_t delta = reinterpret_cast<intptr_t>(target) - reinterpret_cast<intptr_t>(destination + 5);
    int32_t relative = static_cast<int32_t>(delta);
    memcpy(destination + 1, &relative, sizeof(relative));
}

// Layout matches GenericDzTraceHandler: saved flags at [0], return address at
// [9], native arguments at [10..]. Save the pre-alignment ESP separately and
// restore it with pop esp before restoring flags and registers.
inline size_t EmitTraceThunk(BYTE* code, unsigned index, const void* handler, const void* original) {
    const BYTE prefix[] = {0x60, 0x9c, 0x89, 0xe0, 0x83, 0xe4, 0xf0,
        0x83, 0xec, 0x04, 0x50, 0x50, 0x68};
    memcpy(code, prefix, sizeof(prefix));
    memcpy(code + 13, &index, 4);
    code[17] = 0xe8;
    int32_t call = static_cast<int32_t>(reinterpret_cast<intptr_t>(handler) - reinterpret_cast<intptr_t>(code + 22));
    memcpy(code + 18, &call, 4);
    const BYTE suffix[] = {0x83, 0xc4, 0x08, 0x5c, 0x9d, 0x61};
    memcpy(code + 22, suffix, sizeof(suffix));
    WriteRelativeJump(code + 28, original);
    return 33;
}
