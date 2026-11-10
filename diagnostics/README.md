# 研究與診斷文件

這些文件記錄特定版本的逆向結果、功能限制與當時的驗證狀態。涉及二進位位址、插件指紋或遊戲內結果時，應與目前使用的遊戲版本及最新日誌核對。

| 文件 | 主題 |
| --- | --- |
| [all-dead-local-revive.md](all-dead-local-revive.md) | 全員死亡後本機復活的通用入口與限制 |
| [blink-repair.md](blink-repair.md) | 閃現的 Native 呼叫修復與回歸範圍 |
| [hero-stats-and-mass-kill.md](hero-stats-and-mass-kill.md) | 英雄三屬性與本機全圖敵對擊殺 |
| [item-spawn.md](item-spawn.md) | 指定物品 ID 建立到選取角色物品欄 |
| [compatibility-check.md](compatibility-check.md) | Game.dll、War3Plugin.dll 指紋與 Native 就緒狀態 |
| [dummy-human-slot4.md](dummy-human-slot4.md) | KK 平台進圖後加入四號人類 dummy 的可行性 |
| [lua-trace.md](lua-trace.md) | Lua 死亡、掉寶及存檔追蹤 |
| [save-diff-usage.md](save-diff-usage.md) | GUI 存檔快照比較操作 |
| [save-io-research.md](save-io-research.md) | DzAPI 讀寫行為及玩家 handle |

`crash_*.json` 是本機產生的診斷資料，不納入版本控制。
