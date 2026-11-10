# 2026-10-28 閃現修復

閃現改以已驗證的 native registry 呼叫 DzGetSelectedLeaderUnit、DzGetMouseTerrainX/Y、DzSetUnitPosition。使用 JASS unit handle 與 real 指標參數，不再以內部單位指標呼叫猜測的 CWidget::SetCoord，也不再讀取固定的滑鼠座標全域位址。

使用：重新啟動魔獸以卸載舊 DLL，啟動 bin/War3DevToolUI.exe 並注入。選取單位，移動鏡頭到目的地，把滑鼠放在遊戲地面後按 Ctrl+B。面板按鈕提供準備提示；點擊面板時的滑鼠位置不會再拿來當遊戲目標。座標 (0,0) 合法，缺少 native、沒有選取或座標非有限/超出檢查範圍時取消。

回歸測試包含 native 缺失、正確 unit handle/real 指標、零座標、無選取、非法座標及準備模式。這些是隔離測試，不能取代實際地圖測試；地圖自身的移動限制、鏡頭範圍或位置重設邏輯仍可能影響結果。
