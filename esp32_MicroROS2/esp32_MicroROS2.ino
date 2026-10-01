/*
  esp32_OTA
  開發板: ESP32 NodeMCU-32S

  功能:
    - 開機先進入 AP 模式 (WIFI_AP_STA), 提供設定用的 WiFi 熱點
    - 若先前已儲存過 WiFi 帳密 (存於 NVS), 會自動嘗試以 STA 模式連線
    - STA 成功取得 IP 後自動關閉設定用 AP 熱點 (避免 AP/STA 長時間併存導致 "CCMP replay detected" 連線不穩定);
      STA 若之後斷線則自動重新開啟 AP 熱點, 確保仍可連上裝置進行設定或救援
    - 使用 SPIFFS 存放網頁 (data/index.html), 提供含側邊欄的瀏覽器 GUI
      - WiFi 連線設定: 掃描附近 SSID 或手動輸入, 輸入密碼後連線並儲存; 亦可清除已儲存的帳密並重新開機回到設定狀態
      - 設備控制: 開關切換控制 GPIO2 (ON/OFF); D-pad 按住方向按鈕控制 L298N 馬達前進/後退/左轉/右轉,
        放開按鈕自動停止; 速度調棒以 ENA/ENB 的 PWM duty 調整轉速 (0-100%)
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
    5. 於「設備控制」頁面用開關切換 GPIO2 輸出 ON/OFF, 按住 D-pad 方向按鈕控制 AGV 馬達移動,
       並可拖曳速度調棒調整轉速
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

// ==== L298N 馬達方向控制腳位 ====
// 馬達 A (左輪): IN1/IN2, 馬達 B (右輪): IN3/IN4
const int MOTOR_A_IN1 = 27;
const int MOTOR_A_IN2 = 26;
const int MOTOR_B_IN3 = 25;
const int MOTOR_B_IN4 = 33;

// ==== L298N 馬達 PWM 調速腳位 (ENA/ENB) ====
const int MOTOR_A_ENA = 14;
const int MOTOR_B_ENB = 32;

const int PWM_FREQ_HZ = 5000;
const int PWM_RESOLUTION_BITS = 8;  // duty 0-255

const byte DNS_PORT = 53;
DNSServer dnsServer;

WebServer server(80);
Preferences preferences;

bool gpioState = false;

// ==== 手動連線 (網頁觸發) 的非阻塞狀態 ====
// STA 連上新 WiFi 時, 若與 AP 熱點頻道不同會強制切換 AP 頻道, 使瀏覽器與 AP 的 TCP 連線短暫中斷;
// 若在此同一次 HTTP 請求內阻塞等待連線結果, 回應可能來不及送達前端 (出現 "連線要求失敗")。
// 因此改為: 收到連線請求後立即回應「連線中」, 實際連線結果由 loop() 非阻塞處理, 前端再輪詢 /status 取得結果。
bool wifiConnectPending = false;
String wifiConnectSsid = "";
String wifiConnectPassword = "";
unsigned long wifiConnectStartMs = 0;

// 目前馬達動作: stop / forward / backward / left / right
String motorAction = "stop";

// 馬達速度百分比 (0-100), 透過 ENA/ENB 的 PWM duty 控制
int motorSpeed = 100;

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

// ---------- 手動連線 (網頁觸發, 非阻塞) ----------

// 啟動一次非阻塞連線, 實際結果由 loop() 中的 updateWifiConnectProgress() 輪詢判斷
void startWifiConnect(const String &ssid, const String &password) {
  Serial.printf("嘗試連線 WiFi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  wifiConnectSsid = ssid;
  wifiConnectPassword = password;
  wifiConnectStartMs = millis();
  wifiConnectPending = true;
}

// 於 loop() 中呼叫: 檢查是否已連上或逾時, 連線成功才寫入 NVS 儲存帳密
void updateWifiConnectProgress() {
  if (!wifiConnectPending) return;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi 已連線, IP 位址: ");
    Serial.println(WiFi.localIP());

    preferences.begin("wifi", false);
    preferences.putString("ssid", wifiConnectSsid);
    preferences.putString("password", wifiConnectPassword);
    preferences.end();

    wifiConnectPending = false;
  } else if (millis() - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
    Serial.println("WiFi 連線失敗或逾時");
    wifiConnectPending = false;
  }
}

// ---------- AP/STA 併存穩定性處理 ----------
// ESP32 只有一組無線電, AP 與 STA 長時間併存運作時, 群組金鑰 (GTK) 的封包計數器容易互相干擾,
// 導致不斷出現 "CCMP replay detected" 而使 AP 熱點和 STA 連線都變得不穩定。
// 因此改為: STA 成功取得 IP 後自動關閉設定用 AP 熱點 (僅保留 STA, 單頻道運作最穩定);
// 若 STA 之後斷線, 則自動重新開啟 AP 熱點, 確保使用者仍能連上裝置進行設定或救援。
void onWifiStaGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (WiFi.getMode() == WIFI_AP_STA) {
    Serial.println("STA 已取得 IP, 關閉設定用 AP 熱點以避免 AP/STA 併存造成的連線不穩定");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }
}

void onWifiStaDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (WiFi.getMode() == WIFI_STA) {
    Serial.println("STA 已斷線, 重新開啟設定用 AP 熱點");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }
}

// ---------- 馬達控制 ----------

// 設定單一馬達方向: dir 1=正轉(前), -1=反轉(後), 0=停止
void setMotor(int in1, int in2, int dir) {
  if (dir > 0) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  } else if (dir < 0) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

// 依目前動作與速度更新 ENA/ENB 的 PWM duty (停止時輸出 0 使馬達不轉)
void applyMotorSpeed() {
  int duty = (motorAction == "stop") ? 0 : map(motorSpeed, 0, 100, 0, 255);
  ledcWrite(MOTOR_A_ENA, duty);
  ledcWrite(MOTOR_B_ENB, duty);
}

// 依動作名稱控制左右輪 (前進/後退左右輪同方向, 左右轉左右輪反方向原地旋轉)
void applyMotorAction(const String &action) {
  if (action == "forward") {
    setMotor(MOTOR_A_IN1, MOTOR_A_IN2, 1);
    setMotor(MOTOR_B_IN3, MOTOR_B_IN4, 1);
  } else if (action == "backward") {
    setMotor(MOTOR_A_IN1, MOTOR_A_IN2, -1);
    setMotor(MOTOR_B_IN3, MOTOR_B_IN4, -1);
  } else if (action == "left") {
    setMotor(MOTOR_A_IN1, MOTOR_A_IN2, -1);
    setMotor(MOTOR_B_IN3, MOTOR_B_IN4, 1);
  } else if (action == "right") {
    setMotor(MOTOR_A_IN1, MOTOR_A_IN2, 1);
    setMotor(MOTOR_B_IN3, MOTOR_B_IN4, -1);
  } else {
    setMotor(MOTOR_A_IN1, MOTOR_A_IN2, 0);
    setMotor(MOTOR_B_IN3, MOTOR_B_IN4, 0);
  }
  motorAction = action;
  applyMotorSpeed();
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
  // STA 連線成功後會自動關閉設定用 AP 熱點 (見 onWifiStaGotIp), 此時 ap_active 為 false,
  // 前端可依此判斷是否仍要顯示設定熱點資訊
  bool apActive = (WiFi.getMode() == WIFI_AP_STA) || (WiFi.getMode() == WIFI_AP);

  String json = "{";
  json += "\"connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + (connected ? WiFi.SSID() : String("")) + "\",";
  json += "\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"ap_active\":" + String(apActive ? "true" : "false") + ",";
  json += "\"ap_ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"ap_ip\":\"" + (apActive ? WiFi.softAPIP().toString() : String("")) + "\"";
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

// POST /wifi/clear : 清除已儲存的 WiFi 帳密 (NVS) 並中斷目前連線, 回應後重新開機回到僅 AP 設定狀態
void handleWifiClear() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  server.send(200, "application/json", "{\"success\":true}");

  delay(500);
  WiFi.disconnect(true, true);
  ESP.restart();
}

// POST /connect : 依表單傳入的 ssid/password 啟動連線, 立即回應「連線中」,
// 實際連線結果交由 loop() 非阻塞處理, 前端需輪詢 /status 取得最終結果
// (若在此阻塞等待連線完成才回應, STA 連線造成的 AP 頻道切換可能使回應來不及送達前端)
void handleConnect() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"請先選擇或輸入 SSID\"}");
    return;
  }

  startWifiConnect(ssid, password);
  server.send(200, "application/json", "{\"success\":true,\"connecting\":true}");
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

// GET /motor/status : 回傳目前馬達動作與速度 JSON
void handleMotorStatus() {
  String json = "{\"action\":\"" + motorAction + "\",\"speed\":" + String(motorSpeed) + "}";
  server.send(200, "application/json", json);
}

// POST /motor/set : 依表單傳入的 action (forward/backward/left/right/stop) 控制馬達
void handleMotorSet() {
  String action = server.arg("action");
  if (action != "forward" && action != "backward" && action != "left" && action != "right") {
    action = "stop";
  }
  applyMotorAction(action);

  String json = "{\"success\":true,\"action\":\"" + motorAction + "\",\"speed\":" + String(motorSpeed) + "}";
  server.send(200, "application/json", json);
}

// POST /motor/speed : 依表單傳入的 speed (0-100) 調整馬達轉速
void handleMotorSpeed() {
  int speed = server.arg("speed").toInt();
  motorSpeed = constrain(speed, 0, 100);
  applyMotorSpeed();

  String json = "{\"success\":true,\"speed\":" + String(motorSpeed) + "}";
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

  pinMode(MOTOR_A_IN1, OUTPUT);
  pinMode(MOTOR_A_IN2, OUTPUT);
  pinMode(MOTOR_B_IN3, OUTPUT);
  pinMode(MOTOR_B_IN4, OUTPUT);

  // ENA/ENB 以 LEDC PWM 輸出控制轉速 (ESP32 Core 3.x API, channel 由底層自動配置)
  ledcAttach(MOTOR_A_ENA, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttach(MOTOR_B_ENB, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);

  applyMotorAction("stop");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 掛載失敗");
  }

  // STA 連上/斷線時自動切換是否開啟 AP 熱點, 避免 AP/STA 長時間併存造成連線不穩定
  WiFi.onEvent(onWifiStaGotIp, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(onWifiStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // 同時開啟 AP (供設定用) 與 STA (連上既有 WiFi)
  WiFi.mode(WIFI_AP_STA);

  // 關閉 modem-sleep 省電模式: 省電模式下收發 GTK 金鑰更新封包時機不穩,
  // 常導致 ESP32 誤判 "CCMP replay detected" 而丟包斷線 (AP_STA 併存時更明顯)
  WiFi.setSleep(false);
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
  server.on("/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/gpio/status", HTTP_GET, handleGpioStatus);
  server.on("/gpio/set", HTTP_POST, handleGpioSet);
  server.on("/motor/status", HTTP_GET, handleMotorStatus);
  server.on("/motor/set", HTTP_POST, handleMotorSet);
  server.on("/motor/speed", HTTP_POST, handleMotorSpeed);

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
  updateWifiConnectProgress();
}
