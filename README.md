# Warcraft III JASS & Native Dev Tool (v2.0)

現代化、高穩定性、模組化架構的魔獸爭霸 III (Patch 1.27a) JASS 虛擬機與 Native 底層研發、監控與存檔修改工具。

---

## 🌟 專案背景與重構理念 (v2.0 Architecture)

在舊版本中，所有 Hook、通訊、商城偽裝、字串模擬與反作弊攔截邏輯全部堆疊在單一檔案中，造成高度耦合與偶發性記憶體崩潰。
**v2.0 專案全面重啟**，採用分層模組化架構（Clean Layered Architecture），將各子系統徹底解耦，提供企業級的程式碼品質與執行期穩定性：

```
war3DevTool/
├── config/             # 外部動態設定檔 (商城模式、等級、黑白名單)
├── scripts/            # 一鍵編譯與啟動腳本
├── src/                # JassHook.dll 核心源碼
│   ├── Common/         # 全域設定與執行緒安全日誌 (Logger, Config)
│   ├── Core/           # 記憶體安全防護與 Trampoline Hook 引擎
│   ├── Jass/           # JASS 類型還原、Native 註冊表與動態調度器
│   ├── Game/           # 遊戲本體探針 (單位選取、防作弊結束保護)
│   ├── Modules/        # 獨立業務插件 (商城偽裝、存檔攔截與寫入)
│   ├── IPC/            # Named Pipe 跨行程通訊
│   └── DllMain.cpp     # 乾淨的進入點與主執行緒訊息循環
├── client/             # War3JassDevTool.exe 控制台 UI (支援視覺化面板)
└── injector/           # Injector.exe 注入器
```

---

## 🔬 核心底層逆向研究成果

### 1. JASS 字串結構精確還原 (JassString)
魔獸爭霸內部之字串並非單純 C-Style 字串，若直接傳入 `const char*` 會導致遊戲立即閃退崩潰。本專案精確還原底層記憶體佈局：
* **外層結構 (`JassString`)**：偏移量 `+0x08` 指向內部字串資料節點。
* **內層結構 (`JassStringData`)**：偏移量 `+0x00` 為引用計數，`+0x1C` 為實際以 `\0` 結尾之字元指標。
* 核心模組 `JassTypes` 提供 `CreateFakeJassString` 與 `FreeFakeJassString`，保證手動寫入存檔時底層安全無崩潰。

### 2. 伺服器字串 ID 安全解碼 (`<StringID: N>`)
RPG 地圖存檔數值常以 StringPool 形式傳遞。透過特製的記憶體安全探針，能同時識別原始字串與 `0x10000` 以下的 StringID（例如 `<StringID: 1264>`），並在編輯器中無損保存與重寫。

### 3. 動態蜜罐 (Honeypot) 陷阱過濾機制
傳統黑名單作法每換一張地圖就必須重新尋找陷阱變數。經逆向分析 RPG 地圖反作弊邏輯：
* **正規商城道具**：一律由大寫英文字母與數字組成（例如 `VIP1`, `BFK`, `CLNW`）。
* **動態蜜罐道具**：由反作弊隨機字串引擎生成，**必包含小寫字母**（例如 `2d6cDMtOsa`）。
* 本專案於 `MallSpoofer` 實作特徵模式過濾器，自動放行正常道具並強制攔截動態蜜罐，徹底解決各類地圖的作弊偵測。

### 4. 防作弊遊戲結束保護 (Anti-GameOver Protection)
當防作弊觸發時，地圖通常會調用多層結束函式：
* 靜態 Hook：`EndGame`, `RestartGame`, `RemovePlayer`
* 使用者控制保護：攔截 `EnableUserControl(false)`，確保鍵盤滑鼠操作權限不被剝奪
* 戰役失敗彈窗攔截：動態攔截包含「敗」、「Defeat」、「作弊」、「Fail」等關鍵字的對話框（`DialogSetMessage`、`DialogAddQuitButton`、`DialogDisplay`），從根本阻止失敗視窗彈出。

### 5. 主執行緒安全調度 (Main-Thread Marshaling)
Named Pipe 通訊處於獨立背景執行緒，若跨執行緒直接操作魔獸內部狀態或調用 Native，必定發生競態衝突（Race Condition）。
本專案掛鉤遊戲主視窗之 `WndProc`，跨執行緒請求透過自訂 Windows 訊息（`WM_WAR3_SAVE_SERVER`, `WM_WAR3_INVOKE_NATIVE`, `WM_WAR3_KILL_UNIT`）同步轉發至**遊戲主循環執行緒**中執行，確保 100% 執行緒安全。

---

## 🛠️ 編譯與建置 (Build)

### 環境需求
* Windows 10 / 11
* CMake 3.10+
* Visual Studio 2019 / 2022 (安裝 C++ 桌面開發工作負載，支援 32-bit MSVC)

### 一鍵編譯
執行 `scripts` 目錄下的建置腳本：
```cmd
scripts\build.bat
```
編譯完成後，二進位檔會自動輸出至專案下的 `bin\` 目錄：
* `bin\JassHook.dll`
* `bin\War3JassDevTool.exe`
* `bin\Injector.exe`
* `bin\JassHookConfig.ini`
* `bin\mall_blacklist.txt`
* `bin\mall_spoof.txt`

---

## 📖 使用指南

### 1. 注入外掛
1. 啟動魔獸爭霸 III (1.27a) 並載入目標地圖。
2. 執行 `bin\Injector.exe`，程式將自動偵測 `war3.exe` 並注入 `JassHook.dll`。
3. 注入後魔獸爭霸會伴隨開啟專屬監控主控台視窗。

### 2. 開啟控制台
執行 `bin\War3JassDevTool.exe`，成功連接命名管道後即可輸入指令：

#### 存檔即時視覺化面板
```text
> panel
```
* 自動掃描地圖在背景載入的所有存檔 Key。
* 自動彙整至 `save_panel.txt` 並用記事本開啟。
* 在記事本中修改目標數值（例如將 `ISBB=100` 改為 `ISBB=99999`）並存檔。
* 回到控制台輸入：
```text
> saveall
```
* 一鍵批次寫入所有修改數值至地圖！

#### 單筆即時寫入
```text
> save 1048602 ISBB 99999
> save 1048602 ISBB <StringID: 1264>
```

#### 防結束保護開關
```text
> antiover 1   # 開啟防作弊遊戲結束保護
> antiover 0   # 關閉保護
```

#### 商城模式切換
```text
> mall 2       # 100% 全開商城道具
> mall 1       # 白名單模式 (讀取 mall_spoof.txt)
> mall 0       # 關閉商城偽裝
> percent 30   # 隨機開啟 30% 商城道具 (避免全開失去遊戲性)
```

#### 即時控制
```text
> kill         # 秒殺當前選取的單位
```

---

## ⚖️ 授權條款 (License)
本專案僅供逆向工程學術研究、安全測試與單機模組開發用途。
