# 英雄三屬性快捷鍵與全圖敵對單位擊殺評估

## Ctrl+H：選取英雄三屬性設為 1 億

在遊戲視窗選取一名英雄後按 `Ctrl+H`，或在控制台輸入 `!hero100m`。DLL 透過 `DzGetSelectedLeaderUnit` 取得選取 handle，再以 `GetUnitTypeId`、`IsHeroUnitId` 確認是英雄；只有工作區內已核對的 Game.dll 版本與 native 入口位元組符合時，才呼叫 `SetHeroStr`、`SetHeroAgi`、`SetHeroInt`，各傳入 `100000000` 與 `permanent=true`。長按快捷鍵不會重複觸發。

靜態逆向依據：`Game.dll` 的 native 註冊區在 VA `0x6F1EBCAB`、`0x6F1EBCBF`、`0x6F1EBCD3` 分別把 `SetHeroStr`、`SetHeroAgi`、`SetHeroInt` 與 `(Hunit;IB)V` 註冊到 RVA `0x1F5890`、`0x1F5720`、`0x1F5780`。三個 wrapper 會驗證單位的英雄能力並呼叫遊戲內部 setter，因此不需要直接改 CAbilityHero 記憶體欄位。`GetUnitTypeId` 和 `IsHeroUnitId` 的名稱、簽名亦已從註冊區核對。

已通過 x86 編譯與既有 hook regression；由於目前沒有執行中的 War3，尚未確認目標地圖是否在執行中覆蓋屬性、UI 是否顯示完整 1 億或衍生生命／攻擊值是否有地圖自訂上限。首次使用應在可重開的測試局確認遊戲內實際數值。

## 一鍵擊殺全圖敵對單位：本機實作

在遊戲視窗按 `Ctrl+Shift+K`，或於控制台／GUI 指令框輸入 `!killall`。以 `Ctrl+Shift+X` 或 `!killall stop` 取消未執行的項目。DLL 使用本機玩家作為敵對判準，以 `GetWorldBounds`、`CreateGroup`、`GroupEnumUnitsInRect` 對全圖單位取一次快照，依 `GetOwningPlayer`、`IsPlayerEnemy`、`GetWidgetLife` 篩選，再以 `KillUnit` 走遊戲的死亡路徑。只處理已核對的 1.27 `Game.dll`，其餘版本拒絕呼叫。

每次視窗計時器處理最多 16 隻，間隔 50 毫秒；每隻在擊殺前重新確認 handle 對應的種類、擁有者、敵對關係及生命值。掃描上限 32768 隻，避免異常 group 造成無限迴圈。地圖在死亡事件中生成的新單位不在本次快照內，可再次按快捷鍵處理。

此功能只在本機呼叫，連線對戰可能不同步。大量擊殺也不會重設地圖的一次性掉落或存檔旗標；地圖的死亡觸發器可能另有限制。

靜態逆向依據：工作區的 `WarcraftⅢ-11hf/Game.dll` 登錄表確認 `GetWorldBounds` RVA `0x1E6810`、`CreateGroup` `0x1DE080`、`GroupEnumUnitsInRect` `0x1E6A50`、`FirstOfGroup` `0x1E0850`、`GroupRemoveUnit` `0x1E6E30`、`GetOwningPlayer` `0x1E3BA0`、`IsPlayerEnemy` `0x1E8090`、`GetLocalPlayer` `0x1E3150`、`GetWidgetLife` `0x1E6730`、`GetUnitTypeId` `0x1E6670`、`KillUnit` `0x1EFCD0`、`DestroyGroup` `0x1DF640`、`RemoveRect` `0x1F2DD0`。啟動時再次核對 DLL 指紋與入口前綴。
