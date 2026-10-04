# esp32_MicroROS2

ESP32 NodeMCU-32S 的 AGV 車輛控制台：開機自建 WiFi 熱點與導引式門戶，提供含側邊欄的網頁 GUI，可設定 WiFi、檢視系統狀態、控制板載 LED 與 L298N 馬達，並支援 OTA 遠端更新韌體與 SPIFFS 檔案系統。

- 韌體版本：**v1.1.2**（與 `esp32_MicroROS2.ino` 中的 `FIRMWARE_VERSION` 同步維護，顯示於網頁「系統狀態 > 系統資訊」）
- 開發板：ESP32 NodeMCU-32S
- 馬達驅動：L298N

## 專案結構

```
esp32_MicroROS2/
├─ esp32_MicroROS2/
│  ├─ esp32_MicroROS2.ino   主程式：AP+STA、Web Server、WiFi 設定、馬達控制、OTA 燒錄
│  └─ data/
│     └─ index.html         含側邊欄的網頁 GUI（燒錄至 SPIFFS）
├─ Docs/
│  ├─ 接線說明.md           L298N 規格、接線與 ESP32 GPIO 說明
│  └─ images/               接線圖、模組與腳位圖
└─ README.md
```

## 功能

- **系統狀態**：韌體版本與編譯時間、裝置現在時間（連網後由 NTP 校時，時區 UTC+8）、晶片／記憶體／SDK 資訊、WiFi 連線資訊（SSID / IP / 閘道 / 訊號 / MAC / 熱點連線數）、SPIFFS 容量使用率與檔案清單
- **WiFi 連線設定**：掃描附近 SSID 或手動輸入，連線成功後帳密存入 NVS，下次開機自動連線；亦可一鍵清除帳密並重開機回到僅 AP 設定狀態
- **設備控制**
  - 板載 LED（GPIO2）ON/OFF 開關
  - 影像串流：輸入外部相機（如 ESP32-CAM）的快照網址，以 1–10 FPS 定時抓取顯示，可旋轉 90°；網址／FPS／旋轉角度記於瀏覽器 localStorage
  - AGV 車輛控制-1：D-pad 按住方向按鈕控制前進／後退／左轉／右轉，放開自動停止；速度調棒 0–100%（ENA/ENB PWM）
  - AGV 車輛控制-2：可拖曳虛擬搖桿控制方向，放開自動回正並停止（速度沿用控制-1 的設定）
- **OTA 韌體更新**：可選擇更新「韌體 (Firmware)」或「檔案系統 (SPIFFS)」，上傳 `.bin` 後自動重新開機
- **介面主題**：明亮 ☀ / 暗黑 🌙 / 玻璃 🧊 三種配色循環切換，選擇記於 localStorage；手機版側邊欄改為抽屜式
- **設定用熱點**：SSID 為 `ESP32-Car-` 加上該裝置 MAC 的後四碼（例如 `ESP32-Car-3A4C`），多台裝置可直接由名稱區分
- **導引式門戶 (Captive Portal)**：連上 AP 熱點後作業系統通常會自動彈出瀏覽器開啟設定頁

## 使用方式

1. 於 Arduino IDE 安裝 "ESP32 Sketch Data Upload" 外掛，開啟 `esp32_MicroROS2/esp32_MicroROS2.ino` 後執行 工具 > ESP32 Sketch Data Upload，將 `esp32_MicroROS2/data/` 資料夾內容燒錄進 SPIFFS
2. 編譯並上傳 `esp32_MicroROS2.ino` 到 ESP32 開發板
3. 用手機或電腦連線 ESP32 的設定用熱點（SSID：`ESP32-Car-xxxx`，`xxxx` 為該裝置 MAC 的後四碼；密碼：`12345678`，前綴與密碼可於程式碼的 `AP_SSID_PREFIX` / `AP_PASSWORD` 修改）
   - 每台裝置的 SSID 後四碼皆不同，多台同時開機時可直接由熱點名稱分辨；實際 SSID 也會顯示在序列埠與網頁「系統狀態 > WiFi 連線資訊 > 設定用熱點」
   - 裝置內建導引式門戶 (Captive Portal)，連上熱點後作業系統通常會自動偵測並跳出瀏覽器開啟設定頁面
   - 若未自動跳出，手動開啟瀏覽器輸入 `192.168.4.1`
4. 於「WiFi 連線設定」頁面掃描附近 SSID 或手動輸入，輸入密碼後按下連線
   - 連線成功後帳密會存入裝置（NVS），下次開機會自動嘗試連線
   - 連線為非阻塞處理，網頁會輪詢 `/status` 顯示最終結果
   - 若要更換已儲存的 WiFi，可按「清除 WiFi 設定重新連線」清除帳密並重新開機，回到僅 AP 設定狀態
5. 切換到「設備控制」頁面
   - 用開關切換控制板載 LED（GPIO2）輸出 ON/OFF
   - 於「影像串流」填入相機快照網址（例如 `http://192.168.10.142/capture`）後按「開始串流」
   - 按住 D-pad 方向按鈕或拖曳虛擬搖桿控制 AGV 車輛馬達（前進／後退／左轉／右轉），放開自動停止
   - 拖曳速度調棒調整馬達轉速（0–100%，透過 L298N 的 ENA／ENB 以 PWM 控制）
6. 切換到「OTA 韌體更新」頁面，選擇更新類型後上傳 `.bin` 檔，完成後裝置自動重新開機
   - **韌體 (Firmware)**：上傳編譯產生的韌體 `.bin`，更新程式本體
   - **檔案系統 (SPIFFS)**：上傳 SPIFFS 映像檔 `.bin`，更新 `data/` 內的網頁等檔案（會整個覆蓋 SPIFFS 分割區），打包方式見下方「使用 mkspiffs 打包 SPIFFS 映像檔」

## 接線

ESP32 連接 L298N 馬達控制腳位（馬達 A＝左輪、馬達 B＝右輪）：

| ESP32   | L298N | 說明             |
| ------- | ----- | ---------------- |
| GPIO_14 | ENA   | 馬達 A PWM 調速  |
| GPIO_27 | IN1   | 馬達 A 方向      |
| GPIO_26 | IN2   | 馬達 A 方向      |
| GPIO_25 | IN3   | 馬達 B 方向      |
| GPIO_33 | IN4   | 馬達 B 方向      |
| GPIO_32 | ENB   | 馬達 B PWM 調速  |

另外 GPIO_2 為板載 LED。L298N 與 ESP32 必須共接地，詳細規格與接線說明見 [`Docs/接線說明.md`](Docs/接線說明.md)。

## HTTP API

| 方法 | 路徑            | 參數                                          | 說明                                   |
| ---- | --------------- | --------------------------------------------- | -------------------------------------- |
| GET  | `/`             | －                                            | 回傳 SPIFFS 中的 `index.html`          |
| GET  | `/status`       | －                                            | WiFi 連線狀態（供連線後輪詢）          |
| GET  | `/system/info`  | －                                            | 系統／WiFi／SPIFFS 資訊                |
| GET  | `/scan`         | －                                            | 掃描附近 SSID                          |
| POST | `/connect`      | `ssid`, `password`                            | 啟動非阻塞連線，立即回應「連線中」     |
| POST | `/wifi/clear`   | －                                            | 清除已儲存帳密並重新開機               |
| GET  | `/gpio/status`  | －                                            | 取得 GPIO2 狀態                        |
| POST | `/gpio/set`     | `state` = `on` / `off`                        | 設定 GPIO2 輸出                        |
| GET  | `/motor/status` | －                                            | 取得目前動作與速度                     |
| POST | `/motor/set`    | `action` = `forward`/`backward`/`left`/`right`/`stop` | 控制馬達動作               |
| POST | `/motor/speed`  | `speed` = 0–100                               | 調整 PWM 轉速                          |
| POST | `/update`       | `type` = `firmware` / `spiffs` + `.bin` 檔案  | OTA 燒錄，成功後自動重開機             |

## 使用 mkspiffs 打包 SPIFFS 映像檔 (SOP)

當只修改 `esp32_MicroROS2/data/` 資料夾內容（例如網頁 UI）、不需重新編譯燒錄整個韌體時，可用 `mkspiffs` 直接打包出 `spiffs.bin`，再到「OTA 韌體更新」頁面選擇「檔案系統 (SPIFFS)」上傳。

1. **找到 mkspiffs.exe**（隨 ESP32 開發板套件安裝）並存成變數，於 PowerShell 執行：

   ```powershell
   $mkspiffs = (Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs" -Recurse -Filter "mkspiffs.exe" |
     Select-Object -First 1).FullName
   $mkspiffs
   ```

   應會印出類似 `C:\Users\<帳號>\AppData\Local\Arduino15\packages\esp32\tools\mkspiffs\0.2.3\mkspiffs.exe`

2. **確認 SPIFFS 分割區大小**：對應 Arduino IDE 工具 > Partition Scheme 選擇的方案，查看對應 csv 內容：

   ```powershell
   Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\hardware\esp32" -Recurse -Filter "default.csv" |
     Where-Object { $_.FullName -like "*\tools\partitions\*" } |
     Get-Content
   ```

   （方案名稱不是 default 時，把 `default.csv` 換成實際檔名）若看到：

   ```
   spiffs,   data, spiffs,  0x290000,0x160000,
   ```

   代表 SPIFFS 分割區大小為 `0x160000`（起始位址 `0x290000`）。

3. **打包 `data/` 資料夾成 spiffs.bin**（在專案根目錄執行，`-s` 換成步驟 2 查到的大小）：

   ```powershell
   & $mkspiffs -c esp32_MicroROS2/data -b 4096 -p 256 -s 0x160000 esp32_MicroROS2/spiffs.bin
   ```

   參數說明：`-c` 來源資料夾、`-b` block size、`-p` page size、`-s` 映像檔大小（需與分割區大小一致）

4. **上傳更新**：
   - 一般情形：連線裝置的網頁，切換到「OTA 韌體更新」，更新類型選「檔案系統 (SPIFFS)」，選擇剛產生的 `spiffs.bin` 上傳
   - 初次燒錄 / 裝置無法連線時，可改用 `esptool` 透過 USB 直接寫入該分割區（`<COM埠>` 換成裝置管理員看到的埠號，例如 `COM5`）：
     ```powershell
     esptool.py --chip esp32 --port <COM埠> --baud 460800 write_flash 0x290000 esp32_MicroROS2/spiffs.bin
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
- `time.h`
