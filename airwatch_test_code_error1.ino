#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// ================= ERROR CODES =================
#define ERR_NONE        0
#define ERR_WIFI        1
#define ERR_DHT         2
#define ERR_MQ2         3
#define ERR_MQ135       4
#define ERR_CAMERA      5
#define ERR_HTTP        6
#define ERR_I2C         7
#define ERR_OTA         8

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= CUSTOM LCD CHARACTERS =================
// Professional icons para sa display
byte tempIcon[8] = {
  B00100,
  B01010,
  B01010,
  B01110,
  B01110,
  B11111,
  B11111,
  B01110
};

byte humidityIcon[8] = {
  B00100,
  B00100,
  B01010,
  B01010,
  B10001,
  B10001,
  B10001,
  B01110
};

byte wifiIcon[8] = {
  B00000,
  B01110,
  B10001,
  B00100,
  B01010,
  B00000,
  B00100,
  B00000
};

byte checkIcon[8] = {
  B00000,
  B00001,
  B00010,
  B10100,
  B01000,
  B00000,
  B00000,
  B00000
};

byte crossIcon[8] = {
  B00000,
  B10001,
  B01010,
  B00100,
  B01010,
  B10001,
  B00000,
  B00000
};

byte progressBlock[8] = {
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111,
  B11111
};

byte progressEmpty[8] = {
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001,
  B10001
};

byte arrowIcon[8] = {
  B00000,
  B00100,
  B01110,
  B11111,
  B01110,
  B00100,
  B00000,
  B00000
};

// ================= SENSORS =================
#define DHT_PIN 4
#define DHT_TYPE DHT22
#define MQ2_PIN 34
#define MQ135_PIN 35
#define STATION_ID "STN-001"

DHT dht(DHT_PIN, DHT_TYPE);

// ================= WIFI & SERVER =================
const char* WIFI_SSID = "Ron da ber";
const char* WIFI_PASS = "Garayrayosj";
const char* SERVER_URL = "https://airwatch-main-source-production.up.railway.app/api/sensor-data";

// ================= CAMERA =================
#define CAM_RX 16
#define CAM_TX 17
HardwareSerial camSerial(1);
#define CAMERA_BUFFER_SIZE 4096
uint8_t camBuffer[CAMERA_BUFFER_SIZE];
int camIndex = 0;
bool cameraWorking = false;

// ================= OTA CONFIGURATION =================
const char* GITHUB_REPO = "aisatcabug211861/airwatch-firmware";
const char* BIN_FILENAME = "airwatch.bin";
const char* CURRENT_VERSION = "1.0.0";

// OTA Pins
const int LED_PIN = 2;
const int BOOT_BUTTON = 0;

// OTA Timing
const unsigned long OTA_CHECK_INTERVAL = 600000;
const unsigned long OTA_CONFIRM_TIMEOUT = 30000;
const int WDT_TIMEOUT = 30;

// ================= TIMING =================
unsigned long lastSend = 0;
unsigned long lastLCD = 0;
unsigned long lastOTACheck = 0;
const int sendInterval = 5000;

// ================= STATUS TRACKING =================
struct ModuleStatus {
  bool wifi;
  bool dht;
  bool mq2;
  bool mq135;
  bool camera;
  bool i2c;
  int lastError;
  String errorLocation;
} status = {false, false, false, false, false, false, ERR_NONE, "None"};

// ================= OTA VARIABLES =================
Preferences prefs;
bool otaInProgress = false;
bool otaDownloading = false;
bool pendingConfirm = false;
bool updateAvailable = false;
unsigned long bootTime = 0;
unsigned long otaProgress = 0;
unsigned long otaTotal = 0;
String newVersion = "";

// ================= DISPLAY STATE VARIABLES =================
// Para hindi flickering, itatago natin ang previous values
float lastTemp = -999;
float lastHum = -999;
int lastCO2 = -1;
bool lastWifi = false;
bool lastDHT = false;
int animationFrame = 0;
unsigned long lastAnimation = 0;

// ================= BASE64 =================
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(uint8_t* data, int len) {
  String result = "";
  result.reserve(((len + 2) / 3) * 4 + 1);
  for (int i = 0; i < len; i += 3) {
    uint8_t b0 = data[i];
    uint8_t b1 = (i + 1 < len) ? data[i + 1] : 0;
    uint8_t b2 = (i + 2 < len) ? data[i + 2] : 0;
    result += b64chars[(b0 >> 2) & 0x3F];
    result += b64chars[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
    result += (i + 1 < len) ? b64chars[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
    result += (i + 2 < len) ? b64chars[b2 & 0x3F] : '=';
  }
  return result;
}

// ================= PROFESSIONAL DISPLAY FUNCTIONS =================

void initLCDCustomChars() {
  lcd.createChar(0, tempIcon);
  lcd.createChar(1, humidityIcon);
  lcd.createChar(2, wifiIcon);
  lcd.createChar(3, checkIcon);
  lcd.createChar(4, crossIcon);
  lcd.createChar(5, progressBlock);
  lcd.createChar(6, progressEmpty);
  lcd.createChar(7, arrowIcon);
}

void centerText(String text, int row) {
  int len = text.length();
  int pos = (16 - len) / 2;
  if (pos < 0) pos = 0;
  lcd.setCursor(0, row);
  lcd.print("                "); // Clear row
  lcd.setCursor(pos, row);
  lcd.print(text);
}

void drawProgressBar(int percent, int row) {
  int blocks = map(percent, 0, 100, 0, 14);
  lcd.setCursor(0, row);
  lcd.print("[");
  for (int i = 0; i < 14; i++) {
    if (i < blocks) {
      lcd.write(byte(5)); // Solid block
    } else {
      lcd.write(byte(6)); // Empty block
    }
  }
  lcd.print("]");
}

void showAnimatedLoading(String message) {
  unsigned long now = millis();
  if (now - lastAnimation > 300) {
    lastAnimation = now;
    animationFrame = (animationFrame + 1) % 4;
    
    String anim = "";
    switch(animationFrame) {
      case 0: anim = "|"; break;
      case 1: anim = "/"; break;
      case 2: anim = "-"; break;
      case 3: anim = "\\"; break;
    }
    
    lcd.setCursor(15, 1);
    lcd.print(anim);
  }
  
  lcd.setCursor(0, 0);
  lcd.print(message.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print("Loading...      ");
}

// ================= ERROR DISPLAY (Professional) =================
void showError(int code, String location) {
  status.lastError = code;
  status.errorLocation = location;
  
  if (otaInProgress) {
    Serial.printf("[ERROR] Code %d at %s (OTA active)\n", code, location.c_str());
    return;
  }
  
  lcd.clear();
  
  // Error header with icon
  lcd.setCursor(0, 0);
  lcd.write(byte(4)); // X icon
  lcd.print(" ERROR CODE ");
  lcd.print(code);
  
  // Error description
  lcd.setCursor(0, 1);
  switch(code) {
    case ERR_WIFI:  lcd.print("WiFi Failed     "); break;
    case ERR_DHT:   lcd.print("Sensor Error    "); break;
    case ERR_MQ2:   lcd.print("Gas Sensor 1    "); break;
    case ERR_MQ135: lcd.print("CO2 Sensor      "); break;
    case ERR_CAMERA:lcd.print("Camera Offline  "); break;
    case ERR_HTTP:  lcd.print("Server Error    "); break;
    case ERR_I2C:   lcd.print("Display Error   "); break;
    case ERR_OTA:   lcd.print("Update Failed   "); break;
    default:        lcd.print("System Error    "); break;
  }
  
  delay(2500);
}

// ================= OTA DISPLAY (Professional) =================
void showOTAStatus(String line1, String line2) {
  lcd.clear();
  
  // Add decorative elements
  if (line1.length() <= 14) {
    centerText(line1, 0);
  } else {
    lcd.setCursor(0, 0);
    lcd.print(line1.substring(0, 16));
  }
  
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

void showOTAProgress() {
  if (!otaDownloading) return;
  
  int percent = (otaTotal > 0) ? (otaProgress * 100 / otaTotal) : 0;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UPDATING... ");
  lcd.print(percent);
  lcd.print("%");
  
  // Professional progress bar
  drawProgressBar(percent, 1);
}

void showBootScreen() {
  lcd.clear();
  
  // Animated boot sequence
  String bootText[] = {"AIRWATCH", "SYSTEM", "INITIALIZING", "PLEASE WAIT"};
  
  for (int i = 0; i < 4; i++) {
    lcd.clear();
    centerText(bootText[i], 0);
    
    // Loading dots animation
    for (int dot = 0; dot < 3; dot++) {
      lcd.setCursor(7 + dot, 1);
      lcd.print(".");
      delay(200);
    }
    delay(300);
  }
  
  // Show version
  lcd.clear();
  centerText("AIRWATCH PRO", 0);
  centerText("v" + String(CURRENT_VERSION), 1);
  delay(1500);
}

// ================= MODULE TESTERS =================
bool testI2C() {
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() != 0) {
    Wire.beginTransmission(0x3F);
    if (Wire.endTransmission() != 0) return false;
  }
  return true;
}

bool testDHT() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  return !isnan(t) && !isnan(h);
}

bool testMQ(int pin, String name) {
  int raw = analogRead(pin);
  if (raw < 0 || raw > 4095) return false;
  if (raw == 0) {
    Serial.println(name + " WARNING: Reading 0");
    return false;
  }
  return true;
}

// ================= WIFI =================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    status.wifi = true;
    return;
  }

  if (!otaInProgress) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(byte(2)); // WiFi icon
    lcd.print(" CONNECTING...");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    if (!otaInProgress) {
      // Animated dots
      lcd.setCursor(tries % 16, 1);
      lcd.print(".");
      if (tries % 16 == 0) {
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
    }
    tries++;
    
    if (digitalRead(BOOT_BUTTON) == LOW && pendingConfirm) {
      delay(50);
      if (digitalRead(BOOT_BUTTON) == LOW) {
        rollbackFirmware();
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    status.wifi = true;
    if (!otaInProgress) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.write(byte(2)); // WiFi icon
      lcd.print(" CONNECTED!");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP().toString().substring(0, 14));
      delay(1500);
    }
  } else {
    status.wifi = false;
    if (!otaInProgress) showError(ERR_WIFI, "WiFi.begin");
  }
}

// ================= LCD REALTIME (No Flicker) =================
void updateLCD() {
  if (otaDownloading) {
    showOTAProgress();
    return;
  }
  
  if (pendingConfirm) {
    unsigned long elapsed = (millis() - bootTime) / 1000;
    int remaining = (OTA_CONFIRM_TIMEOUT / 1000) - elapsed;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("NEW FIRMWARE!   ");
    lcd.setCursor(0, 1);
    lcd.print("Confirm: ");
    lcd.print(remaining);
    lcd.print("s ");
    lcd.write(byte(4)); // X icon
    
    digitalWrite(LED_PIN, (millis() / 200) % 2);
    return;
  }
  
  if (updateAvailable && !otaInProgress) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(byte(7)); // Arrow icon
    lcd.print(" UPDATE AVAILABLE");
    lcd.setCursor(0, 1);
    lcd.print("Version: ");
    lcd.print(newVersion.substring(0, 7));
    delay(2000);
    updateAvailable = false;
    return;
  }

  // Read sensors
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int mq135Raw = analogRead(MQ135_PIN);
  int co2 = map(mq135Raw, 0, 4095, 400, 5000);
  
  // Line 1: Temperature and Humidity with Icons
  lcd.setCursor(0, 0);
  
  // Temperature (only update if changed)
  if (!isnan(temperature)) {
    status.dht = true;
    if (abs(temperature - lastTemp) > 0.1 || lastTemp == -999) {
      lcd.write(byte(0)); // Temp icon
      lcd.print(" ");
      lcd.print(temperature, 1);
      lcd.print((char)223); // Degree symbol
      lcd.print("C");
      lastTemp = temperature;
    }
  } else {
    status.dht = false;
    lcd.write(byte(0));
    lcd.print(" --.-"); 
  }
  
  // Spacer
  lcd.print(" ");
  
  // Humidity (only update if changed)
  if (!isnan(humidity)) {
    if (abs(humidity - lastHum) > 1 || lastHum == -999) {
      lcd.write(byte(1)); // Humidity icon
      lcd.print(" ");
      lcd.print(humidity, 0);
      lcd.print("%");
      lastHum = humidity;
    }
  } else {
    lcd.write(byte(1));
    lcd.print(" --%");
  }
  
  // Fill rest with spaces
  while (lcd.getCursor() < 16) lcd.print(" ");
  
  // Line 2: CO2 and Status Icons
  lcd.setCursor(0, 1);
  
  // CO2 Level with label
  if (lastCO2 != co2) {
    lcd.print("CO2:");
    lcd.print(co2);
    lcd.print("ppm");
    lastCO2 = co2;
  }
  
  // Status indicators (right side) - compact and clean
  lcd.setCursor(10, 1);
  
  // WiFi status
  if (status.wifi != lastWifi) {
    lcd.print(status.wifi ? char(2) : char(4)); // WiFi icon or X
    lastWifi = status.wifi;
  }
  
  lcd.print(" ");
  
  // DHT status
  if (status.dht != lastDHT) {
    lcd.print(status.dht ? char(3) : char(4)); // Check or X
    lastDHT = status.dht;
  }
  
  lcd.print(" ");
  
  // Camera status
  lcd.print(cameraWorking ? char(3) : char(4));
  
  lcd.print(" ");
  
  // Error or OTA indicator
  if (otaInProgress) {
    lcd.print("UP");
  } else if (status.lastError == ERR_NONE) {
    lcd.print("OK");
  } else {
    lcd.print("E");
    lcd.print(status.lastError);
  }
}

// ================= POST =================
bool sendPOST(bool includeCamera = false) {
  if (WiFi.status() != WL_CONNECTED) {
    if (!otaInProgress) showError(ERR_WIFI, "HTTP.send");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, SERVER_URL);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  int currentError = ERR_NONE;
  String errorLocation = "None";
  
  if (isnan(temperature) || isnan(humidity)) {
    status.dht = false;
    currentError = ERR_DHT;
    errorLocation = "DHT.read";
    temperature = -999;
    humidity = -999;
  } else {
    status.dht = true;
  }

  int mq2Raw = analogRead(MQ2_PIN);
  int mq135Raw = analogRead(MQ135_PIN);
  
  if (mq2Raw == 0) {
    status.mq2 = false;
    if (currentError == ERR_NONE) {
      currentError = ERR_MQ2;
      errorLocation = "MQ2.read";
    }
  } else {
    status.mq2 = true;
  }
  
  if (mq135Raw == 0) {
    status.mq135 = false;
    if (currentError == ERR_NONE) {
      currentError = ERR_MQ135;
      errorLocation = "MQ135.read";
    }
  } else {
    status.mq135 = true;
  }

  int lpg   = map(mq2Raw,  0, 4095, 0, 10000);
  int smoke = map(mq2Raw,  0, 4095, 0, 1000);
  int co2   = map(mq135Raw, 0, 4095, 400, 5000);

  String imageDataEncoded = "";
  if (includeCamera && camIndex > 100) {
    int safeSize = min(camIndex, 3000);
    imageDataEncoded = base64Encode(camBuffer, safeSize);
  }

  String json = "{";
  json += "\"station\":\"" STATION_ID "\",";
  json += "\"temperature\":" + String(temperature, 1) + ",";
  json += "\"humidity\":" + String(humidity, 1) + ",";
  json += "\"lpg\":" + String(lpg) + ",";
  json += "\"smoke\":" + String(smoke) + ",";
  json += "\"co2\":" + String(co2) + ",";
  json += "\"camera\":" + String(includeCamera ? "true" : "false") + ",";
  json += "\"imageData\":\"" + imageDataEncoded + "\",";
  json += "\"errorCode\":" + String(currentError) + ",";
  json += "\"errorLocation\":\"" + errorLocation + "\",";
  json += "\"firmwareVersion\":\"" + String(CURRENT_VERSION) + "\",";
  json += "\"otaStatus\":\"" + String(otaInProgress ? "updating" : "idle") + "\",";
  json += "\"sensorStatus\":{";
  json += "\"dht\":" + String(status.dht ? "true" : "false") + ",";
  json += "\"mq2\":" + String(status.mq2 ? "true" : "false") + ",";
  json += "\"mq135\":" + String(status.mq135 ? "true" : "false") + ",";
  json += "\"camera\":" + String(cameraWorking ? "true" : "false");
  json += "}";
  json += "}";

  Serial.println("Sending: " + json.substring(0, 100) + "...");

  int httpCode = http.POST(json);
  http.end();

  if (httpCode <= 0) {
    if (!otaInProgress) showError(ERR_HTTP, String(httpCode));
    return false;
  }
  
  status.lastError = currentError;
  status.errorLocation = errorLocation;
  
  return true;
}

// ================= OTA FUNCTIONS =================

void checkBootStatus() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  esp_err_t err = esp_ota_get_state_partition(running, &otaState);
  
  Serial.print("[OTA Boot] Partition: ");
  Serial.println(running->label);
  
  if (err == ESP_OK) {
    switch (otaState) {
      case ESP_OTA_IMG_NEW:
      case ESP_OTA_IMG_PENDING_VERIFY:
        Serial.println("[OTA Boot] New firmware - pending verification");
        pendingConfirm = true;
        bootTime = millis();
        
        esp_task_wdt_init(WDT_TIMEOUT, true);
        esp_task_wdt_add(NULL);
        
        showOTAStatus("NEW FIRMWARE!", "Hold BOOT=Rollback");
        delay(2000);
        break;
        
      case ESP_OTA_IMG_VALID:
        Serial.println("[OTA Boot] Firmware validated");
        pendingConfirm = false;
        break;
        
      case ESP_OTA_IMG_INVALID:
        Serial.println("[OTA Boot] Invalid firmware - rolling back");
        rollbackFirmware();
        break;
        
      default:
        pendingConfirm = false;
    }
  }
}

void confirmFirmware() {
  if (!pendingConfirm) return;
  
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err == ESP_OK) {
    Serial.println("[OTA] Firmware confirmed VALID");
    pendingConfirm = false;
    
    esp_task_wdt_delete(NULL);
    
    showOTAStatus("Update Success!", "New firmware OK");
    delay(2000);
  }
}

void rollbackFirmware() {
  Serial.println("[OTA] Rolling back...");
  showOTAStatus("Rolling back...", "Please wait...");
  
  esp_ota_mark_app_invalid_rollback_and_reboot();
  
  delay(1000);
  ESP.restart();
}

void checkForOTAUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (otaInProgress) return;
  if (pendingConfirm) return;
  
  Serial.println("[OTA] Checking for updates...");
  
  HTTPClient http;
  http.setTimeout(10000);
  String apiUrl = "https://api.github.com/repos/" + String(GITHUB_REPO) + "/releases/latest";
  
  http.begin(apiUrl);
  http.addHeader("User-Agent", "AirWatch-ESP32");
  http.addHeader("Accept", "application/vnd.github.v3+json");
  
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[OTA] API error: %d\n", code);
    http.end();
    return;
  }
  
  String payload = http.getString();
  http.end();
  
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("[OTA] JSON error");
    return;
  }
  
  const char* tag = doc["tag_name"];
  String latest = String(tag);
  if (latest.startsWith("v")) latest = latest.substring(1);
  
  Serial.printf("[OTA] Current: %s, Latest: %s\n", CURRENT_VERSION, latest.c_str());
  
  if (isNewerVersion(latest, String(CURRENT_VERSION))) {
    Serial.println("[OTA] Update available!");
    newVersion = latest;
    updateAvailable = true;
    
    String url = "";
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
      if (String(asset["name"]) == BIN_FILENAME) {
        url = asset["browser_download_url"].as<String>();
        break;
      }
    }
    
    if (url == "") {
      url = "https://github.com/" + String(GITHUB_REPO) + 
            "/releases/download/" + String(tag) + "/" + BIN_FILENAME;
    }
    
    delay(3000);
    downloadOTAUpdate(url);
  }
}

bool isNewerVersion(String a, String b) {
  int a1=0, a2=0, a3=0, b1=0, b2=0, b3=0;
  sscanf(a.c_str(), "%d.%d.%d", &a1, &a2, &a3);
  sscanf(b.c_str(), "%d.%d.%d", &b1, &b2, &b3);
  if (a1 != b1) return a1 > b1;
  if (a2 != b2) return a2 > b2;
  return a3 > b3;
}

void downloadOTAUpdate(String url) {
  otaInProgress = true;
  otaDownloading = true;
  
  Serial.println("[OTA] Downloading: " + url);
  showOTAStatus("OTA Starting...", "Connecting...");
  
  HTTPClient http;
  http.setTimeout(60000);
  http.begin(url);
  
  int code = http.GET();
  if (code == 302 || code == 301) {
    String newUrl = http.getLocation();
    http.end();
    http.begin(newUrl);
    code = http.GET();
  }
  
  if (code != 200) {
    Serial.printf("[OTA] Download failed: %d\n", code);
    showError(ERR_OTA, "Download");
    otaInProgress = false;
    otaDownloading = false;
    return;
  }
  
  int size = http.getSize();
  Serial.printf("[OTA] Size: %d bytes\n", size);
  
  if (!Update.begin(size)) {
    Serial.println("[OTA] Not enough space!");
    showError(ERR_OTA, "NoSpace");
    otaInProgress = false;
    otaDownloading = false;
    return;
  }
  
  Update.onProgress([](size_t done, size_t total) {
    otaProgress = done;
    otaTotal = total;
    handleSensorsDuringOTA();
  });
  
  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  http.end();
  
  otaDownloading = false;
  
  Serial.println();
  if (written == size && Update.end(true)) {
    Serial.println("[OTA] Success! Restarting...");
    showOTAStatus("OTA Complete!", "Restarting...");
    
    prefs.putBool("first_boot", true);
    delay(2000);
    ESP.restart();
  } else {
    Serial.println("[OTA] Failed!");
    showError(ERR_OTA, "FlashFail");
    Update.abort();
    otaInProgress = false;
  }
}

void handleSensorsDuringOTA() {
  while (camSerial.available()) {
    uint8_t b = camSerial.read();
    if (camIndex < CAMERA_BUFFER_SIZE) {
      camBuffer[camIndex++] = b;
    }
  }
  
  digitalWrite(LED_PIN, (millis() / 100) % 2);
}

// ================= DIAGNOSTIC MODE (Professional) =================
void runDiagnostics() {
  if (otaInProgress) return;
  
  lcd.clear();
  centerText("DIAGNOSTICS", 0);
  delay(800);

  String tests[] = {"I2C/LCD", "DHT22", "MQ2 GAS", "MQ135 CO2", "CAMERA"};
  bool* statuses[] = {&status.i2c, &status.dht, &status.mq2, &status.mq135, nullptr};
  
  for (int i = 0; i < 5; i++) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Testing: ");
    lcd.print(tests[i]);
    
    lcd.setCursor(0, 1);
    lcd.print("[");
    for (int j = 0; j < 14; j++) lcd.print(" ");
    lcd.print("]");
    
    // Simulate progress
    for (int j = 0; j < 14; j++) {
      lcd.setCursor(1 + j, 1);
      lcd.write(byte(5));
      delay(100);
    }
    
    // Test result
    bool result = false;
    switch(i) {
      case 0: result = testI2C(); break;
      case 1: result = testDHT(); break;
      case 2: result = testMQ(MQ2_PIN, "MQ2"); break;
      case 3: result = testMQ(MQ135_PIN, "MQ135"); break;
      case 4: 
        result = (camIndex > 0); 
        cameraWorking = result;
        break;
    }
    
    if (i < 4 && statuses[i] != nullptr) *statuses[i] = result;
    
    lcd.setCursor(0, 1);
    if (result) {
      lcd.print("    [");
      lcd.write(byte(3)); // Check
      lcd.print("] OK    ");
    } else {
      lcd.print("   [");
      lcd.write(byte(4)); // X
      lcd.print("] FAIL   ");
    }
    delay(600);
  }

  lcd.clear();
  centerText("SYSTEM READY", 0);
  centerText("Starting...", 1);
  delay(1000);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AirWatch Pro Display ===");
  Serial.println("Firmware: " + String(CURRENT_VERSION));

  pinMode(LED_PIN, OUTPUT);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  
  // Init I2C
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  // Initialize custom characters
  initLCDCustomChars();
  
  // Show professional boot screen
  showBootScreen();
  
  dht.begin();
  analogReadResolution(12);
  camSerial.begin(115200, SERIAL_8N1, CAM_RX, CAM_TX);
  
  prefs.begin("airwatch", false);
  checkBootStatus();
  
  runDiagnostics();
  
  connectWiFi();
  
  Serial.println("Setup complete");
  delay(1000);
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();
  
  // Handle camera data
  while (camSerial.available()) {
    uint8_t b = camSerial.read();
    if (camIndex < CAMERA_BUFFER_SIZE) {
      camBuffer[camIndex++] = b;
    }
    cameraWorking = true;
  }
  
  // Handle new firmware confirmation
  if (pendingConfirm) {
    esp_task_wdt_reset();
    
    if (now - bootTime > OTA_CONFIRM_TIMEOUT) {
      Serial.println("[OTA] Timeout - rolling back");
      rollbackFirmware();
    }
    
    if (digitalRead(BOOT_BUTTON) == LOW) {
      delay(50);
      if (digitalRead(BOOT_BUTTON) == LOW) {
        rollbackFirmware();
      }
    }
    
    static bool autoConfirm = false;
    if (!autoConfirm && now - bootTime > 15000) {
      confirmFirmware();
      autoConfirm = true;
    }
  }
  
  // Update LCD (optimized para hindi flicker)
  if (now - lastLCD >= (otaDownloading ? 500 : 1000)) {
    lastLCD = now;
    updateLCD();
  }
  
  // Send sensor data
  if (!otaDownloading && now - lastSend >= sendInterval) {
    lastSend = now;
    
    if (!status.wifi) connectWiFi();
    
    bool includeCamera = (camIndex > 200);
    bool ok = sendPOST(includeCamera);
    
    if (ok) {
      camIndex = 0;
    }
    
    static int successCount = 0;
    if (ok) {
      successCount++;
      if (successCount >= 5 && status.lastError != ERR_NONE) {
        status.lastError = ERR_NONE;
        successCount = 0;
      }
    } else {
      successCount = 0;
    }
  }
  
  // Check for OTA updates
  if (!otaInProgress && !pendingConfirm && 
      now - lastOTACheck >= OTA_CHECK_INTERVAL) {
    lastOTACheck = now;
    checkForOTAUpdate();
  }
  
  delay(10);
}