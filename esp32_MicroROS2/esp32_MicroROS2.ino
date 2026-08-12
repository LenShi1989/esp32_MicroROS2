/*
  esp32_OTA
  開發板: ESP32 NodeMCU-32S

  功能:
    - 開機先進入 AP 模式 (WIFI_AP_STA), 提供設定用的 WiFi 熱點
    - 若先前已儲存過 WiFi 帳密 (存於 NVS), 會自動嘗試以 STA 模式連線
    - 使用 SPIFFS 存放網頁 (data/index.html), 提供含側邊欄的瀏覽器 GUI
      - WiFi 連線設定: 掃描附近 SSID 或手動輸入, 輸入密碼後連線並儲存
      - 設備控制: 開關切換控制 GPIO2 (ON/OFF)
      - OTA 燒錄: 可選擇更新「韌體 (Firmware)」或「檔案系統 (SPIFFS)」, 上傳 .bin 檔後
        由 Update 函式庫寫入對應分割區, 完成後自動重開機
    - 內建 DNS 導引式門戶 (Captive Portal): 手機/電腦連上 AP 熱點後,
      作業系統會偵測到需要登入的網路而自動彈出瀏覽器開啟設定頁面

  使用方式:
    1. Arduino IDE: 工具 > ESP32 Sketch Data Upload, 先把 data 資料夾內容燒錄進 SPIFFS
       (需先安裝 "ESP32 Sketch Data Upload" 外掛)
    2. 編譯並上傳本程式到 ESP32
    3. 用手機/電腦連線 ESP32 熱點 (AP_SSID / AP_PASSWORD), 大多數作業系統會自動彈出瀏覽器
       開啟設定頁面; 若未自動跳出, 手動開啟瀏覽器輸入 192.168.4.1
    4. 於「WiFi 連線設定」頁面掃描或手動輸入 SSID, 輸入密碼後按下連線
       連線成功後帳密會存入裝置, 下次開機自動連線
    5. 於「設備控制」頁面用開關切換 GPIO2 輸出 ON/OFF
    6. 切換到「OTA 燒錄」頁面, 選擇更新類型 (韌體 / SPIFFS 檔案系統),
       選擇對應的 .bin 檔上傳, 完成後裝置自動重開機並執行新韌體或載入新檔案系統
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <SPIFFS.h>
#include <Preferences.h>

// ==== 設定用 AP 熱點基本資料 ====
const char *AP_SSID = "ESP32-OTA-Setup";
const char *AP_PASSWORD = "12345678";

// 嘗試以既有帳密連線 WiFi 的逾時時間 (毫秒)
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// 受控制的 GPIO 腳位
const int GPIO_CONTROL_PIN = 2;

const byte DNS_PORT = 53;
DNSServer dnsServer;

WebServer server(80);
Preferences preferences;

bool gpioState = false;

// 韌體更新結果, 用於上傳完成後回傳網頁訊息
bool updateSuccess = false;
String updateMessage = "";

// ---------- 共用工具 ----------

// 嘗試連線至指定 WiFi, 逾時則放棄, 回傳是否連線成功
bool connectToWiFi(const String &ssid, const String &password, unsigned long timeoutMs) {
  if (ssid.length() == 0) return false;

  Serial.printf("嘗試連線 WiFi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi 已連線, IP 位址: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi 連線失敗或逾時");
  return false;
}

void loadAndConnectSavedWiFi() {
  preferences.begin("wifi", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();

  if (savedSSID.length() > 0) {
    connectToWiFi(savedSSID, savedPassword, WIFI_CONNECT_TIMEOUT_MS);
  } else {
    Serial.println("尚未儲存過 WiFi 帳密");
  }
}

// ---------- 網頁路由 ----------

void handleRoot() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "找不到 index.html, 請確認已上傳 SPIFFS 資料");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

// GET /status : 回傳目前連線狀態 JSON
void handleStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String json = "{";
  json += "\"connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + (connected ? WiFi.SSID() : String("")) + "\",";
  json += "\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"ap_ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// GET /scan : 掃描附近 SSID, 回傳 JSON 陣列
void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

// POST /connect : 依表單傳入的 ssid/password 嘗試連線, 成功則存入 NVS
void handleConnect() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  bool success = connectToWiFi(ssid, password, WIFI_CONNECT_TIMEOUT_MS);

  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  if (success) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();

    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":\"" + ssid + "\"";
  } else {
    json += "\"message\":\"連線失敗, 請確認 SSID/密碼是否正確\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// GET /gpio/status : 回傳 GPIO2 目前狀態 JSON
void handleGpioStatus() {
  String json = "{\"state\":\"" + String(gpioState ? "on" : "off") + "\"}";
  server.send(200, "application/json", json);
}

// POST /gpio/set : 依表單傳入的 state (on/off) 控制 GPIO2
void handleGpioSet() {
  String state = server.arg("state");
  gpioState = (state == "on");
  digitalWrite(GPIO_CONTROL_PIN, gpioState ? HIGH : LOW);

  String json = "{\"success\":true,\"state\":\"" + String(gpioState ? "on" : "off") + "\"}";
  server.send(200, "application/json", json);
}

// 導引式門戶: 未定義的路徑一律導回設定頁, 讓作業系統的連線偵測機制自動彈出瀏覽器
void handleCaptivePortal() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// 上傳完成後回傳的結果 JSON
void handleUpdateResult() {
  String json = "{";
  json += "\"success\":" + String(updateSuccess ? "true" : "false") + ",";
  json += "\"message\":\"" + updateMessage + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// 記錄本次上傳的更新目標 (firmware / spiffs), 供上傳結束後判斷是否需要重新掛載 SPIFFS
String updateType = "firmware";

// 處理 /update 的檔案上傳流程 (multipart/form-data)
// 網頁表單需將 "type" 欄位放在檔案欄位之前送出, 上傳開始時才能讀到 server.arg("type")
void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    updateType = server.arg("type");
    if (updateType != "spiffs") updateType = "firmware";

    int command = (updateType == "spiffs") ? U_SPIFFS : U_FLASH;
    Serial.printf("開始上傳 %s: %s\n", updateType.c_str(), upload.filename.c_str());
    updateSuccess = false;
    updateMessage = "";

    // 燒錄 SPIFFS 前需先卸載, 避免寫入時檔案系統仍被掛載讀寫
    if (command == U_SPIFFS) {
      SPIFFS.end();
    }

    // 未知檔案大小時使用 UPDATE_SIZE_UNKNOWN
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, command)) {
      Update.printError(Serial);
      updateMessage = "Update.begin() 失敗";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      updateMessage = "寫入資料失敗";
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("%s 更新完成, 共 %u bytes\n", updateType.c_str(), upload.totalSize);
      updateSuccess = true;
      updateMessage = (updateType == "spiffs") ? "SPIFFS 檔案系統更新成功" : "韌體更新成功";
    } else {
      Update.printError(Serial);
      updateSuccess = false;
      updateMessage = (updateType == "spiffs") ? "SPIFFS 檔案系統更新失敗" : "韌體更新失敗";
      // 更新失敗且未重開機時, 若剛才卸載了 SPIFFS 需重新掛載, 讓網頁仍可正常讀取
      if (updateType == "spiffs") {
        SPIFFS.begin(true);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.end();
    updateSuccess = false;
    updateMessage = "上傳已取消";
    if (updateType == "spiffs") {
      SPIFFS.begin(true);
    }
    Serial.println("上傳已取消");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(GPIO_CONTROL_PIN, OUTPUT);
  digitalWrite(GPIO_CONTROL_PIN, LOW);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 掛載失敗");
  }

  // 同時開啟 AP (供設定用) 與 STA (連上既有 WiFi)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP 已啟動, SSID: ");
  Serial.print(AP_SSID);
  Serial.print(", IP 位址: ");
  Serial.println(WiFi.softAPIP());

  // DNS 全部導向 AP IP, 讓連上熱點的裝置觸發導引式門戶偵測
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  loadAndConnectSavedWiFi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/gpio/status", HTTP_GET, handleGpioStatus);
  server.on("/gpio/set", HTTP_POST, handleGpioSet);

  // /update 路由: 上傳完成回傳結果 JSON, 上傳過程呼叫 handleUpdateUpload
  server.on(
    "/update", HTTP_POST,
    []() {
      handleUpdateResult();
      delay(500);
      if (updateSuccess) {
        ESP.restart();
      }
    },
    handleUpdateUpload);

  // 各作業系統的網路連線偵測會請求各自固定的網址, 一律導回設定頁觸發自動跳轉瀏覽器
  server.onNotFound(handleCaptivePortal);

  server.begin();
  Serial.println("HTTP Server 已啟動");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
