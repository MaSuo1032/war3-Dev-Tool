#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>

// Control Identifiers
#define IDC_GRP_INJECT            1000
#define IDC_LBL_WAR3_STATUS       1001
#define IDC_LBL_HOOK_STATUS       1002
#define IDC_LBL_PIPE_STATUS       1003
#define IDC_CHK_AUTO_INJECT       1004
#define IDC_BTN_INJECT            1005
#define IDC_BTN_LAUNCH_CLI        1006
#define IDC_BTN_RECONNECT         1007

#define IDC_GRP_MALL              1010
#define IDC_CMB_MALL_MODE         1011
#define IDC_SLIDER_PERCENT        1012
#define IDC_EDIT_PERCENT          1013
#define IDC_BTN_APPLY_MALL        1014
#define IDC_BTN_EDIT_BLACKLIST    1015
#define IDC_BTN_EDIT_WHITELIST    1016
#define IDC_BTN_RELOAD_LISTS      1017
#define IDC_BTN_MALL_MODE0        1018
#define IDC_BTN_MALL_MODE1        1019
#define IDC_BTN_MALL_MODE2        1008

#define IDC_GRP_SAVE              1020
#define IDC_BTN_PANEL             1021
#define IDC_BTN_SAVEALL           1022
#define IDC_BTN_OPEN_LOCALSAVE    1023
#define IDC_EDIT_PID              1024
#define IDC_EDIT_KEY              1025
#define IDC_EDIT_VAL              1026
#define IDC_BTN_SAVE_SINGLE       1027
#define IDC_EDIT_QUICKSAVE        1028
#define IDC_BTN_QUICKSAVE         1029
#define IDC_BTN_PASTE_SAVE        1034

#define IDC_GRP_CMD               1030
#define IDC_BTN_KILL              1031
#define IDC_EDIT_CMD              1032
#define IDC_BTN_SEND_CMD          1033

#define IDC_GRP_LOG               1040
#define IDC_EDIT_LOG              1041
#define IDC_CHK_AUTO_SCROLL       1042
#define IDC_BTN_CLEAR_LOG         1043
#define IDC_BTN_COPY_LOG          1044
#define IDC_BTN_OPEN_TRACE        1045

#define IDC_TAB_MAIN              1050
#define IDC_TAB_MONITOR           1050
#define IDC_LIST_SAVE_MONITOR     1051
#define IDC_BTN_COPY_SAVE_ITEM    1052
#define IDC_BTN_FILL_SAVE_INPUT   1053
#define IDC_BTN_CLEAR_SAVE_LIST   1054
#define IDC_BTN_RELOAD_SAVE       1055
#define IDC_BTN_BATCH_READ        1060
#define IDC_BTN_EDIT_MAP_KEYS     1061
#define IDC_BTN_EXTRACT_KEYS      1062

// Web Controller & Direct Memory Modification Controls
#define IDC_BTN_MEM_HEAL          1070
#define IDC_BTN_MEM_GODMODE       1071
#define IDC_BTN_MEM_SPEED         1072
#define IDC_BTN_MEM_STATS         1073
#define IDC_BTN_MEM_GOLD          1074
#define IDC_BTN_MEM_LUMBER        1075
#define IDC_BTN_OPEN_WEB_UI       1076

#define IDC_BTN_MEM_SCAN          1080
#define IDC_BTN_MEM_SCAN_RES      1081
#define IDC_EDIT_UNIT_HP          1082
#define IDC_EDIT_UNIT_MAXHP       1083
#define IDC_BTN_SET_HP            1084
#define IDC_EDIT_UNIT_MP          1085
#define IDC_EDIT_UNIT_MAXMP       1086
#define IDC_BTN_SET_MP            1087
#define IDC_EDIT_UNIT_STR         1088
#define IDC_BTN_SET_STR           1089
#define IDC_EDIT_UNIT_AGI         1090
#define IDC_BTN_SET_AGI           1091
#define IDC_EDIT_UNIT_INT         1092
#define IDC_BTN_SET_INT           1093
#define IDC_EDIT_UNIT_SPEED       1094
#define IDC_BTN_SET_SPEED         1095
#define IDC_EDIT_UNIT_DEF         1096
#define IDC_BTN_SET_DEF           1097
#define IDC_EDIT_UNIT_EXP         1098
#define IDC_BTN_SET_EXP           1099

#define IDC_CMB_RES_PLAYER        1100
#define IDC_EDIT_RES_GOLD         1101
#define IDC_BTN_SET_RES_GOLD      1102
#define IDC_BTN_ADD_RES_GOLD      1103
#define IDC_EDIT_RES_LUMBER       1104
#define IDC_BTN_SET_RES_LUMBER    1105
#define IDC_BTN_ADD_RES_LUMBER    1106

#define IDC_EDIT_ITEM_SLOT0       1110
#define IDC_EDIT_ITEM_SLOT1       1111
#define IDC_EDIT_ITEM_SLOT2       1112
#define IDC_EDIT_ITEM_SLOT3       1113
#define IDC_EDIT_ITEM_SLOT4       1114
#define IDC_EDIT_ITEM_SLOT5       1115
#define IDC_BTN_SET_ITEM_SLOT0    1116
#define IDC_BTN_SET_ITEM_SLOT1    1117
#define IDC_BTN_SET_ITEM_SLOT2    1118
#define IDC_BTN_SET_ITEM_SLOT3    1119
#define IDC_BTN_SET_ITEM_SLOT4    1120
#define IDC_BTN_SET_ITEM_SLOT5    1121
#define IDC_BTN_ADD_UNIT_STR      1122
#define IDC_BTN_ADD_UNIT_AGI      1123
#define IDC_BTN_ADD_UNIT_INT      1124
#define IDC_BTN_ADD_UNIT_EXP      1125
#define IDC_BTN_MEM_BLINK         1126
#define IDC_BTN_SCAN_ITEM_IDS     1127
#define IDC_BTN_COMPAT_CHECK      1128

#define IDT_TIMER_STATUS          1
#define IDT_TIMER_AUTO_INJECT     2
#define WM_USER_UPDATE_SAVE_LIST  (WM_USER + 101)

// Common Control Unicode helper wrappers (avoids ANSI macro mismatch in non-UNICODE builds)
inline void ListView_GetItemTextW_Helper(HWND hwnd, int iItem, int iSubItem, LPWSTR pszText, int cchTextMax) {
    LVITEMW lvi{};
    lvi.iSubItem = iSubItem;
    lvi.cchTextMax = cchTextMax;
    lvi.pszText = pszText;
    SendMessageW(hwnd, LVM_GETITEMTEXTW, (WPARAM)iItem, (LPARAM)&lvi);
}

inline void ListView_SetItemTextW_Helper(HWND hwnd, int iItem, int iSubItem, LPCWSTR pszText) {
    LVITEMW lvi{};
    lvi.iSubItem = iSubItem;
    lvi.pszText = (LPWSTR)pszText;
    SendMessageW(hwnd, LVM_SETITEMTEXTW, (WPARAM)iItem, (LPARAM)&lvi);
}

inline int ListView_InsertItemW_Helper(HWND hwnd, const LVITEMW* pitem) {
    return (int)SendMessageW(hwnd, LVM_INSERTITEMW, 0, (LPARAM)pitem);
}

inline int ListView_InsertColumnW_Helper(HWND hwnd, int iCol, const LVCOLUMNW* pcol) {
    return (int)SendMessageW(hwnd, LVM_INSERTCOLUMNW, (WPARAM)iCol, (LPARAM)pcol);
}

inline int TabCtrl_InsertItemW_Helper(HWND hwnd, int iItem, const TCITEMW* pitem) {
    return (int)SendMessageW(hwnd, TCM_INSERTITEMW, (WPARAM)iItem, (LPARAM)pitem);
}

inline BOOL TabCtrl_SetItemW_Helper(HWND hwnd, int iItem, const TCITEMW* pitem) {
    return (BOOL)SendMessageW(hwnd, TCM_SETITEMW, (WPARAM)iItem, (LPARAM)pitem);
}

// Data structures for Save Monitor
struct SaveRecord {
    std::wstring time;
    std::wstring action;
    std::wstring player;
    std::wstring key;
    std::wstring val;
};

struct SaveItemKey {
    std::wstring player;
    std::wstring key;
    bool operator<(const SaveItemKey& o) const {
        if (player != o.player) return player < o.player;
        return key < o.key;
    }
};

// String Encoding Conversion Helpers
inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size, NULL, NULL);
    return str;
}

inline std::string GetExeDir() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s = path;
    size_t last = s.find_last_of("\\/");
    if (last != std::string::npos) return s.substr(0, last + 1);
    return ".\\";
}

inline std::wstring GetExeDirWide() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return L".\\";
    std::wstring result(path, length);
    size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L".\\" : result.substr(0, slash + 1);
}
