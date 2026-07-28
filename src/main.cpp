#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sleep.h>

#include "mempool_client.h"
#include "ui.h"
#include "wifi_connect.h"

namespace {

constexpr uint64_t SLEEP_US_BATTERY = 20ULL * 60ULL * 1000000ULL;  // 20 min
constexpr uint64_t SLEEP_US_CHARGING = 3ULL * 60ULL * 1000000ULL;  // 3 min
constexpr uint32_t AWAKE_MS_BATTERY = 12000;
constexpr uint32_t AWAKE_MS_CHARGING = 45000;
constexpr uint32_t AWAKE_EXTEND_MS = 10000;

RTC_DATA_ATTR uint32_t rtcLastHeight = 0;
RTC_DATA_ATTR uint8_t rtcPage = 0;

Preferences prefs;
Metrics gMetrics;
Page gPage = PAGE_MARKETS;
uint8_t gTopSub = 0;
uint32_t gAwakeDeadline = 0;

bool isCharging() {
  return M5.Power.isCharging();
}

uint8_t batteryPct() {
  int lvl = M5.Power.getBatteryLevel();
  if (lvl < 0) {
    return 0;
  }
  if (lvl > 100) {
    return 100;
  }
  return static_cast<uint8_t>(lvl);
}

void extendAwake() {
  const uint32_t base = isCharging() ? AWAKE_MS_CHARGING : AWAKE_MS_BATTERY;
  gAwakeDeadline = millis() + max(base, AWAKE_EXTEND_MS);
}

void goDeepSleep() {
  uiSleepDisplay();
  wifiDisconnectFull();

  // Wake on Button A (G37) — active low on StickC Plus2
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0);
  const uint64_t sleepUs =
      isCharging() ? SLEEP_US_CHARGING : SLEEP_US_BATTERY;
  esp_sleep_enable_timer_wakeup(sleepUs);

  Serial.flush();
  esp_deep_sleep_start();
}

void beepNewBlock() {
  M5.Speaker.setVolume(128);
  M5.Speaker.tone(880, 80);
  delay(90);
  M5.Speaker.tone(1175, 120);
}

bool loadCachedMetrics() {
  if (!prefs.begin("blkclk", true)) {
    return false;
  }
  const size_t need = sizeof(Metrics);
  if (prefs.getBytesLength("m") != need) {
    prefs.end();
    return false;
  }
  prefs.getBytes("m", &gMetrics, need);
  prefs.end();
  return gMetrics.valid;
}

void saveCachedMetrics() {
  if (!prefs.begin("blkclk", false)) {
    return;
  }
  prefs.putBytes("m", &gMetrics, sizeof(Metrics));
  prefs.end();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);
  M5.Power.begin();
  // Plus2: keep power hold so it stays alive off USB
  M5.Power.setLed(0);

  Serial.begin(115200);
  uiBegin();

  gPage = static_cast<Page>(rtcPage % PAGE_COUNT);
  loadCachedMetrics();

  uiShowStatus("WiFi...", "YourHomeWifi / YourHackerspaceWifi");
  char ssid[33] = {};
  const bool wifiOk = wifiConnectKnownNetworks(ssid, sizeof(ssid));
  if (!wifiOk) {
    uiShowStatus("No WiFi", "sleeping...");
    delay(1500);
    if (gMetrics.valid) {
      uiDrawPage(gPage, gMetrics, batteryPct(), isCharging(), gTopSub);
      delay(2500);
    }
    goDeepSleep();
  }

  strncpy(gMetrics.wifiSsid, ssid, sizeof(gMetrics.wifiSsid) - 1);
  uiShowStatus("Fetching...", ssid);

  Metrics fresh = gMetrics;
  const bool ok = fetchAllMetrics(fresh);
  if (ok) {
    strncpy(fresh.wifiSsid, ssid, sizeof(fresh.wifiSsid) - 1);
    if (rtcLastHeight > 0 && fresh.blockHeight > rtcLastHeight) {
      beepNewBlock();
    }
    if (fresh.blockHeight > 0) {
      rtcLastHeight = fresh.blockHeight;
    }
    gMetrics = fresh;
    saveCachedMetrics();
  } else {
    uiShowStatus("Fetch fail", "using cache");
    delay(800);
    strncpy(gMetrics.wifiSsid, ssid, sizeof(gMetrics.wifiSsid) - 1);
  }

  uiDrawPage(gPage, gMetrics, batteryPct(), isCharging(), gTopSub);
  extendAwake();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    gPage = static_cast<Page>((static_cast<uint8_t>(gPage) + 1) % PAGE_COUNT);
    rtcPage = static_cast<uint8_t>(gPage);
    gTopSub = 0;
    uiDrawPage(gPage, gMetrics, batteryPct(), isCharging(), gTopSub);
    extendAwake();
  }

  if (M5.BtnB.wasPressed()) {
    if (gPage == PAGE_TOP_NODES) {
      gTopSub ^= 1;
    } else {
      // Jump to top nodes / toggle later
      gPage = PAGE_TOP_NODES;
      rtcPage = static_cast<uint8_t>(gPage);
    }
    uiDrawPage(gPage, gMetrics, batteryPct(), isCharging(), gTopSub);
    extendAwake();
  }

  // Power button short press: sleep now
  if (M5.BtnPWR.wasClicked()) {
    goDeepSleep();
  }

  if (static_cast<int32_t>(millis() - gAwakeDeadline) >= 0) {
    goDeepSleep();
  }

  delay(20);
}
