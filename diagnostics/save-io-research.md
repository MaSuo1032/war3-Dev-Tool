# 存檔讀寫改善（2026-10-29）

研究依據：DzAPI_Research/DzAPI_Function_List.md 的 DzAPI_Map_GetServerValue 條目指出該 native 讀取插件的平台值快取，沒有自行送出網路請求；SaveServerValue 條目指出它轉交給插件寫入流程。其簽名分別是 (Hplayer;S)S 和 (Hplayer;SS)B，第一參數是玩家 handle，不應與玩家席位 1–12 混淆。研究文件的 509 native 清單來自舊版插件；目前已驗證的 2026-10-28 插件由 generate_dz_registry.py 產生 517 個條目。

本次改動：
- 核心寫入會先執行 native，再記錄 Native result。只有 native 回傳非零才列入 local_save.txt 的 SAVED 記錄；這仍不能證明平台已持久化。
- 核心讀取附上原始快取錯誤碼（-1 表示無法取得該 native），明確標示 Cached Value。空值不代表資料不存在。
- GUI 手動寫入僅顯示「已送出／待回報」，不再直接更新監控表，以免污染快照差異。批次讀取明確說明是讀插件快取。
- 完整驗證玩家 handle 的正整數格式及 Key/value 長度與分隔字元，超長內容不再默默截斷成另一筆寫入。

使用建議：先在遊戲中載入資料，從監控記錄複製實際玩家 handle，再進行讀寫。寫入後檢查 dztrace.log 的 Native result，並在遊戲中重新載入資料確認持久化。舊注入 DLL 無法熱更新；使用 bin/SaveIO-20261029 完整套件時需重啟遊戲後重新注入。四項 CTest 回歸測試通過；平台端持久化需實際遊戲驗證。
