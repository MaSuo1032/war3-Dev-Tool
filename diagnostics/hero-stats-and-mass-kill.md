# 英雄三屬性快捷鍵與全圖敵對單位擊殺評估

## Ctrl+H：選取英雄三屬性設為 1 億

在遊戲視窗選取一名英雄後按 `Ctrl+H`，或在控制台輸入 `!hero100m`。DLL 透過 `DzGetSelectedLeaderUnit` 取得選取 handle，再以 `GetUnitTypeId`、`IsHeroUnitId` 確認是英雄；只有工作區內已核對的 Game.dll 版本與 native 入口位元組符合時，才呼叫 `SetHeroStr`、`SetHeroAgi`、`SetHeroInt`，各傳入 `100000000` 與 `permanent=true`。長按快捷鍵不會重複觸發。

靜態逆向依據：`Game.dll` 的 native 註冊區在 VA `0x6F1EBCAB`、`0x6F1EBCBF`、`0x6F1EBCD3` 分別把 `SetHeroStr`、`SetHeroAgi`、`SetHeroInt` 與 `(Hunit;IB)V` 註冊到 RVA `0x1F5890`、`0x1F5720`、`0x1F5780`。三個 wrapper 會驗證單位的英雄能力並呼叫遊戲內部 setter，因此不需要直接改 CAbilityHero 記憶體欄位。`GetUnitTypeId` 和 `IsHeroUnitId` 的名稱、簽名亦已從註冊區核對。

已通過 x86 編譯與既有 hook regression；由於目前沒有執行中的 War3，尚未確認目標地圖是否在執行中覆蓋屬性、UI 是否顯示完整 1 億或衍生生命／攻擊值是否有地圖自訂上限。首次使用應在可重開的測試局確認遊戲內實際數值。

## 一鍵擊殺敵對所有單位：可行性

單機或可同步控制的測試環境可實作：在遊戲模擬執行緒建立 group，以世界邊界枚舉單位，根據 `GetOwningPlayer` 和 `IsPlayerEnemy` 相對於指定玩家過濾，檢查存活，再逐個呼叫已驗證的 `KillUnit` native。這會走死亡事件路徑，通常比直接把 HP 寫成 0 更接近正常擊殺。

不建議直接在現有 `WndProc` 快捷鍵中一次殺完：大型地圖可能有數千個單位，死亡觸發器、掉寶與伺服器存檔呼叫會在同一幀大量堆疊。較可靠的設計是先快照 handle 清單，每個遊戲 tick 分批處理，逐筆重新檢查 handle、存活和敵對關係；若地圖在死亡事件中生成新單位，應由使用者決定是否納入下一輪。

連線對戰還有同步問題：`GetLocalPlayer` 和本機快捷鍵只在其中一台電腦執行；直接從該客戶端修改遊戲狀態可能造成不同步。需要由遊戲允許的同步訊息或共同的模擬事件，使所有客戶端使用相同的玩家、目標順序與批次時機。若平台或地圖禁止此類控制，只能作為單機／自建測試局功能。大量擊殺也不會重設地圖的一次性掉落或存檔旗標。

因此目前只完成可行性分析，沒有加入全圖擊殺快捷鍵。實作前要先取得一張目標地圖的單機死亡／掉寶／存檔追蹤紀錄，以確認擊殺量、死亡事件的副作用與實際同步條件。
