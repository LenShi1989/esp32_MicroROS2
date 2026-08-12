# esp32_MicroROS2

## 使用方式

1. 於 Arduino IDE 安裝 "ESP32 Sketch Data Upload" 外掛，執行 工具 > ESP32 Sketch Data Upload，將 `data/` 資料夾內容燒錄進 SPIFFS
2. 編譯並上傳 `esp32_OTA.ino` 到 ESP32 開發板
3. 用手機或電腦連線 ESP32 的設定用熱點（預設 SSID：`ESP32-OTA-Setup`，密碼：`12345678`，可於程式碼中修改）
   - 裝置內建導引式門戶 (Captive Portal)，連上熱點後作業系統通常會自動偵測並跳出瀏覽器開啟設定頁面
   - 若未自動跳出，手動開啟瀏覽器輸入 `192.168.4.1`
4. 於「WiFi 連線設定」頁面掃描附近 SSID 或手動輸入，輸入密碼後按下連線
   - 連線成功後帳密會存入裝置（NVS），下次開機會自動嘗試連線
5. 切換到「設備控制」頁面，用開關切換控制 GPIO2 輸出 ON/OFF
6. 切換到「OTA 韌體更新」頁面，選擇更新類型後上傳 `.bin` 檔，完成後裝置自動重新開機
   - **韌體 (Firmware)**：上傳編譯產生的韌體 `.bin`，更新程式本體
   - **檔案系統 (SPIFFS)**：上傳 SPIFFS 映像檔 `.bin`，更新 `data/` 內的網頁等檔案（會整個覆蓋 SPIFFS 分割區），打包方式見下方「使用 mkspiffs 打包 SPIFFS 映像檔」

## 使用 mkspiffs 打包 SPIFFS 映像檔 (SOP)

當只修改 `data/` 資料夾內容（例如網頁 UI）、不需重新編譯燒錄整個韌體時，可用 `mkspiffs` 直接打包出 `spiffs.bin`，再到「OTA 韌體更新」頁面選擇「檔案系統 (SPIFFS)」上傳。

1. **找到 mkspiffs.exe**（隨 ESP32 開發板套件安裝）並存成變數，於 PowerShell 執行：

   ```powershell
   $mkspiffs = (Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs" -Recurse -Filter "mkspiffs.exe" |
     Select-Object -First 1).FullName
   $mkspiffs
   ```

   應會印出類似 `C:\Users\<帳號>\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe`

2. **確認 SPIFFS 分割區大小**：對應 Arduino IDE 工具 > Partition Scheme 選擇的方案，查看對應 csv 內容：

   ```powershell
   Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32" -Recurse -Filter "default.csv" -Path *\tools\partitions* |
     Get-Content
   ```

   （方案名稱不是 default 時，把 `default.csv` 換成實際檔名）若看到：

   ```
   spiffs,   data, spiffs,  0x290000,0x160000,
   ```

   代表 SPIFFS 分割區大小為 `0x160000`（起始位址 `0x290000`）。

3. **打包 `data/` 資料夾成 spiffs.bin**（在專案根目錄執行，`-s` 換成步驟 2 查到的大小）：

   ```powershell
   & $mkspiffs -c data -b 4096 -p 256 -s 0x160000 spiffs.bin
   ```

   參數說明：`-c data` 來源資料夾、`-b` block size、`-p` page size、`-s` 映像檔大小（需與分割區大小一致）

4. **上傳更新**：
   - 一般情形：連線裝置的網頁，切換到「OTA 韌體更新」，更新類型選「檔案系統 (SPIFFS)」，選擇剛產生的 `spiffs.bin` 上傳
   - 初次燒錄 / 裝置無法連線時，可改用 `esptool` 透過 USB 直接寫入該分割區（`<COM埠>` 換成裝置管理員看到的埠號，例如 `COM5`）：
     ```powershell
     esptool.py --chip esp32 --port <COM埠> --baud 460800 write_flash 0x290000 spiffs.bin
     ```
     位址需與步驟 2 查到的分割區起始位址一致

## 需要的函式庫

皆為 ESP32 Arduino Core 內建，不需額外安裝：

- `WiFi.h`
- `WebServer.h`
- `DNSServer.h`
- `Update.h`
- `SPIFFS.h`
- `Preferences.h`

## LM298N 規格

| 項目           | 參數      |
| -------------- | --------- |
| 馬達輸出電壓   | 5V – 46V  |
| 邏輯輸入電壓   | 4.5V – 7V |
| 每通道連續電流 | 2A        |
| 最大功率耗散   | 25瓦      |

L298N電機驅動模組腳位圖
L298N 模組共有 11 個腳位。

![alt text](<images/L298N Module-1.png>)

---

### 電源引腳

L298N 模組上有兩個電源輸入接腳：VS 和 VSS。

![alt text](<images/L298N Module-2.png>)

`VS` 是馬達的主要電源輸入引腳。在許多模組上，該引腳標有 +12V，但實際上它可以承受 5V 到 46V 的寬範圍直流電壓。然而，需要注意的是，馬達實際接收到的電壓會比您施加到 VS 引腳的電壓略低。這是因為 L298N 內部的 H 橋電晶體會造成約 2V 的壓降。本節將詳細介紹此壓降以及為確保馬達全速運轉應提供的電壓。

`VSS` 是晶片內部邏輯電路的電源輸入。此邏輯電路需要穩定的 5V 電源才能運作。您可以透過兩種方式提供此 5V 電源。一種方法是將外部 5V 電源直接連接到 VSS 引腳。另一種方法是使用板載 5V 穩壓器，該穩壓器從馬達電源 (VS) 獲取電源並自動為邏輯電路提供 5V 電源。使用板載穩壓器時，您無需手動將任何裝置連接至 VSS 接腳。本節將詳細介紹如何使用此穩壓器以及何時直接連接 VSS 。

接地此引腳是模組的公共接地連接點。馬達電源和ESP32都必須透過此引腳共用一個接地端。

---

### 馬達輸出引腳

這些是連接直流馬達的引腳。

![alt text](<images/L298N Module-3.png>)

這`OUT1` 和 `OUT2`引腳連接到你的第一個馬達（馬達 A），而`OUT3` 和 `OUT4`這些引腳連接到你的第二個馬達（馬達 B）。你可以將任何工作電壓為 5 至 46 伏特的直流馬達連接到這些引腳。

---

### 方向控制引腳

這些引腳透過接通和斷開內部H橋開關來控制馬達的旋轉方向：

![alt text](<images/L298N Module-4.png>)

該晶片為每個馬達配備了兩個方向控制引腳。`IN1` 和 `IN2`引腳控制馬達A的旋轉方向，而`IN3` 和 `IN4`銷釘控制馬達 B 的旋轉方向。

透過設定這些引腳上不同的高電平或低電平訊號組合，您可以使馬達正轉、反轉或停止。下表詳細展示了其工作原理：

| 輸入1   | 輸入2   | 旋轉方向 |
| ------- | ------- | -------- |
| 低(0)   | 低(0)   | 馬達關閉 |
| 高（1） | 低(0)   | 向前     |
| 低(0)   | 高（1） | 落後     |
| 高（1） | 高（1） | 馬達關閉 |
