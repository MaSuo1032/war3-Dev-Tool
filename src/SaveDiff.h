#pragma once
#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

namespace SaveDiff {
using Key = std::pair<std::string, std::string>;
using Snapshot = std::map<Key, std::string>;
inline void Write(std::ostream& out, const Snapshot& snapshot) {
    out << "WAR3_SAVE_SNAPSHOT_V1\n";
    for (const auto& row : snapshot)
        out << std::quoted(row.first.first) << ' ' << std::quoted(row.first.second) << ' ' << std::quoted(row.second) << '\n';
}
inline bool Read(std::istream& in, Snapshot& snapshot) {
    Snapshot parsed;
    std::string line;
    if (!std::getline(in, line) || line != "WAR3_SAVE_SNAPSHOT_V1") return false;
    while (in >> std::ws && in.peek() != EOF) {
        std::string player, key, value;
        if (!(in >> std::quoted(player) >> std::quoted(key) >> std::quoted(value))) return false;
        if (player.empty() || key.empty() || !parsed.emplace(Key{player,key},value).second) return false;
    }
    if (in.bad()) return false;
    snapshot.swap(parsed); return true;
}
inline std::string Escape(const std::string& text) {
    std::string result;
    for (char c : text) {
        switch(c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        default: result += c;
        }
    }
    return result;
}
inline std::string Report(const Snapshot& before, const Snapshot& after) {
    size_t added=0, changed=0, missing=0, same=0;
    std::ostringstream rows;
    auto emit = [&](const char* state, const Key& key, const std::string& oldValue, const std::string& newValue) {
        rows << "<tr><td>" << state << "</td><td>" << Escape(key.first) << "</td><td>" << Escape(key.second)
             << "</td><td>" << Escape(oldValue) << "</td><td>" << Escape(newValue) << "</td></tr>";
    };
    for (const auto& row : before) {
        auto found = after.find(row.first);
        if (found == after.end()) { ++missing; emit("本次未觀測",row.first,row.second,""); }
        else if (row.second != found->second) { ++changed; emit("變更",row.first,row.second,found->second); }
        else ++same;
    }
    for (const auto& row : after) if (before.find(row.first)==before.end()) {
        ++added; emit("新增觀測",row.first,"",row.second);
    }
    std::ostringstream html;
    html << "<!doctype html><html lang='zh-Hant'><meta charset='utf-8'><title>存檔差異比較</title>"
         << "<style>body{font:16px sans-serif;margin:32px;background:#f5f7fa}table{border-collapse:collapse;width:100%;background:white}td,th{border:1px solid #ccd;padding:10px;text-align:left;white-space:pre-wrap;overflow-wrap:anywhere}th{background:#e4ebf5}</style>"
         << "<h1>存檔差異比較</h1><p>新增觀測 " << added << " · 變更 " << changed << " · 本次未觀測 " << missing << " · 未變更 " << same
         << "</p><p>依玩家 ID 與 Key 比較監控列表的最後觀測值。未觀測不代表伺服器已刪除；歷史資料可能未刷新。請在同一地圖、相同玩家下比較，並等待讀取結果到齊。手動送出但未確認的寫入也可能出現在列表。</p>"
         << "<table><tr><th>狀態</th><th>玩家 ID</th><th>Key</th><th>之前</th><th>現在</th></tr>" << rows.str() << "</table></html>";
    return html.str();
}
}
