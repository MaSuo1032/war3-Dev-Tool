#pragma once
#include <string>
#include <cstdlib>
#include <climits>

// The pipe protocol is line based; key/value storage in SaveData is bounded.
inline bool ValidateSaveCommand(const std::string& player, const std::string& key,
                                const std::string& value, bool writing) {
    if (player.empty() || player.size() > 10 || key.empty() || key.size() >= 128 ||
        (writing && (value.empty() || value.size() >= 256))) return false;
    if (key.find_first_of(":\r\n") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos) return false;
    for (char c : player) if (c < '0' || c > '9') return false;
    char* end = nullptr;
    long parsed = std::strtol(player.c_str(), &end, 10);
    return *end == '\0' && parsed > 0 && parsed <= INT_MAX;
}
