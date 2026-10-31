# war3DevTool

Warcraft III 1.27（x86）的本機開發工具。專案提供注入 DLL、Windows 圖形介面、命令列控制台，以及針對存檔、單位與 DzAPI 的研究輔助功能。

> 本工具依賴特定版本的 `Game.dll` 與 DzAPI 插件。部分操作會檢查二進位指紋；版本不符時會拒絕執行。建置與回歸測試無法取代在目標地圖中的實測。

## 專案結構

| 路徑 | 用途 |
| --- | --- |
| `src/JassHook.cpp` | 注入 DLL、Native 註冊與 Hook、命名管道服務、遊戲內快捷鍵 |
| `src/gui.cpp`、`src/Wc3Memory.*` | GUI、存檔快照、單位與資源控制 |
| `src/main.cpp` | 命令列控制台 |
| `src/injector.cpp` | 獨立注入器 |
| `src/*.h` | 共用解析、安全檢查、通訊及 GUI 元件 |
| `config/` | 初次安裝時複製的預設設定；使用者修改位於 `bin/` 的副本 |
| `scripts/` | 建置、啟動、Lua 模組及研究工具 |
| `tests/` | 可在未啟動遊戲時執行的回歸測試 |
| `diagnostics/` | 功能研究、限制與操作紀錄；見[文件索引](diagnostics/README.md) |
| `build/`、`bin/` | 本機建置與執行目錄，Git 不追蹤 |

目前 DLL 與 GUI 的主要實作仍集中在各自的 `.cpp` 檔；上表描述實際檔案配置。

## 建置與測試

需要 Windows、CMake 3.15 以上，以及具備 x86 C++ 工具鏈的 Visual Studio。必須以 **Win32** 架構建置。

```bat
scripts\build.bat --no-pause
```

腳本會設定 CMake、編譯 Release，然後安裝完整執行套件至 `bin/`。重新安裝會保留 `bin/` 已存在的 INI 與名單檔。可單獨執行測試：

```bat
ctest --test-dir build -C Release --output-on-failure
```

`memory_web_regression` 需要允許測試程式連線至本機 localhost。測試涵蓋 Hook、注入器、記憶體 Web 控制與存檔差異；不會啟動魔獸或驗證特定地圖的行為。

## 啟動

1. 啟動 1.27 遊戲並進入地圖。
2. 執行 `scripts\run_gui.bat`，在 GUI 內注入並操作；或執行 `bin\Injector.exe` 注入後，再執行 `scripts\run_client.bat` 開啟命令列控制台。
3. 修改 `bin/` 內的設定與名單；`config/` 是新安裝的預設值。

更新 `JassHook.dll` 後，需重新啟動遊戲並重新注入。遊戲日誌、存檔面板與快照是執行時資料，請留在 `bin/` 或使用者選取的位置。

## 常用操作

| 功能 | 遊戲快捷鍵 | 控制台／GUI 指令框 |
| --- | --- | --- |
| 擊殺選取單位 | `Ctrl+K` | `!kill` |
| 本機分批擊殺全圖敵人 | `Ctrl+Shift+K` | `!killall` |
| 取消尚未執行的全圖擊殺 | `Ctrl+Shift+X` | `!killall stop` |
| 選取單位閃現到滑鼠地面位置 | `Ctrl+B` | GUI 的閃現準備按鈕 |
| 選取英雄三屬性各設為一億 | `Ctrl+H` | `!hero100m` |

控制台另提供 `!panel`、`!saveall`、`!save <玩家 handle> <Key> <Value>`、`!mall <0..3>`、`!percent <1..100>`、`!reload` 與 `!luatrace`。輸入 `!help` 可查看當前指令清單。GUI 可讀取存檔快取、保存 `.w3snapshot` 並產生差異報告；[存檔差異說明](diagnostics/save-diff-usage.md)列出比較範圍。

存檔讀取來自 DzAPI 插件快取；寫入 Native 回傳成功也不保證平台已持久化，詳見[存檔讀寫研究](diagnostics/save-io-research.md)。全圖擊殺只在本機執行，連線對戰可能不同步，也不會重置地圖的一次性掉落或存檔旗標，詳見[擊殺功能說明](diagnostics/hero-stats-and-mass-kill.md)。

## 開發注意事項

- `src/dz_registry.h` 由 `scripts/generate_dz_registry.py` 依已驗證的插件產生；更換插件版本時需重新核對入口與簽名。
- DLL 的 Native 呼叫、注入器與記憶體欄位依賴 x86 ABI；不要直接改為 x64 建置。
- 若某功能未出現在原始碼與控制台指令表，請勿依賴舊研究紀錄中的規劃內容。
