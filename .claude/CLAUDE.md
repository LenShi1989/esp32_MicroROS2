## 使用開發版ESP32 NodeMCU-32S

- OTA遠端更新
- GUI介面使用 SPIFFS Web Server，含側邊欄
  - WiFi連線設定
    - ESP32開AP模式供設定連線
    - 掃描附近 SSID 或手動輸入
    - 輸入密碼後連線，並將帳密存入裝置（下次開機自動連線）
  - OTA韌體更新
    - 上傳bin檔執行程式燒錄
  - 設備控制
    - 控制GPIO_2 LED燈 ON/OFF，使用switch button
    - 控制AGV車輛馬達
      - 前進
      - 後退
      - 左轉
        - 左右輪方向不同
      - 右轉
        - 左右輪方向不同

## 專案結構

- `esp32_OTA/esp32_OTA.ino`：主程式，開啟 AP+STA 熱點、啟動 Web Server、處理 WiFi 掃描/連線與 `.bin` 上傳燒錄
- `data/index.html`：含側邊欄的 GUI（WiFi 連線設定 / OTA 韌體更新），燒錄至 SPIFFS

## ESP32連接L298N馬達控制PIN腳

| ESP32   | L298N |
| ------- | ----- |
| GPIO_26 | INT1  |
| GPIO_25 | INT2  |
| GPIO_33 | INT3  |
| GPIO_32 | INT4  |
