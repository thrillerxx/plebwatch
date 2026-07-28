#include <M5Unified.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <time.h>

#include "config.h"
#include "mempool_client.h"
#include "ui.h"
#include "watch_face.h"
#include "wifi_connect.h"

namespace {

constexpr uint64_t SLEEP_US_BATTERY = 20ULL * 60ULL * 1000000ULL;  // 20 min
constexpr uint64_t SLEEP_US_CHARGING = 3ULL * 60ULL * 1000000ULL;  // 3 min
constexpr uint32_t AWAKE_MS_BATTERY = 20000;
constexpr uint32_t AWAKE_MS_CHARGING = 60000;
constexpr uint32_t AWAKE_EXTEND_MS = 15000;

constexpr gpio_num_t HOLD_PIN = GPIO_NUM_4;

constexpr uint8_t BRIGHTNESS_LEVELS[] = {40, 80, 140, 200, 255};
constexpr size_t BRIGHTNESS_COUNT =
    sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

RTC_DATA_ATTR uint32_t rtcLastHeight = 0;
RTC_DATA_ATTR uint8_t rtcPage = 0;
RTC_DATA_ATTR uint8_t rtcBrightnessIdx = 2;

Preferences prefs;
Metrics gMetrics;
Page gPage = PAGE_WATCH;
uint8_t gTopSub = 0;
uint32_t gAwakeDeadline = 0;
uint8_t gBrightnessIdx = 2;

void releaseHoldLatch() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(HOLD_PIN);
}

void assertPowerHold() {
  releaseHoldLatch();
  pinMode(HOLD_PIN, OUTPUT);
  digitalWrite(HOLD_PIN, HIGH);
}

void latchPowerHoldForSleep() {
  digitalWrite(HOLD_PIN, HIGH);
  gpio_hold_en(HOLD_PIN);
  gpio_deep_sleep_hold_en();
}

bool isCharging() { return M5.Power.isCharging(); }

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

void applyBrightness() {
  gBrightnessIdx = gBrightnessIdx % BRIGHTNESS_COUNT;
  M5.Display.setBrightness(BRIGHTNESS_LEVELS[gBrightnessIdx]);
}

void extendAwake() {
  const uint32_t base = isCharging() ? AWAKE_MS_CHARGING : AWAKE_MS_BATTERY;
  gAwakeDeadline = millis() + max(base, AWAKE_EXTEND_MS);
}

void goDeepSleep() {
  M5.Display.setBrightness(0);
  wifiDisconnectFull();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0);
  const uint64_t sleepUs =
      isCharging() ? SLEEP_US_CHARGING : SLEEP_US_BATTERY;
  esp_sleep_enable_timer_wakeup(sleepUs);
  latchPowerHoldForSleep();
  Serial.println("deep sleep");
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

void showPage() {
  M5.Display.wakeup();
  applyBrightness();
  if (gPage == PAGE_WATCH) {
    watchFaceResetCache();
  }
  uiDrawPage(gPage, gMetrics, batteryPct(), isCharging(), gTopSub);
}

}  // namespace

void setup() {
  assertPowerHold();

  Serial.begin(115200);
  delay(30);
  Serial.println("plebwatch boot");

  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);
  assertPowerHold();

  M5.Display.wakeup();
  setenv("TZ", PLEBWATCH_TZ, 1);
  tzset();
  gBrightnessIdx = rtcBrightnessIdx % BRIGHTNESS_COUNT;
  applyBrightness();
  uiBegin();
  uiBootSplash();

  gPage = static_cast<Page>(rtcPage % PAGE_COUNT);
  loadCachedMetrics();

  uiShowStatus("WiFi...", "joining...");
  char ssid[33] = {};
  const bool wifiOk = wifiConnectKnownNetworks(ssid, sizeof(ssid));
  if (!wifiOk) {
    uiShowStatus("No WiFi", "retry later");
    delay(3000);
    if (gMetrics.valid || uiIsWatchPage(gPage)) {
      showPage();
      delay(4000);
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
    delay(1200);
    strncpy(gMetrics.wifiSsid, ssid, sizeof(gMetrics.wifiSsid) - 1);
  }

  showPage();
  extendAwake();
}

void loop() {
  M5.update();
  assertPowerHold();

  // Long-press A jumps back to main watch face
  if (M5.BtnA.wasHold()) {
    gPage = PAGE_WATCH;
    rtcPage = static_cast<uint8_t>(gPage);
    gTopSub = 0;
    showPage();
    extendAwake();
  } else if (M5.BtnA.wasPressed()) {
    gPage = static_cast<Page>((static_cast<uint8_t>(gPage) + 1) % PAGE_COUNT);
    rtcPage = static_cast<uint8_t>(gPage);
    gTopSub = 0;
    showPage();
    extendAwake();
  }

  if (M5.BtnB.wasPressed()) {
    if (gPage == PAGE_TOP_NODES) {
      gTopSub ^= 1;
      showPage();
    } else {
      // Cycle brightness on watch / dashboard pages
      gBrightnessIdx = (gBrightnessIdx + 1) % BRIGHTNESS_COUNT;
      rtcBrightnessIdx = gBrightnessIdx;
      applyBrightness();
      if (uiIsWatchPage(gPage)) {
        showPage();
      }
    }
    extendAwake();
  }

  // On watch face, only refresh the clock digits when the minute changes
  if (gPage == PAGE_WATCH) {
    watchFaceUpdateClockIfNeeded(gMetrics, batteryPct(), isCharging(),
                                 gMetrics.wifiSsid[0] != '\0');
  }

  if (static_cast<int32_t>(millis() - gAwakeDeadline) >= 0) {
    goDeepSleep();
  }

  delay(20);
}
