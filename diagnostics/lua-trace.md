# Lua 死亡／掉寶／存檔追蹤

## 使用

1. 以此專案的 `bin/JassHook.dll` 注入遊戲，進入目標地圖。
2. 執行 `bin/War3JassDevTool.exe`，輸入 `!luatrace`。控制台會把兩個 Lua 模組部署到目前遊戲的安裝目錄，並透過命名管道要求遊戲內的 Lua 引擎載入模組。
3. 在遊戲安裝目錄開啟 `war3dev_death_trace.log`。`READY death_event=true` 表示死亡事件已註冊；`HOOK_ERROR` 或 `EVENT_ERROR` 表示某項觀測不可用。
4. 正常擊殺目標怪物一次，檢查 `UNIT_DEATH`、`CreateItem`、`UnitAddItem`、`SAVE` 的時間順序。`SAVE` 由原有 DzAPI 存檔監控器寫入同一個檔案。
5. 輸入 `!luatrace stop` 停止記錄。Lua 的 `require` 會快取模組；停止後若要重新啟動，重新進入地圖以建立新 Lua 狀態。

自動載入只支援目前分析的 alpha `jass.dll`（PE 時間戳 `0x68110ecb`、映像大小 `0x737000`、校驗碼 `0x733a1d`），並且只在其 Cheat 橋接已初始化時執行。版本或入口不符會記錄拒絕原因，不會嘗試呼叫未知位址。

## 可觀測範圍

- `UNIT_DEATH` 透過所有玩家的 `EVENT_PLAYER_UNIT_DEATH` 捕捉一般戰鬥及腳本死亡。
- `KillUnit`、`CreateItem`、`UnitAddItem` 透過 `jass.hook` 觀測經 JASS 虛擬機呼叫的 native。遊戲引擎內部直接建立的掉落物可能不經這些函式。
- `SAVE` 由既有 `DzAPI_Map_SaveServerValue` 監控器捕捉；只有該 native 的存檔會出現。
- 此功能不修改怪物、物品或存檔，也不會重複觸發一次性獎勵。記錄可用來定位地圖的一次性旗標及實際獎勵呼叫鏈。

## 逆向依據與尚未驗證事項

本地 `jass.dll` 的 `exec-lua:` 處理器位於 RVA `0x296530`；反組譯顯示它讀取 JASS 字串、辨識前綴並將模組名稱送往 Lua 載入器。RVA `0x6dd6a0` 是處理器最後呼叫的原 Cheat 函式指標。這兩處都有執行前檢查。上游 [YDWE Lua 引擎載入器](https://github.com/actboy168/YDWE/blob/master/Development/Plugin/Warcraft3/yd_lua_engine/lua_engine/lua_loader.cpp) 明確顯示 `exec-lua:` 執行 `require`，而非任意 Lua 程式；[Lua 引擎說明](https://github.com/actboy168/jass2lua/blob/master/lua-engine.md) 說明 `jass.hook` 將原 native 作為最後一個參數傳入。[上游基礎函式實作](https://github.com/actboy168/YDWE/blob/master/Development/Plugin/Warcraft3/yd_lua_engine/lua_engine/fix_baselib.cpp) 顯示 `dofile` 被停用且寫檔限制在遊戲安裝目錄。

目前沒有執行中的 `War3.exe`，因此尚未驗證 alpha 二進位在實際地圖上的模組搜尋路徑、事件註冊與寫檔結果。`!luatrace` 後應以 `READY` 和至少一次實際 `UNIT_DEATH` 為驗收依據；僅看到「Start module requested」不代表腳本已載入。
