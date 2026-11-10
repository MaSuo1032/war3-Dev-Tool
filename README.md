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

雙擊 `scripts\update_and_build.bat` 可一鍵更新 `origin/main`、讀取 KK 對戰平台目前安裝的 `War3Plugin.dll`、重新產生 DzAPI 版本對應表、編譯並安裝 x86 Release 套件，最後執行回歸測試。也可在命令列執行：

```bat
scripts\update_and_build.bat --no-pause
```

預設插件路徑為專案旁的 `kkduizhan\plugins\Game\War3Plugin.dll`。若平台安裝在別處，先設定 `WAR3_PLUGIN_PATH` 為該 DLL 的完整路徑。插件由 KK 平台更新；腳本使用已安裝的版本，不會從未經驗證的網站下載 DLL。需要 Python 3 與 `pefile`、`capstone`（`python -m pip install pefile capstone`）。更新後請重新啟動遊戲並注入新的 `JassHook.dll`。

新版 DLL 也會在啟動時解析已載入的 `War3Plugin.dll`。KK 更新插件後，只要 Native 名稱、簽名與已驗證的註冊結構仍相容，就不必為了新指紋重新編譯。進圖後用 `!compat` 查看 `native resolution=runtime-parsed` 與個別 Native 的 `ready` 狀態；插件大幅改版時解析器會拒絕不確定的結果。

只編譯目前原始碼、不更新 Git 或插件對應表時，執行：

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
| 立即嘗試復活本機已死亡英雄 | — | `!revive` |
| 監看並嘗試復活本機英雄 | — | `!autorevive`；`!autorevive stop` 關閉 |
| 在選取角色物品欄建立指定物品 | — | `!item I000` 或 `!item I000 2`（第 2 格） |
| 掃描選取角色六格物品 ID | — | `!items`，或 GUI「掃描六格物品 ID」 |
| 版本相容性檢查 | — | `!compat`，或 GUI「版本相容性檢查」 |

控制台另提供 `!panel`、`!saveall`、`!save <玩家 handle> <Key> <Value>`、`!mall <0..3>`、`!percent <1..100>`、`!reload` 與 `!luatrace`。輸入 `!help` 可查看當前指令清單。GUI 可讀取存檔快取、保存 `.w3snapshot` 並產生差異報告；[存檔差異說明](diagnostics/save-diff-usage.md)列出比較範圍。

`!item` 接受地圖中的四字元物品 rawcode 或 `0x` 開頭的八位十六進位 ID；未指定格數時選第一個空格，不會覆蓋已有物品。執行結果寫入 `bin/dztrace.log`。物品 ID 必須存在於當前地圖；本機建立物品不代表平台存檔會記錄它，連線遊戲也可能不同步。實作與驗證範圍見[指定物品研究](diagnostics/item-spawn.md)。

`!items` 會透過遊戲 Native 讀取選取角色的物品欄，列出每格的四字元 ID、十六進位 ID、名稱與使用次數；結果顯示在 GUI 即時日誌或 CLI，並寫入 `bin/dztrace.log`。空格會標示 `empty`。名稱若無法從遊戲字串池解碼，會顯示 `(unavailable)`。

`!autorevive` 請在本機英雄仍存活時啟用；它每次死亡只嘗試一次 `ReviveHero`，`!autorevive stop` 可關閉。`!revive` 是單次嘗試。此功能只改本機遊戲狀態；地圖若已觸發全員死亡失敗或已移除英雄，復活可能失敗，也可能無法讓遊戲繼續。詳見[本機復活的實作與限制](diagnostics/all-dead-local-revive.md)。

DLL 啟動及 GUI／CLI 連線時會在即時日誌輸出 `[Compat]` 報告，列出 `Game.dll`、`War3Plugin.dll` 的 PE 指紋、磁碟檔 SHA-256 與所需 Native 的就緒狀態。進圖後可用 `!compat` 重新檢查。`ready` 僅表示必要入口存在；是否能在目前地圖使用，仍需實際操作確認。詳見[相容性檢查說明](diagnostics/compatibility-check.md)。

存檔讀取來自 DzAPI 插件快取；寫入 Native 回傳成功也不保證平台已持久化，詳見[存檔讀寫研究](diagnostics/save-io-research.md)。全圖擊殺只在本機執行，連線對戰可能不同步，也不會重置地圖的一次性掉落或存檔旗標，詳見[擊殺功能說明](diagnostics/hero-stats-and-mass-kill.md)。

## 開發注意事項

- `src/dz_registry.h` 由 `scripts/generate_dz_registry.py` 依已驗證的插件產生；更換插件版本時需重新核對入口與簽名。
- DLL 的 Native 呼叫、注入器與記憶體欄位依賴 x86 ABI；不要直接改為 x64 建置。
- 若某功能未出現在原始碼與控制台指令表，請勿依賴舊研究紀錄中的規劃內容。
