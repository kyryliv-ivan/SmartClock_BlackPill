#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <math.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "Audio_nopsram.h"
#include "SetupPage.h"

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
//     SETTIME:2026-08-18T12:34:00  (manual override from the setup webpage's
//                                   Date & Time card - unlike TIME: above,
//                                   STM32 applies this every time, not just
//                                   once after boot; see handleSetupSetTime())
//     STATUS:WIFI_OK,RADIO_PLAY,<ssid>  (wifi: WIFI_OK/WIFI_DOWN, radio:
//                                   RADIO_PLAY/RADIO_STOP, ssid empty
//                                   while disconnected)
//     EQ:<l0>,<l1>,<l2>,<l3>       (0-4 each: bass, low-mid, high-mid,
//                                   treble peak-hold - see eqSendIfDue())
//     SETUP_MODE:1 / SETUP_MODE:0  (1 = no saved WiFi, running the
//                                   SmartClock AP + setup webpage; 0 =
//                                   back to normal once connected)
//   STM32 -> ESP32:
//     STATION:<index>             (index into the `stations[]` table below -
//                                   sent when the user picks a station in the
//                                   OLED menu; STM32 and ESP32 must agree on
//                                   the same station list/order)
//     VOLUME:<0-10>                (STM32-side UI volume, scaled to the
//                                   Audio library's 0-21 range)
//     RECONNECT:1                  (WiFi submenu's Reconnect entry - drops
//                                   and retries the saved network)
//     FORGET_WIFI:1                 (WiFi submenu's Forget WiFi entry -
//                                   erases saved creds and reboots into
//                                   the SmartClock setup AP)
//     ALARM_SOUND:<0-6>             (alarm just fired - index into
//                                   alarmMelodyPaths[], plays on repeat
//                                   until ALARM_STOP)
//     ALARM_STOP:1                  (alarm dismissed on the STM32 side)
//     ALARM_PREVIEW:<0-6>           (Alarm > Melody cycling - plays once,
//                                   doesn't loop)
//     ALARM_VOLUME:<0-10>            (Alarm > Volume - separate from the
//                                   radio's VOLUME: above; forced during
//                                   ALARM_SOUND/ALARM_PREVIEW, then the
//                                   radio's own volume is restored)
HardwareSerial stm32Serial(1);

// NTP - Ukraine (EET/EEST, handles DST automatically)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffsetSec = 2 * 3600;
const int   daylightOffsetSec = 3600;

Audio audio;

// Audio_nopsram.h reports its own internal state (decode errors, "file not
// found", format problems, stream/file end, etc.) exclusively through these
// weak callbacks - without overriding them, all of that is silently
// swallowed. Needed to actually see why a local file (alarm melody) might
// fail to play, since connecttoFS() itself returns no per-failure detail.
void audio_info(const char *info) {
  Serial.print("[audio] ");
  Serial.println(info);
}
void audio_eof_mp3(const char *info) {
  Serial.print("[audio] eof_mp3: ");
  Serial.println(info);
}
void audio_eof_stream(const char *info) {
  Serial.print("[audio] eof_stream: ");
  Serial.println(info);
}

struct RadioStation {
  const char* name;
  const char* url;
};

// Keep this list (order and count) in sync with radio.c's `labels[]` on the
// STM32 side, since STM32 only ever sends an index, never a name/URL.
// Most stations are TAVR Media, sharing the online.<name>.ua/<Station>
// stream layout; the rest are each station's own public icecast/nginx
// endpoint. "Zakarpattya FM" is a bare IP:port, not a domain - if that IP
// ever changes, this entry is the one to fix.
const RadioStation stations[] = {
  { "Hit FM",           "http://online.hitfm.ua/HitFM" },
  { "Radio ROKS",       "http://online.radioroks.ua/RadioROKS" },
  { "Zakarpattya FM",   "http://195.234.148.51:8000/" },
  { "Kiss FM",          "http://online.kissfm.ua/KissFM" },
  { "Radio Relax",      "http://online.radiorelax.ua/RadioRelax" },
  { "Nashe Radio",      "https://online.nasheradio.ua/NasheRadio" },
  { "Ukr Radio 1",      "https://radio.ukr.radio/ur1-mp3" },
  { "Ukr Radio 2",      "https://radio.ukr.radio/ur2-mp3" },
  { "Avtoradio",        "https://cast.mediaonline.net.ua/avtoradio" },
  { "Hromadske",        "https://hromadske.radio/radio_https_upstream" },
  { "Ukr Radio 3",      "https://radio.ukr.radio/ur3-mp3" },
  { "Ukr Radio 4",      "https://radio.ukr.radio/ur4-mp3" },
  { "Kiss FM Ukr",      "https://online.kissfm.ua/KissFM_Ukr" },
  { "Kiss Digital",     "https://online.kissfm.ua/KissFM_Digital" },
  { "ROKS Ukr",         "http://online.radioroks.ua/RadioROKS_Ukr_HD" },
  { "ROKS New Rock",    "http://online.radioroks.ua/RadioROKS_NewRock_HD" },
  { "Relax Instr",      "https://online.radiorelax.ua/RadioRelax_Instrumental_HD" },
  { "Hit FM Ukr",       "http://online.hitfm.ua/HitFM_Ukr" },
  { "Hit FM Top",       "http://online.hitfm.ua/HitFM_Top" },
  { "Melodia Romantic", "http://online.melodiafm.ua/MelodiaFM_Romantic_Live" },
  { "Bayraktar",        "https://online.radiobayraktar.ua/RadioBayraktar" },
  { "Nakypilo",         "https://radiostream.nakypilo.ua/full" },
  { "MFM Ukraine",      "https://radio.mfm.ua/online128" },
  { "Lviv Hvylya",      "http://onair.lviv.fm:8000/lviv32.fm" },
  { "Radio Trek",       "http://online2.radiotrek.rv.ua:8000/AAC+_64" },
  { "Lounge FM",        "https://cast.mediaonline.net.ua/loungefm320" },
  { "Jazz FM",          "http://online.radiojazz.ua/RadioJazz" },
  { "Radio Maximum",    "https://lux.radio.tvstitch.com/kyiv/max_adv_sd" },
  { "Zahid FM",         "https://radio.zfm.com.ua:8443/zfm" },
};
const uint8_t STATION_COUNT = sizeof(stations) / sizeof(stations[0]);

// --- Alarm melodies ---------------------------------------------------------
// Real mp3 files, placed in the sketch's data/ folder and flashed to SPIFFS
// via Arduino's "Sketch Data Upload" tool (separate from the normal sketch
// Upload). Audio_nopsram.h decodes them directly with connecttoFS() - no
// on-device synthesis needed anymore (an earlier version of this generated
// WAV files from scratch at boot; real recordings sound better and this is
// far less code).
#define ALARM_MELODY_COUNT  5

const char *alarmMelodyPaths[ALARM_MELODY_COUNT] = {
  "/alarm1.mp3", "/alarm2.mp3", "/alarm3.mp3", "/alarm4.mp3", "/alarm5.mp3"
};

bool    g_alarmSoundActive = false;
uint8_t g_alarmMelodyIndex = 0;

// Tracks who currently "owns" the single shared Audio object (radio /
// ringing alarm / one-shot melody preview) so volume can be saved and
// restored correctly across the handoffs.
typedef enum { AUDIO_SRC_NONE, AUDIO_SRC_RADIO, AUDIO_SRC_ALARM, AUDIO_SRC_PREVIEW } audio_src_t;
audio_src_t g_audioSource = AUDIO_SRC_NONE;

// Alarm/preview forces this volume (independent of whatever the radio is
// set to - a quiet radio setting must never mean a quiet alarm), then
// restores whatever the radio's volume was once the ring/preview ends.
// 8 matches the ESP32-scale default set in setup() - same formula as
// setVolumeFromStm32() (STM32 0-10 -> ESP32 0-21 range).
uint8_t g_alarmVolumeRaw       = 8;
uint8_t g_savedVolumeBeforeAlarm = 8;

// Only snapshot the volume to restore later on the *first* entry into
// alarm/preview mode - if a preview re-triggers while already previewing
// (fast scrolling through the melody list), a second snapshot here would
// capture g_alarmVolumeRaw itself instead of the real original level.
void enterAlarmVolume() {
  if (g_audioSource != AUDIO_SRC_ALARM && g_audioSource != AUDIO_SRC_PREVIEW) {
    g_savedVolumeBeforeAlarm = audio.getVolume();
  }
  audio.setVolume(g_alarmVolumeRaw);
}

void playAlarmSound(uint8_t index) {
  if (index >= ALARM_MELODY_COUNT) index = 0;
  enterAlarmVolume();
  g_alarmMelodyIndex = index;
  g_alarmSoundActive = true;
  g_audioSource = AUDIO_SRC_ALARM;
  // stopSong() first - connecttoFS() (local file) after connecttohost()
  // (radio stream) without it can leave the decoder in the wrong internal
  // mode and play nothing.
  audio.stopSong();
  audio.connecttoFS(SPIFFS, alarmMelodyPaths[index]);
}

void stopAlarmSound() {
  g_alarmSoundActive = false;
  g_audioSource = AUDIO_SRC_NONE;
  audio.stopSong();
  audio.setVolume(g_savedVolumeBeforeAlarm);
}

bool g_wifiReady = false;
bool g_ntpSynced = false;
uint8_t g_currentStation = 0;
bool g_setupMode = false; // true while the setup AP (see below) is up

// Limits how many times a dropped connection retries before giving up and
// broadcasting the setup AP instead - without this, a network that's
// reachable but never fully handshakes (bad password saved, router issue,
// etc.) retries every 5s forever, and the device never becomes usable
// either as a client or as its own AP.
#define STA_RETRY_LIMIT 3
static uint8_t g_staRetryCount = 0;

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("Причина відключення (код): ");
    Serial.println(info.wifi_sta_disconnected.reason);

    g_wifiReady = false;

    if (g_setupMode) return; // already broadcasting our own AP, nothing to retry

    g_staRetryCount++;
    if (g_staRetryCount > STA_RETRY_LIMIT) {
      Serial.println("Забагато невдалих спроб - переходжу в setup-режим");
      g_staRetryCount = 0;
      WiFi.disconnect();
      startSetupMode();
      return;
    }

    WiFi.disconnect();
    delay(5000);
    esp_wifi_connect(); // використає ті самі (вже збережені) дані
  }

  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    g_wifiReady = true;
    g_staRetryCount = 0;
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

// --- QR-code WiFi setup ----------------------------------------------------
// No saved network on first boot? Instead of the old Serial-terminal flow
// (useless without a PC), put up a WPA2-protected access point + a small
// webpage so setup works from just a phone. The AP's SSID/password are
// fixed, so the QR code that gets it (shown on the STM32 OLED, see
// wifi.c) can be a pre-rendered bitmap instead of encoded on-device.
#define SETUP_AP_SSID "SmartClock"
#define SETUP_AP_PASSWORD "SmartClock" // 10 chars - clears WPA2's 8-char minimum

WebServer setupServer(80);
DNSServer  dnsServer;

void onWifiConnected() {
  Serial.println("========================");
  Serial.println("Wi-Fi ПІДКЛЮЧЕНО!");
  Serial.println("========================");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  g_wifiReady = true;

  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

  playStation(g_currentStation);
}

void handleSetupRoot() {
  setupServer.send(200, "text/html", SETUP_PAGE_HTML);
}

void handleSetupScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  setupServer.send(200, "application/json", json);
}

void stopSetupMode() {
  dnsServer.stop();
  setupServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  g_setupMode = false;
  stm32Serial.print("SETUP_MODE:0\n");

  // Whatever the STM32 sent while setup mode was up (e.g. a stray/phantom
  // Forget WiFi tap while the QR was showing) never got read - handleStm32Rx()
  // is skipped entirely during setup mode. Without this it would fire right
  // now, seconds or minutes late, immediately after the credentials it's
  // about to erase were just saved.
  while (stm32Serial.available()) stm32Serial.read();
}

void handleSetupConnect() {
  String ssid = setupServer.arg("ssid");
  String password = setupServer.arg("password");

  setupServer.send(200, "text/html",
      "Connecting to <b>" + ssid + "</b>&hellip; you can close this page.");
  delay(300); // let the response actually go out before we touch the radio

  WiFi.mode(WIFI_AP_STA); // keep the AP up while STA tries to join

  wifi_config_t conf = {};
  strncpy((char*)conf.sta.ssid, ssid.c_str(), sizeof(conf.sta.ssid));
  strncpy((char*)conf.sta.password, password.c_str(), sizeof(conf.sta.password));
  conf.sta.pmf_cfg.capable = true;
  conf.sta.pmf_cfg.required = false;

  esp_wifi_set_config(WIFI_IF_STA, &conf); // persisted to flash by default
  esp_wifi_connect();

  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    stopSetupMode();
    onWifiConnected();
  }
  // wrong password / out of range - stays in AP+STA, user just retries via
  // the same page (still reachable at the AP's own address)
}

// Setup page's Date & Time card - lets the user get a working clock without
// ever touching Wi-Fi (once online, NTP takes over and this is moot).
void handleSetupSetTime() {
  String date = setupServer.arg("date"); // "YYYY-MM-DD" (HTML <input type=date>)
  String time = setupServer.arg("time"); // "HH:MM"      (HTML <input type=time>)

  if (date.length() != 10 || time.length() < 5) {
    setupServer.send(400, "text/plain", "Invalid date/time.");
    return;
  }

  char line[40];
  snprintf(line, sizeof(line), "SETTIME:%sT%.5s:00\n", date.c_str(), time.c_str());
  stm32Serial.print(line);

  setupServer.send(200, "text/plain", "Clock set.");
}

// Captive-portal catch-all: any URL a phone's OS probes to detect a portal
// (e.g. /generate_204, /hotspot-detect.html) redirects here instead of 404,
// which is what makes most phones auto-pop the "Sign in to network" sheet.
void handleSetupNotFound() {
  setupServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  setupServer.send(302, "text/plain", "");
}

void startSetupMode() {
  g_setupMode = true;

  // handleStm32Rx() doesn't run at all while setup mode is active (see
  // loop()) - anything already sitting in the RX buffer from before this
  // moment would otherwise just wait there and fire the instant setup mode
  // ends, regardless of how stale it is by then. Discard it.
  while (stm32Serial.available()) stm32Serial.read();

  WiFi.mode(WIFI_AP);

  // Power-save on the radio can starve the WPA handshake mid-association -
  // a well-known cause of iOS reporting "Unable to Join" on the very first
  // attempt against an ESP32 SoftAP. Must be set AFTER WiFi.mode(WIFI_AP).
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Explicit channel (1) and max_connection - some phones fail to
  // associate on whatever channel softAP() picks by default when it's
  // left unspecified.
  bool apOk = WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD, /*channel=*/1,
                           /*hidden=*/0, /*max_connection=*/4);

  dnsServer.start(53, "*", WiFi.softAPIP());

  setupServer.on("/", handleSetupRoot);
  setupServer.on("/scan", handleSetupScan);
  setupServer.on("/connect", HTTP_POST, handleSetupConnect);
  setupServer.on("/settime", HTTP_POST, handleSetupSetTime);
  setupServer.onNotFound(handleSetupNotFound);
  setupServer.begin();

  Serial.print("Setup AP \"");
  Serial.print(SETUP_AP_SSID);
  Serial.print("\" ");
  Serial.println(apOk ? "started OK" : "FAILED to start");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  stm32Serial.print("SETUP_MODE:1\n");
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
  char line[80];
  snprintf(line, sizeof(line), "STATUS:%s,%s,%s\n",
           g_wifiReady ? "WIFI_OK" : "WIFI_DOWN",
           audio.isRunning() ? "RADIO_PLAY" : "RADIO_STOP",
           g_wifiReady ? WiFi.SSID().c_str() : "");
  stm32Serial.print(line);
}

// --- Equalizer bands, computed from real decoded PCM samples --------------
// audio_process_extern() is a weak hook in Audio_nopsram.h, called by
// sendBytes() with the just-decoded interleaved L/R buffer right before
// each frame is played - real audio-reactive data, not just isRunning().
// No registration call needed: defining it here overrides the weak default.
//
// 4-band split via cascaded one-pole low-pass filters ("poor man's FFT") -
// cheap enough to run per-sample without risking audio dropouts, unlike a
// real FFT would be on a chip already busy decoding + WiFi + I2S:
//   band0 bass     ( <200 Hz)  = |lp1|
//   band1 low-mid  (200Hz-1kHz)= |lp2 - lp1|
//   band2 high-mid (1-4 kHz)   = |lp3 - lp2|
//   band3 treble   ( >4 kHz)   = |mono - lp3|
static float   lp1 = 0, lp2 = 0, lp3 = 0;   // filter states, persist across calls
static uint8_t bandPeak[4] = { 0, 0, 0, 0 }; // slow-decaying peak hold per band

void audio_process_extern(int16_t* buff, uint16_t len, bool *continueI2S) {
  *continueI2S = true; // must stay true, or audio playback stops
  if (len == 0) return;

  const float a1 = 0.03f; // ~200 Hz cutoff  (assumes ~44.1kHz stream)
  const float a2 = 0.13f; // ~1 kHz cutoff
  const float a3 = 0.43f; // ~4 kHz cutoff

  int32_t bandMax[4] = { 0, 0, 0, 0 };

  for (uint16_t i = 0; i < len; i++) {
    float mono = (buff[i * 2] + buff[i * 2 + 1]) * 0.5f;

    lp1 += a1 * (mono - lp1);
    lp2 += a2 * (mono - lp2);
    lp3 += a3 * (mono - lp3);

    // lowMid/hiMid/treble are *differences* of two low-passes, so they're
    // naturally quieter than the bass band even on loud material - boost
    // them here so all 4 bars have a comparable chance of reaching the top.
    int32_t bass   = (int32_t) (fabsf(lp1)        * 1.0f);
    int32_t lowMid = (int32_t) (fabsf(lp2 - lp1)  * 1.6f);
    int32_t hiMid  = (int32_t) (fabsf(lp3 - lp2)  * 2.5f);
    int32_t treble = (int32_t) (fabsf(mono - lp3) * 4.0f);

    if (bass   > bandMax[0]) bandMax[0] = bass;
    if (lowMid > bandMax[1]) bandMax[1] = lowMid;
    if (hiMid  > bandMax[2]) bandMax[2] = hiMid;
    if (treble > bandMax[3]) bandMax[3] = treble;
  }

  for (uint8_t b = 0; b < 4; b++) {
    // lower divisor than a plain int16-range split - most music never
    // hits the true full-scale sample value, so level 4 needs to be
    // reachable well below that to ever actually show up
    uint8_t inst = min(4, (int)(bandMax[b] / 5000)); // -> 0..4
    if (inst > bandPeak[b]) bandPeak[b] = inst;
  }
}

// call from loop() - sends "EQ:<bass>,<lowMid>,<hiMid>,<treble>\n" to STM32
void eqSendIfDue() {
  static uint32_t lastDecay = 0;
  static uint32_t lastSend  = 0;

  if (millis() - lastDecay > 200) { // peak-hold bars fall slowly
    lastDecay = millis();
    for (uint8_t b = 0; b < 4; b++) {
      if (bandPeak[b] > 0) bandPeak[b]--;
    }
  }

  if (millis() - lastSend < 100) return; // throttle UART traffic
  lastSend = millis();

  char line[24];
  snprintf(line, sizeof(line), "EQ:%u,%u,%u,%u\n",
           bandPeak[0], bandPeak[1], bandPeak[2], bandPeak[3]);
  stm32Serial.print(line);
}

// index 255 is a reserved "stop playback" sentinel sent by the STM32 side
// (radio.c's "Stop" list entry), not a real station index.
//
// NOTE: stations[0] (Hit FM) sends a Content-Disposition header without a
// filename= field, which crashes Audio_nopsram.cpp's parseHttpResponseHeader()
// (NULL pointer sprintf, ~line 3575) - a bug in the vendored library itself,
// not this code. Patch: wrap that AUDIO_INFO("Filename is %s", ...) call in
// `if(pos1 > 0)`. Until patched, avoid auto-connecting to stations[0] on boot.
void playStation(uint8_t index) {
  if (index == 255) {
    Serial.println("Зупинка радіо");
    audio.stopSong();
    g_audioSource = AUDIO_SRC_NONE;
    return;
  }

  if (index >= STATION_COUNT) return;

  g_currentStation = index;
  g_audioSource = AUDIO_SRC_RADIO;

  Serial.print("Перемикання станції: ");
  Serial.println(stations[index].name);

  // stopSong() first - switching stations (or coming back from a local-file
  // melody/preview) without it can leave the decoder in the wrong internal
  // mode and play nothing.
  audio.stopSong();
  audio.connecttohost(stations[index].url);
}

// STM32-side UI works in a plain 0-10 range; the Audio library's setVolume()
// takes 0-21, so scale up (rounding to nearest) instead of just truncating.
void setVolumeFromStm32(uint8_t vol) {
  if (vol > 10) vol = 10;
  audio.setVolume((uint8_t)((vol * 21 + 5) / 10));
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

      // Alarm commands work regardless of WiFi/setup-mode state - an alarm
      // clock has to be able to ring even before it's ever been connected
      // to a network. Checked first, unconditionally.
      if (strncmp(rxLine, "ALARM_SOUND:", 12) == 0) {
        playAlarmSound((uint8_t)atoi(rxLine + 12));
      }
      else if (strncmp(rxLine, "ALARM_STOP:", 11) == 0) {
        stopAlarmSound();
      }
      else if (strncmp(rxLine, "ALARM_PREVIEW:", 14) == 0) {
        uint8_t idx = (uint8_t)atoi(rxLine + 14);
        if (idx >= ALARM_MELODY_COUNT) idx = 0;
        enterAlarmVolume(); // must run before g_audioSource changes below
        g_alarmSoundActive = false; // one-shot - don't re-trigger on EOF like a real alarm
        g_audioSource = AUDIO_SRC_PREVIEW;
        audio.stopSong(); // clean handoff from whatever was playing before (radio stream or another preview)
        audio.connecttoFS(SPIFFS, alarmMelodyPaths[idx]);
      }
      else if (strncmp(rxLine, "ALARM_VOLUME:", 13) == 0) {
        uint8_t vol = (uint8_t)atoi(rxLine + 13);
        if (vol > 10) vol = 10;
        g_alarmVolumeRaw = (uint8_t)((vol * 21 + 5) / 10); // same 0-10 -> 0-21 scaling as radio volume
        if (g_audioSource == AUDIO_SRC_ALARM || g_audioSource == AUDIO_SRC_PREVIEW) {
          audio.setVolume(g_alarmVolumeRaw); // already ringing/previewing - apply immediately
        }
      }
      // Everything below needs a resolved WiFi state - STATION: has no
      // network to stream from yet, and RECONNECT: switching WiFi modes
      // mid-setup could disrupt the AP that's currently up. Deferred until
      // setup mode ends rather than acted on and likely failing anyway.
      else if (g_setupMode) {
        // ignored for now
      }
      else if (strncmp(rxLine, "STATION:", 8) == 0) {
        playStation((uint8_t)atoi(rxLine + 8));
      }
      else if (strncmp(rxLine, "VOLUME:", 7) == 0) {
        setVolumeFromStm32((uint8_t)atoi(rxLine + 7));
      }
      else if (strncmp(rxLine, "RECONNECT:", 10) == 0) {
        Serial.println("Перепідключення WiFi...");
        WiFi.disconnect();
        g_wifiReady = false;
        tryConnectSaved();
      }
      else if (strncmp(rxLine, "FORGET_WIFI:", 12) == 0) {
        Serial.println("Забуваю WiFi, перезапуск у режим налаштування...");
        WiFi.disconnect(false, true); // erase saved creds, keep radio on
        delay(200); // let the erase land in flash before restarting
        ESP.restart();
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

  // I2S/speaker setup must work with or without WiFi ever actually
  // connecting - it used to live in onWifiConnected(), which never runs at
  // all if the device is still sitting in the setup AP. Kept AFTER
  // WiFi.mode() though (unlike an earlier version of this change) -
  // initializing I2S before the WiFi radio broke audio output entirely
  // (radio included, not just the alarm), so this ordering matters on ESP32.
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(8);

  bool connected = tryConnectSaved();

  if (connected) {
    onWifiConnected();
  } else {
    startSetupMode();
  }

  // Mounts the SPIFFS partition that "Sketch Data Upload" already flashed
  // with the alarm mp3s (see the data/ folder) - nothing to generate
  // anymore, just needs to be mounted before connecttoFS() can read them.
  SPIFFS.begin();
}

void loop() {
  if (g_setupMode) {
    dnsServer.processNextRequest();
    setupServer.handleClient();
    // No early return anymore - an alarm clock has to be able to ring
    // whether or not WiFi has ever been set up. Only the genuinely
    // WiFi-dependent bits (radio, EQ, TIME/STATUS sends) are skipped
    // further down while setup mode is still active.
  }

  // audio.isRunning() alone covers one-shot preview playback too (Alarm >
  // Melody), which sets neither g_wifiReady nor g_alarmSoundActive.
  if (g_wifiReady || g_alarmSoundActive || audio.isRunning()) {
    audio.loop();
  }

  // Audio_nopsram.h only plays a file once through - re-trigger it for as
  // long as the alarm is meant to keep ringing, so it repeats instead of
  // going silent after ~2-3s.
  if (g_alarmSoundActive && !audio.isRunning()) {
    audio.connecttoFS(SPIFFS, alarmMelodyPaths[g_alarmMelodyIndex]);
  }

  // Preview clip finished - just go quiet. Radio is NOT auto-resumed (even
  // if it was playing before the preview interrupted it) - some stations
  // send headers that crash the reconnect (see Audio_nopsram.cpp patch
  // notes), so leaving the radio off is both simpler and safer.
  if (g_audioSource == AUDIO_SRC_PREVIEW && !audio.isRunning()) {
    g_audioSource = AUDIO_SRC_NONE;
    audio.setVolume(g_savedVolumeBeforeAlarm);
  }

  // Always drained, setup mode or not - handleStm32Rx() itself gates which
  // individual commands actually run while g_setupMode is true (alarm
  // commands always do; radio/WiFi ones wait, see the comment inside).
  handleStm32Rx();

  if (g_setupMode) return; // EQ/TIME/STATUS below are meaningless without a resolved WiFi state

  eqSendIfDue();

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
