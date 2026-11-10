#include "SaveDiff.h"
#include "SaveCommand.h"
#include <iostream>
int main() {
    if (!ValidateSaveCommand("1048602", "ITEM_KEY", "999", true) ||
        ValidateSaveCommand("0", "ITEM_KEY", "999", true) ||
        ValidateSaveCommand("1048602", std::string(128, 'K'), "999", true) ||
        ValidateSaveCommand("1048602", "ITEM_KEY", std::string(256, 'V'), true) ||
        ValidateSaveCommand("1048602", "A:B", "999", true) ||
        ValidateSaveCommand("1048602", "ITEM_KEY", "x\ny", true) ||
        !ValidateSaveCommand("1048602", "ITEM_KEY", "", false)) return 6;
    SaveDiff::Snapshot a{{{"1","gold"},"10"},{{"2","gold"},"20"},{{"1","old"},"x"},{{"1","text"},"<script>\n\"quoted\""}};
    std::stringstream encoded; SaveDiff::Write(encoded,a);
    SaveDiff::Snapshot restored;
    if (!SaveDiff::Read(encoded,restored) || restored!=a) return 1;
    auto b=a; b[{"1","gold"}]="15"; b.erase({"1","old"}); b[{"1","new"}]="";
    auto report=SaveDiff::Report(a,b);
    if(report.find("&lt;script&gt;")!=std::string::npos) return 2; // unchanged rows omitted
    b[{"1","text"}]="new"; report=SaveDiff::Report(a,b);
    if(report.find("&lt;script&gt;")==std::string::npos || report.find("<script>")!=std::string::npos) return 3;
    if(report.find("新增觀測 1")==std::string::npos || report.find("變更 2")==std::string::npos || report.find("本次未觀測 1")==std::string::npos) return 4;
    std::stringstream bad("WAR3_SAVE_SNAPSHOT_V1\n\"1\" \"key\" \"unterminated");
    if(SaveDiff::Read(bad,restored)) return 5;
    std::cout << "Snapshot roundtrip, multiline/quotes, player isolation, changes, missing observations and HTML escaping passed\n";
}
