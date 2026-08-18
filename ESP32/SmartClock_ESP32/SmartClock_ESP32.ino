#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include "Audio_nopsram.h"

#define I2S_BCLK  10
#define I2S_LRC   11
#define I2S_DOUT  12

// UART link to STM32 Black Pill (bidirectional):
//   ESP32 GPIO4  (TX) -> STM32 PA10 (USART1_RX)
//   ESP32 GPIO2  (RX) <- STM32 PA9  (USART1_TX)
#define STM32_TX_PIN  4
#define STM32_RX_PIN  2
#define STM32_BAUD    115200

// Line-based protocol, one message per line ('\n' terminated):
//   ESP32 -> STM32:
//     TIME:2026-08-18T12:34:56
//     STATUS:WIFI_OK,RADIO_PLAY   (or WIFI_DOWN / RADIO_BUFFERING / RADIO_STOP)
//   STM32 -> ESP32:
//     STATION:<index>             (index into the `stations[]` table below -
//                                   sent when the user picks a station in the
//                                   OLED menu; STM32 and ESP32 must agree on
//                                   the same station list/order)
HardwareSerial stm32Serial(1);

// NTP - Ukraine (EET/EEST, handles DST automatically)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffsetSec = 2 * 3600;
const int   daylightOffsetSec = 3600;

Audio audio;

struct RadioStation {
  const char* name;
  const char* url;
};

// TODO: replace with the real station list once decided - keep the STM32
// menu (whatever displays station names) in sync with this order, since
// STM32 only ever sends an index, never a name/URL.
const RadioStation stations[] = {
  { "Hit FM",     "http://online.hitfm.ua/HitFM" },
  { "Radio ROKS", "http://online.radioroks.ua/RadioROKS" },
  { "Kiss FM",    "http://online.kissfm.ua/KissFM" },
};
const uint8_t STATION_COUNT = sizeof(stations) / sizeof(stations[0]);

String g_ssid;
String g_password;
bool g_wifiReady = false;
bool g_ntpSynced = false;
uint8_t g_currentStation = 0;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("Причина відключення (код): ");
    Serial.println(info.wifi_sta_disconnected.reason);

    g_wifiReady = false;

    WiFi.disconnect();
    delay(5000);
    esp_wifi_connect(); // використає ті самі (вже збережені) дані
  }

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    g_wifiReady = true;
  }
}

bool tryConnectSaved() {
  Serial.println("Спроба підключення до збереженої мережі...");

  WiFi.begin(); // без аргументів - бере останні збережені дані з флеш

  for (int i = 0; i < 20; i++) { // ~10 секунд
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return false;
}

void manualSetup() {
  Serial.println("Сканування Wi-Fi...");

  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("Мереж не знайдено.");
    return;
  }

  Serial.print("Знайдено мереж: ");
  Serial.println(n);
  Serial.println();

  for (int i = 0; i < n; i++) {
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" | RSSI: ");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm");
  }

  Serial.println();
  Serial.println("Введіть номер мережі і натисніть Enter:");

  while (!Serial.available()) delay(100);
  int selectedIndex = Serial.parseInt();
  while (Serial.available()) Serial.read();

  if (selectedIndex < 1 || selectedIndex > n) {
    Serial.println("Невірний номер. Перезапустіть плату.");
    return;
  }

  g_ssid = WiFi.SSID(selectedIndex - 1);

  Serial.print("Обрано мережу: ");
  Serial.println(g_ssid);
  Serial.println("Введіть пароль і натисніть Enter:");

  while (!Serial.available()) delay(100);
  g_password = Serial.readStringUntil('\n');
  g_password.trim();

  Serial.print("Підключення до: ");
  Serial.println(g_ssid);

  wifi_config_t conf = {};
  strncpy((char*)conf.sta.ssid, g_ssid.c_str(), sizeof(conf.sta.ssid));
  strncpy((char*)conf.sta.password, g_password.c_str(), sizeof(conf.sta.password));
  conf.sta.pmf_cfg.capable = true;
  conf.sta.pmf_cfg.required = false;

  esp_wifi_set_config(WIFI_IF_STA, &conf); // за замовчуванням зберігається у флеш
  esp_wifi_connect();

  for (int i = 0; i < 30; i++) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

// Sends "TIME:YYYY-MM-DDTHH:MM:SS\n" to STM32. Returns false if NTP time
// isn't available yet (right after boot, before the first sync completes).
bool sendTimeToStm32() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) return false;

  char line[40];
  strftime(line, sizeof(line), "TIME:%Y-%m-%dT%H:%M:%S\n", &timeinfo);
  stm32Serial.print(line);

  return true;
}

void sendStatusToStm32() {
  char line[48];
  snprintf(line, sizeof(line), "STATUS:%s,%s\n",
           g_wifiReady ? "WIFI_OK" : "WIFI_DOWN",
           audio.isRunning() ? "RADIO_PLAY" : "RADIO_STOP");
  stm32Serial.print(line);
}

void playStation(uint8_t index) {
  if (index >= STATION_COUNT) return;

  g_currentStation = index;

  Serial.print("Перемикання станції: ");
  Serial.println(stations[index].name);

  audio.connecttohost(stations[index].url);
}

// Reads whatever STM32 has sent so far, one line at a time, and dispatches
// it. Called every loop() pass - non-blocking, just drains what's already
// in the UART RX FIFO.
void handleStm32Rx() {
  static char rxLine[32];
  static uint8_t rxLen = 0;

  while (stm32Serial.available()) {
    char c = stm32Serial.read();

    if (c == '\n') {
      rxLine[rxLen] = '\0';

      if (strncmp(rxLine, "STATION:", 8) == 0) {
        playStation((uint8_t)atoi(rxLine + 8));
      }

      rxLen = 0;
    }
    else if (rxLen < sizeof(rxLine) - 1) {
      rxLine[rxLen++] = c;
    }
    /* line too long / garbage - drop silently, rxLen just stops growing
       until the next '\n' resets it */
  }
}

void setup() {
  setCpuFrequencyMhz(160); ///!!!!!!
  Serial.begin(115200);
  stm32Serial.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  delay(1000);

  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(100);

  bool connected = tryConnectSaved();

  if (!connected) {
    manualSetup();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("========================");
    Serial.println("Wi-Fi ПІДКЛЮЧЕНО!");
    Serial.println("========================");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    g_wifiReady = true;

    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(8);

    playStation(g_currentStation);
  } else {
    Serial.print("ПОМИЛКА. WiFi.status() = ");
    Serial.println(WiFi.status());
  }
}

void loop() {
  if (g_wifiReady) {
    audio.loop();
  }

  handleStm32Rx();

  static uint32_t lastTimeTx = 0;
  static uint32_t lastStatusTx = 0;
  uint32_t now = millis();

  /* TIME once a second - cheap, and it's what keeps the STM32 RTC honest */
  if (now - lastTimeTx >= 1000) {
    lastTimeTx = now;
    if (sendTimeToStm32()) g_ntpSynced = true;
  }

  /* STATUS less often - it only matters for the OLED status line */
  if (now - lastStatusTx >= 5000) {
    lastStatusTx = now;
    sendStatusToStm32();
  }
}
