## 使用開發版ESP32 NodeMCU-32S

- OTA遠端更新
- GUI介面使用 SPIFFS Web Server，含側邊欄
  - WiFi連線設定
    - ESP32開AP模式供設定連線
    - 掃描附近 SSID 或手動輸入
    - 輸入密碼後連線，並將帳密存入裝置（下次開機自動連線）
  - OTA韌體更新
    - 上傳bin檔執行程式燒錄

## 專案結構

- `esp32_OTA/esp32_OTA.ino`：主程式，開啟 AP+STA 熱點、啟動 Web Server、處理 WiFi 掃描/連線與 `.bin` 上傳燒錄
- `data/index.html`：含側邊欄的 GUI（WiFi 連線設定 / OTA 韌體更新），燒錄至 SPIFFS
