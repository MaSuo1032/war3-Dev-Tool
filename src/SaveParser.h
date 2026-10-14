#pragma once
#include <string>
#include <vector>
#include <sstream>

// Utility trimmer
inline void TrimSaveString(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
}

// Universal parser for all known War3 RPG Save formats:
// 1. [SaveServerValue] Player: 1048602 | Key: "ITYMSTGCS" | Value: "10"
// 2. [GetServerValue] Player: 1048602 | Key: "ITYMSTGCS" | Value(From Server): "10"
// 3. [16:20:10] [SaveServerValue] Player: 1048602 | Key: "ITYMSTGCS" | Value: "10"
// 4. Player: 1048602 | Key: ITYMSTGCS | Value: 10
// 5. Player 1048602 SAVED: ITYMSTGCS = 10 (or LOADED: / SET_STAT:)
// 6. 1048602:ITYMSTGCS=10
// 7. 1048602:ITYMSTGCS:10
// 8. ITYMSTGCS=10 (auto uses defPid)
// 9. !save 1048602 ITYMSTGCS 10 or !save ITYMSTGCS 10
// 10. SAVE:1048602:ITYMSTGCS:10
// 11. 1048602 ITYMSTGCS 10
inline bool ParseSaveLine(const std::string& rawLine, const std::string& defPid, std::string& outPid, std::string& outKey, std::string& outVal) {
    std::string line = rawLine;
    TrimSaveString(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') return false;

    // 1. Strip !save or !SAVE prefix
    if (line.rfind("!save ", 0) == 0 || line.rfind("!SAVE ", 0) == 0) {
        line = line.substr(6);
        TrimSaveString(line);
    }

    // 2. Strip SAVE: or save: prefix
    if (line.rfind("SAVE:", 0) == 0 || line.rfind("save:", 0) == 0) {
        line = line.substr(5);
        TrimSaveString(line);
    }

    // 3. Match DzAPI log format: [SaveServerValue] or [GetServerValue] or any string containing "Player" and "Key"
    // e.g.: [SaveServerValue] Player: 1048602 | Key: "ITYMSTGCS" | Value: "10"
    // e.g.: [GetServerValue] Player: 1048602 | Key: "ITYMSTGCS" | Value(From Server): "10"
    if (line.find("Player") != std::string::npos && line.find("Key") != std::string::npos) {
        // Extract Player
        size_t pPos = line.find("Player");
        size_t colonP = line.find(':', pPos);
        size_t searchDigitsFrom = (colonP != std::string::npos && colonP - pPos < 10) ? colonP : pPos + 6;
        size_t pNumStart = line.find_first_of("0123456789", searchDigitsFrom);
        if (pNumStart != std::string::npos) {
            size_t pNumEnd = line.find_first_not_of("0123456789", pNumStart);
            outPid = line.substr(pNumStart, (pNumEnd == std::string::npos ? line.length() : pNumEnd) - pNumStart);
        } else {
            outPid = defPid;
        }

        // Extract Key
        size_t kPos = line.find("Key");
        size_t q1 = line.find('"', kPos);
        size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
        if (q1 != std::string::npos && q2 != std::string::npos) {
            outKey = line.substr(q1 + 1, q2 - q1 - 1);
        } else {
            size_t kColon = line.find(':', kPos);
            if (kColon != std::string::npos) {
                size_t kPipe = line.find('|', kColon);
                if (kPipe != std::string::npos) {
                    outKey = line.substr(kColon + 1, kPipe - kColon - 1);
                }
            }
        }
        TrimSaveString(outKey);

        // Extract Value (MUST search after the key to avoid matching 'Value' inside '[GetServerValue]' or '[SaveServerValue]')
        size_t searchValFrom = (q2 != std::string::npos) ? (q2 + 1) : (kPos + 3);
        size_t vPos = line.find("Value", searchValFrom);
        if (vPos != std::string::npos) {
            size_t q3 = line.find('"', vPos);
            size_t q4 = (q3 != std::string::npos) ? line.find('"', q3 + 1) : std::string::npos;
            if (q3 != std::string::npos && q4 != std::string::npos) {
                outVal = line.substr(q3 + 1, q4 - q3 - 1);
            } else {
                size_t vColon = line.find(':', vPos);
                if (vColon != std::string::npos) {
                    outVal = line.substr(vColon + 1);
                }
            }
        }
        TrimSaveString(outVal);

        if (!outKey.empty() && !outVal.empty()) {
            if (outPid.empty()) outPid = defPid;
            return true;
        }
    }

    // 4. Match local_save.txt format: Player 1048602 SAVED: Key = Val or LOADED: Key = Val
    if (line.find("Player ") == 0 && (line.find(" SAVED: ") != std::string::npos || line.find(" LOADED: ") != std::string::npos || line.find(" SET_STAT: ") != std::string::npos)) {
        size_t sp1 = 7;
        size_t sp2 = line.find(' ', sp1);
        if (sp2 != std::string::npos) {
            outPid = line.substr(sp1, sp2 - sp1);
            size_t colon = line.find(": ", sp2);
            size_t eq = line.find(" = ", colon);
            if (colon != std::string::npos && eq != std::string::npos) {
                outKey = line.substr(colon + 2, eq - (colon + 2));
                outVal = line.substr(eq + 3);
                TrimSaveString(outPid);
                TrimSaveString(outKey);
                TrimSaveString(outVal);
                return true;
            }
        }
    }

    // 5. Standard Player:Key=Value (or Player:Key:Value)
    size_t colon = line.find(':');
    size_t eq = line.find('=');
    if (colon != std::string::npos && eq != std::string::npos && colon < eq) {
        outPid = line.substr(0, colon);
        outKey = line.substr(colon + 1, eq - colon - 1);
        outVal = line.substr(eq + 1);
        TrimSaveString(outPid);
        TrimSaveString(outKey);
        TrimSaveString(outVal);
        if (!outKey.empty() && !outVal.empty() && 
            outPid.find('[') == std::string::npos && outPid.find('|') == std::string::npos && 
            outKey.find('|') == std::string::npos) {
            if (outPid.empty()) outPid = defPid;
            return true;
        }
    }

    // 5b. Colon only: Player:Key:Val
    if (colon != std::string::npos) {
        size_t colon2 = line.find(':', colon + 1);
        if (colon2 != std::string::npos) {
            outPid = line.substr(0, colon);
            outKey = line.substr(colon + 1, colon2 - colon - 1);
            outVal = line.substr(colon2 + 1);
            TrimSaveString(outPid);
            TrimSaveString(outKey);
            TrimSaveString(outVal);
            if (!outKey.empty() && !outVal.empty() && 
                outPid.find('[') == std::string::npos && outPid.find('|') == std::string::npos && 
                outKey.find('|') == std::string::npos) {
                if (outPid.empty()) outPid = defPid;
                return true;
            }
        }
    }

    // 6. Key=Value (without Player ID)
    if (eq != std::string::npos) {
        outPid = defPid;
        outKey = line.substr(0, eq);
        outVal = line.substr(eq + 1);
        TrimSaveString(outKey);
        TrimSaveString(outVal);
        if (!outKey.empty() && !outVal.empty()) {
            return true;
        }
    }

    // 7. Space-separated tokens: Player Key Val (3 tokens) or Key Val (2 tokens)
    std::stringstream tss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (tss >> tok) tokens.push_back(tok);

    if (tokens.size() >= 3) {
        outPid = tokens[0];
        outKey = tokens[1];
        outVal = tokens[2];
        for (size_t i = 3; i < tokens.size(); i++) {
            outVal += " " + tokens[i];
        }
        return true;
    } else if (tokens.size() == 2) {
        outPid = defPid;
        outKey = tokens[0];
        outVal = tokens[1];
        return true;
    }

    return false;
}
