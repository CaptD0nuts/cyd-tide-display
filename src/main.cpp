// Animated tide display for the Cheap Yellow Display (ESP32-2432S028R).
// WiFi and the NOAA station are set on a setup hotspot (WiFiManager); tap the
// screen to replay the next 24 hours as a time-lapse.
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include "config.h"
#include "tide.h"
#include "scene.h"

// Touch controller on its own SPI pins.
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
Preferences prefs;

static char g_station[12] = DEFAULT_STATION;
static char g_tz[48] = DEFAULT_TZ;
static char g_name[40] = "";
static TideData g_tide;
static bool g_haveData = false, g_lastOk = false;
static uint32_t g_nextTry = 0;
static bool g_lapse = false, g_wasTouched = false;
static uint32_t g_lapseStart = 0;
static uint32_t g_lastChart = 0;  // 0 = chart needs a repaint

static void loadPrefs() {
  prefs.begin("tide", true);
  prefs.getString("station", g_station, sizeof g_station);
  prefs.getString("tz", g_tz, sizeof g_tz);
  prefs.end();
  if (!g_station[0]) strlcpy(g_station, DEFAULT_STATION, sizeof g_station);
  if (!g_tz[0]) strlcpy(g_tz, DEFAULT_TZ, sizeof g_tz);
}

static void onPortal(WiFiManager *) {
  sceneMessage("SETUP", "Join WiFi: " SETUP_AP_NAME, "pick your network + tide station");
}

// force = open the setup hotspot even if saved WiFi works (user held the screen).
static void connect(bool force) {
  WiFiManagerParameter pSta("station", "NOAA tide station id (8730667 = Alabama Point, Orange Beach)", g_station, 10);
  WiFiManagerParameter pTz("tz", "POSIX time zone (Central: CST6CDT,M3.2.0,M11.1.0)", g_tz, 46);
  WiFiManager wm;
  wm.addParameter(&pSta);
  wm.addParameter(&pTz);
  wm.setAPCallback(onPortal);
  wm.setConfigPortalTimeout(300);
  bool ok = force ? wm.startConfigPortal(SETUP_AP_NAME) : wm.autoConnect(SETUP_AP_NAME);

  const char *sta = pSta.getValue(), *tz = pTz.getValue();
  if ((sta[0] && strcmp(sta, g_station)) || (tz[0] && strcmp(tz, g_tz))) {
    prefs.begin("tide", false);
    if (sta[0]) prefs.putString("station", sta);
    if (tz[0]) prefs.putString("tz", tz);
    prefs.end();
    Serial.printf("saved station %s\n", sta);
  }
  if (!ok || force) ESP.restart();  // timed out, or settings may have changed: start clean
}

static void refresh() {
  bool ok = tideFetch(g_station, g_tide);
  g_lastOk = ok;
  if (ok) {
    g_haveData = true;
    if (!g_name[0] && !tideStationName(g_station, g_name, sizeof g_name)) snprintf(g_name, sizeof g_name, "NOAA %s", g_station);
  }
  g_nextTry = millis() + (ok ? REFRESH_S * 1000UL : RETRY_MS);
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);  // landscape, USB port on the right
  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);
  sceneBegin(tft);
  loadPrefs();

  sceneMessage("TIDE", "connecting to WiFi...", "hold the screen to open setup");
  // Holding the screen through the first few seconds reopens setup.
  uint32_t t0 = millis();
  bool held = true;
  while (millis() - t0 < PORTAL_HOLD_MS) {
    if (!touch.touched()) held = false;
    delay(50);
  }
  connect(held);
  Serial.printf("wifi up, ip %s, station %s\n", WiFi.localIP().toString().c_str(), g_station);

  configTzTime(g_tz, "pool.ntp.org", "time.nist.gov");
  sceneMessage("TIDE", "setting the clock...");
  while (time(nullptr) < 1700000000) delay(200);

  sceneMessage("TIDE", "fetching NOAA predictions...", g_station);
  while (!g_haveData) {
    refresh();
    if (!g_haveData) {
      sceneMessage("NO TIDE DATA", "NOAA fetch failed, retrying", "check the station id on the setup page");
      delay(10000);
    }
  }
  tft.fillScreen(TFT_BLACK);
}

void loop() {
  uint32_t ms = millis();
  if ((int32_t)(ms - g_nextTry) >= 0 && !g_lapse) refresh();

  bool touched = touch.touched();
  if (touched && !g_wasTouched) {
    g_lapse = !g_lapse;
    g_lapseStart = ms;
  }
  g_wasTouched = touched;

  double now = time(nullptr) + (ms % 1000) / 1000.0;
  double t = now;
  if (g_lapse) {
    uint32_t el = ms - g_lapseStart;
    if (el >= LAPSE_MS) g_lapse = false;
    else t = now + (double)el / LAPSE_MS * LAPSE_HOURS * 3600.0;
  }

  if (!tideCovers(g_tide, t)) {
    // Data ran out (fetches failing for days): say so instead of a frozen tide.
    static uint32_t lastMsg = 0;
    if (ms - lastMsg > 5000) {
      sceneMessage("NO TIDE DATA", "predictions expired, retrying", g_station);
      lastMsg = ms;
    }
    g_lastChart = 0;
    g_lapse = false;
    delay(200);
    return;
  }

  sceneDraw(g_tide, t, g_lapse, !g_lastOk, ms);
  static bool wasLapse = false;
  if (g_lapse || wasLapse || ms - g_lastChart > 30000 || g_lastChart == 0) {
    chartDraw(g_tide, t);
    g_lastChart = ms | 1;
  }
  wasLapse = g_lapse;
  static uint32_t lastInfo = 0;
  if (ms - lastInfo > 500) {
    infoDraw(g_tide, t, g_name);
    lastInfo = ms;
  }

  uint32_t spent = millis() - ms;
  if (spent < FRAME_MS) delay(FRAME_MS - spent);
}
