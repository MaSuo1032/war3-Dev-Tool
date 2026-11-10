# 版本相容性檢查

`JassHook.dll` 啟動時，以及 GUI／CLI 連上 Named Pipe 時，會把 `[Compat]` 報告寫入 `bin/dztrace.log`。可在 GUI 即時日誌查看，或輸入 `!compat`／按「版本相容性檢查」重新產生報告。

報告分別列出載入的 `Game.dll` 和 `War3Plugin.dll` 的 PE timestamp、SizeOfImage、checksum，以及其磁碟檔 SHA-256。Game.dll 的預期 SHA-256 來自工作區 1.27.0.52240 版本；插件預期值由 `scripts/generate_dz_registry.py` 產生於 `src/dz_registry.h`。磁碟檔雜湊無法讀取時顯示 `unavailable`；它反映磁碟檔，不代表注入後的記憶體內容。

插件與編譯時版本相同時使用已產生的 Native 位址表；版本不同時，DLL 會掃描**已載入的** PE32 插件內的 Native 註冊指令並解析函式位置。解析結果必須符合已知名稱與簽名、位於可執行區段，且多數註冊呼叫必須指向同一入口。不符合時拒絕採用，等待進圖時由 Game.dll 註冊閘道取得真實 Native。

`[Compat] War3Plugin.dll native resolution=` 會顯示 `exact-table`、`runtime-parsed` 或 `waiting-for-registrations`。PE／SHA-256 的 `match=no` 只表示不等於編譯時插件；若 `native resolution=runtime-parsed` 且所需 Native 為 `ready`，相應指令已有可用入口。掃描器支援已驗證的註冊結構與已知函式簽名；插件大幅改版時仍需更新解析器。

Native 區塊列出選取單位、滑鼠地面位置、單位移動、存檔及 Game.dll 物品／英雄／擊殺入口的狀態，並彙總 `!kill`、`!item`、`!items`、`!blink`、`!hero100m`、`!killall`。`ready` 代表對應函式已註冊或已通過版本及入口位元組檢查；`waiting`／`missing` 表示至少一項依賴目前不可用。進圖時 Native 可能重新註冊，所以應在進圖後再跑一次 `!compat`。報告不呼叫會改變遊戲狀態的 Native，也無法證明當前地圖允許某項操作。

本機復活另列 `ReviveHero`、`!revive`、`!autorevive` 的入口檢查及監看狀態。`ready` 只代表必要入口符合本版 `Game.dll`，不保證地圖全員死亡後仍接受復活。
