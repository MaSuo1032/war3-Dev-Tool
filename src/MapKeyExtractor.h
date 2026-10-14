#pragma once
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>

// Extract all DzAPI Save keys from JASS script (war3map.j or map script text)
inline std::vector<std::string> ExtractKeysFromJassText(const std::string& text) {
    std::set<std::string> uniqueKeys;
    
    struct Pattern {
        std::string token;
        std::string prefix;
    };

    Pattern patterns[] = {
        {"DzAPI_Map_GetStoredInteger", "I"},
        {"DzAPI_Map_GetStoredString", "S"},
        {"DzAPI_Map_GetStoredReal", "R"},
        {"DzAPI_Map_GetStoredBoolean", "B"},
        {"DzAPI_Map_GetServerValue", ""},
        {"DzAPI_Map_SaveServerValue", ""},
        {"DzAPI_Map_Stat_SetStat", ""},
        {"DzAPI_Map_Ladder_SetStat", ""}
    };

    size_t pos = 0;
    while (pos < text.length()) {
        size_t nextNl = text.find('\n', pos);
        if (nextNl == std::string::npos) nextNl = text.length();
        std::string line = text.substr(pos, nextNl - pos);
        pos = nextNl + 1;

        size_t firstChar = line.find_first_not_of(" \t\r");
        if (firstChar == std::string::npos || line[firstChar] == '/' || 
            line.compare(firstChar, 8, "function") == 0 || 
            line.compare(firstChar, 6, "native") == 0) {
            continue;
        }

        for (const auto& p : patterns) {
            size_t tokenPos = line.find(p.token);
            if (tokenPos != std::string::npos) {
                size_t q1 = line.find('"', tokenPos + p.token.length());
                if (q1 != std::string::npos) {
                    size_t q2 = line.find('"', q1 + 1);
                    if (q2 != std::string::npos && q2 > q1 + 1) {
                        std::string k = line.substr(q1 + 1, q2 - q1 - 1);
                        if (k.length() >= 2 && k.length() <= 64 && k.find('\n') == std::string::npos) {
                            uniqueKeys.insert(p.prefix + k);
                        }
                    }
                }
            }
        }
    }

    return std::vector<std::string>(uniqueKeys.begin(), uniqueKeys.end());
}

inline bool ExtractKeysFromFile(const std::wstring& filePath, std::vector<std::string>& outKeys) {
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string text = ss.str();
    file.close();
    outKeys = ExtractKeysFromJassText(text);
    return !outKeys.empty();
}

inline bool LoadMapKeysFile(const std::string& path, std::vector<std::string>& outKeys) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        outKeys.push_back(line);
    }
    file.close();
    return !outKeys.empty();
}
