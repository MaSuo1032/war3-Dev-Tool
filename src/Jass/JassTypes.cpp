#include "JassTypes.hpp"
#include <cstring>
#include <cstdio>

namespace War3Dev {

const char* JassTypes::ReadJassString(uintptr_t jassStrPtr) {
    __try {
        if (jassStrPtr > 0x10000 && jassStrPtr < 0x7FFFFFFF) {
            uintptr_t stringData = *reinterpret_cast<uintptr_t*>(jassStrPtr + 8);
            if (stringData > 0x10000 && stringData < 0x7FFFFFFF) {
                char* chars = *reinterpret_cast<char**>(stringData + 0x1C);
                if (chars && reinterpret_cast<uintptr_t>(chars) > 0x10000 && reinterpret_cast<uintptr_t>(chars) < 0x7FFFFFFF) {
                    return chars;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return "(null)";
}

std::string JassTypes::DecodeServerString(uintptr_t resultPtr) {
    const char* chars = ReadJassString(resultPtr);
    if (chars && strcmp(chars, "(null)") != 0 && chars[0] != '\0') {
        return std::string(chars);
    }

    if (resultPtr > 0 && resultPtr < 0x10000) {
        char buf[64];
        sprintf_s(buf, sizeof(buf), "<StringID: %u>", static_cast<unsigned int>(resultPtr));
        return std::string(buf);
    }

    return "(null/empty)";
}

uintptr_t JassTypes::CreateFakeJassString(const char* str) {
    if (!str) return 0;

    size_t len = strlen(str);
    char* strCopy = new char[len + 1];
    strcpy_s(strCopy, len + 1, str);

    JassStringData* data = new JassStringData();
    memset(data, 0, sizeof(JassStringData));
    data->refCount = 1;
    data->chars = strCopy;

    JassString* jStr = new JassString();
    memset(jStr, 0, sizeof(JassString));
    jStr->data = data;

    return reinterpret_cast<uintptr_t>(jStr);
}

void JassTypes::FreeFakeJassString(uintptr_t jassStrPtr) {
    if (!jassStrPtr) return;

    JassString* jStr = reinterpret_cast<JassString*>(jassStrPtr);
    if (jStr->data) {
        if (jStr->data->chars) {
            delete[] jStr->data->chars;
        }
        delete jStr->data;
    }
    delete jStr;
}

} // namespace War3Dev
