# KK 平台已進圖後加入四號人類 dummy 的可行性

## 結論

目前沒有已驗證的方法，可在 KK 平台的對局開始後，從 `JassHook.dll` 把一個全新的 dummy 加入四號人類玩家席位。現有 JASS Native 可修改玩家物件的控制類型，但不會替它建立大廳席位、網路連線或開局時的玩家名單。只改本機資料可能讓本機腳本看到 `MAP_CONTROL_USER`，卻不能證明其他客戶端或平台承認該玩家。

## 證據

- 本機 1.27 `Game.dll` 的 Native 註冊字串包含 `SetPlayerController(Hplayer;Hmapcontrol;)V`、`GetPlayerController(Hplayer;)Hmapcontrol;` 和 `GetPlayerSlotState(Hplayer;)Hplayerslotstate;`。可讀取席位狀態，但未找到對應的 JASS `SetPlayerSlotState` Native。
- [Warcraft III Community Edition 的 common.j](https://raw.githubusercontent.com/Warcraft-III-Community-Edition/W3CE/main/Scripts/common.j)列出 `SetPlayerController`、`GetPlayerController`、`GetPlayerSlotState`，並區分 `PLAYER_SLOT_STATE_EMPTY` 與 `PLAYER_SLOT_STATE_PLAYING`。這些 API 描述玩家物件與席位狀態，沒有新增連線玩家的 API。
- [GoWarcraft3 的 dummy 客戶端](https://github.com/nielsAD/gowarcraft3/blob/master/cmd/w3gsclient/main.go)透過 `dummy.Join` 加入**大廳**；其[專案說明](https://github.com/nielsAD/gowarcraft3)也明確把 dummy 用途寫成加入大廳。這提供了開局前佔用人類席位的研究路線，沒有證明 KK 平台支援開局後補入新玩家。

## 四號位與可行時點

遊戲介面中的「玩家 4」通常對應 JASS `Player(3)`；若使用者指的是 JASS `Player(4)`，則是第五個索引位置。必須在具體地圖核對顏色、隊伍及席位設定。

| 時點 | 可能結果 |
| --- | --- |
| KK 平台已進圖 | 無已驗證的全新玩家加入流程；改控制類型不能補出真實的人類連線。 |
| 開局前的大廳 | 若主持端接受 W3GS dummy 客戶端並允許定位四號席位，可作為人類玩家加入；KK 平台是否允許未驗證。 |
| 自己主持的 LAN 大廳 | 可在隔離環境驗證 dummy 加入、席位、開局後保持連線，以及地圖對 `GetPlayerController` 和 `GetPlayerSlotState` 的判斷。 |

## 若繼續研究

先在自己主持的 1.27 LAN 測試：讓 dummy 在開局前進入第四個人類席位，進圖後記錄 `GetPlayerController(Player(3))`、`GetPlayerSlotState(Player(3))`、玩家名稱及離線事件。通過後再確認 KK 平台的大廳是否接受相同加入流程。不要以單機本地記憶體改值的結果推論多人對局會有一個真正的人類玩家。
