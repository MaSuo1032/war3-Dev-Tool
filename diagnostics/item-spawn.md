# 指定物品 ID 放入角色物品欄

## 靜態驗證

- 本機 `WarcraftⅢ-11hf/Game.dll` 為 1.27.0.52240：PE timestamp `0x56bd0e1c`、映像大小 `0xcd8000`、checksum `0xc96841`。
- DLL 的 Native 註冊字串含 `UnitAddItemToSlotById`，簽名 `(Hunit;II)B`；其函式位於 RVA `0x1fa7b0`。`UnitItemInSlot` 位於 `0x1faf50`，`GetItemTypeId` 位於 `0x1e2cc0`。已核對三個入口的機器碼前綴。
- 已驗證的 KK `War3Plugin.dll` 註冊 `DzGetSelectedLeaderUnit`，簽名 `()Hunit;`，可取得目前選取的單位 handle。

## 使用方式

在遊戲中選取有物品欄的角色，注入 `JassHook.dll` 後，在 GUI 指令框或 CLI 輸入：

```text
!item I000
!item I000 2
!item 0x49303030 2
```

四字元 ID 會依 Warcraft rawcode 的字元順序轉為 32 位整數。格數為 1 到 6；省略時選第一個空格。命令在遊戲視窗執行緒上呼叫引擎 Native，先確認格子為空，再檢查 Native 回傳值及該格實際的物品 ID。結果記入 `bin/dztrace.log`。

可用 `!items` 或 GUI 物品欄的「掃描六格物品 ID」按鈕列出選取角色每格的 rawcode、十六進位 ID、名稱與使用次數。掃描走 `UnitInventorySize`、`UnitItemInSlot`、`GetItemTypeId`、`GetItemName`、`GetItemCharges` Native；對應的 RVA 依序為 `0x1faee0`、`0x1faf50`、`0x1e2cc0`、`0x1e2c20`、`0x1e2be0`，已核對本機 DLL 的註冊入口和函式前綴。掃描只讀取，不修改物品。

## 尚需遊戲內驗證

目前只完成二進位分析、編譯及無遊戲回歸測試。還需要在目標地圖使用實際存在的物品 rawcode 驗證外觀、屬性與地圖事件。地圖可能阻止物品建立或限制物品欄；連線遊戲的本機呼叫可能不同步。這個操作不修改平台伺服器存檔，也不保證重新進圖後保留物品。

物品名稱從遊戲字串池複製；若地圖使用不同編碼或引擎未提供字串，日誌中的名稱可能無法正確顯示，ID 與使用次數仍可用於辨識物品。探查器尚未在實際地圖完成執行驗證。
