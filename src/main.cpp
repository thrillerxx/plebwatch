#include <M5Unified.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "local_clock.h"
#include "mempool_client.h"
#include "pleb_steps.h"
#include "ui.h"
#include "watch_face.h"
#include "wifi_connect.h"

namespace {

// Power budget (M5StickC Plus2 ~200 mAh) aimed at ~1–3 days untethered:
// - Radio off while the UI is up (Wi‑Fi only for fetch/NTP).
// - Short on-battery glance window; buttons re-up the same window.
// - Sparse IMU micro-wakes (PlebSteps is approximate, not gym-tracker grade).
constexpr uint64_t SLEEP_US_IMU_MICRO = 2ULL * 60ULL * 1000000ULL;  // 2 min
constexpr uint32_t IMU_MICRO_SAMPLE_MS = 2000;
constexpr uint32_t FULL_WAKE_SEC_BATTERY = 60UL * 60UL;       // 60 min
constexpr uint32_t FULL_WAKE_SEC_CHARGING = 3UL * 60UL;       // 3 min
constexpr uint32_t AWAKE_MS_BATTERY = 90UL * 1000UL;         // 90 s glance
constexpr uint32_t AWAKE_MS_CHARGING = 5UL * 60UL * 1000UL;  // 5 min on USB
constexpr uint32_t AWAKE_EXTEND_MS = 15000;
constexpr uint8_t LOW_BRIGHTNESS = 28;
constexpr float PRICE_ALERT_PCT = 2.0f;
constexpr time_t kValidUnixFloor = 1700000000;

constexpr gpio_num_t HOLD_PIN = GPIO_NUM_4;
constexpr gpio_num_t BTN_A_WAKE_PIN = GPIO_NUM_37;

constexpr uint8_t BRIGHTNESS_LEVELS[] = {40, 80, 140, 200, 255};
constexpr size_t BRIGHTNESS_COUNT =
    sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);

RTC_DATA_ATTR uint32_t rtcLastHeight = 0;
RTC_DATA_ATTR float rtcLastPriceUsd = 0;
RTC_DATA_ATTR uint32_t rtcFullWakeAt = 0;  // unix epoch for next Wi‑Fi/UI wake
RTC_DATA_ATTR uint8_t rtcPage = 0;
RTC_DATA_ATTR uint8_t rtcBrightnessIdx = 2;

Preferences prefs;
Metrics gMetrics;
Page gPage = PAGE_WATCH;
uint32_t gAwakeDeadline = 0;
uint8_t gBrightnessIdx = 2;
bool gDimSession = false;

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

void applySessionBrightness() {
  if (gDimSession) {
    M5.Display.setBrightness(LOW_BRIGHTNESS);
  } else {
    applyBrightness();
  }
}

void clearDimSession() {
  if (gDimSession) {
    gDimSession = false;
    applyBrightness();
  }
}

void extendAwake() {
  // Each press restarts the glance window (charging stays generous).
  const uint32_t base = isCharging() ? AWAKE_MS_CHARGING : AWAKE_MS_BATTERY;
  gAwakeDeadline = millis() + max(base, AWAKE_EXTEND_MS);
}

void radioOffForUi() {
  // Holding STA associated during the glance window burns more than the LCD.
  wifiDisconnectFull();
}

time_t unixNow() {
  time_t now = time(nullptr);
  return now;
}

void scheduleNextFullWake() {
  const time_t now = unixNow();
  if (now < kValidUnixFloor) {
    rtcFullWakeAt = 0;
    return;
  }
  const time_t gap =
      isCharging() ? FULL_WAKE_SEC_CHARGING : FULL_WAKE_SEC_BATTERY;
  rtcFullWakeAt = static_cast<uint32_t>(now + gap);
}

void goDeepSleep() {
  M5.Display.setBrightness(0);
  wifiDisconnectFull();
  esp_sleep_enable_ext0_wakeup(BTN_A_WAKE_PIN, 0);

  uint64_t sleepUs = SLEEP_US_IMU_MICRO;
  const time_t now = unixNow();
  if (rtcFullWakeAt == 0 && now >= kValidUnixFloor) {
    scheduleNextFullWake();
  }
  if (now >= kValidUnixFloor && rtcFullWakeAt > static_cast<uint32_t>(now)) {
    const uint64_t untilFull =
        static_cast<uint64_t>(rtcFullWakeAt - static_cast<uint32_t>(now)) *
        1000000ULL;
    if (untilFull < sleepUs) {
      sleepUs = untilFull;
    }
  }
  if (sleepUs < 500000ULL) {
    sleepUs = 500000ULL;
  }

  esp_sleep_enable_timer_wakeup(sleepUs);
  latchPowerHoldForSleep();
  Serial.printf("deep sleep %llu us (full@%lu)\n",
                static_cast<unsigned long long>(sleepUs),
                static_cast<unsigned long>(rtcFullWakeAt));
  Serial.flush();
  esp_deep_sleep_start();
}

void beepNewBlock() {
  M5.Speaker.setVolume(128);
  M5.Speaker.tone(880, 80);
  delay(90);
  M5.Speaker.tone(1175, 120);
}

void beepPriceUp() {
  M5.Speaker.setVolume(128);
  M5.Speaker.tone(784, 70);
  delay(85);
  M5.Speaker.tone(988, 70);
  delay(85);
  M5.Speaker.tone(1175, 110);
}

void beepPriceDown() {
  M5.Speaker.setVolume(128);
  M5.Speaker.tone(1175, 70);
  delay(85);
  M5.Speaker.tone(988, 70);
  delay(85);
  M5.Speaker.tone(784, 110);
}

void beepStepsGoal() {
  M5.Speaker.setVolume(140);
  M5.Speaker.tone(784, 80);
  delay(90);
  M5.Speaker.tone(988, 80);
  delay(90);
  M5.Speaker.tone(1175, 80);
  delay(90);
  M5.Speaker.tone(1568, 160);
}

void maybeAlertPrice(float newPrice) {
  if (newPrice <= 0.0f) {
    return;
  }
  if (rtcLastPriceUsd > 0.0f) {
    const float deltaPct =
        ((newPrice - rtcLastPriceUsd) / rtcLastPriceUsd) * 100.0f;
    if (deltaPct >= PRICE_ALERT_PCT) {
      beepPriceUp();
    } else if (deltaPct <= -PRICE_ALERT_PCT) {
      beepPriceDown();
    }
  }
  rtcLastPriceUsd = newPrice;
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
  applySessionBrightness();
  if (gPage == PAGE_WATCH) {
    watchFaceResetCache();
  }
  uiDrawPage(gPage, gMetrics, batteryPct(), isCharging());
}

bool shouldFullWake(esp_sleep_wakeup_cause_t cause) {
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    return true;
  }
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
    return true;  // cold boot / reset
  }
  const time_t now = unixNow();
  if (rtcFullWakeAt == 0 || now < kValidUnixFloor) {
    return true;
  }
  return now >= static_cast<time_t>(rtcFullWakeAt);
}

void runImuMicroWake() {
  M5.Display.setBrightness(0);
  M5.Imu.update();
  plebStepsBegin();
  plebStepsSampleWindow(IMU_MICRO_SAMPLE_MS);
  if (plebStepsConsumeGoalHit()) {
    // Celebrate quietly next full wake if speaker used now without display —
    // still beep so the user hears the goal while walking.
    beepStepsGoal();
  }
  goDeepSleep();
}

}  // namespace

void setup() {
  assertPowerHold();

  Serial.begin(115200);
  delay(30);

  auto cfg = M5.config();
  cfg.clear_display = true;
  M5.begin(cfg);
  assertPowerHold();

  localClockBegin();
  plebStepsBegin();

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  Serial.printf("plebwatch boot cause=%d\n", static_cast<int>(cause));

  if (!shouldFullWake(cause)) {
    Serial.println("imu micro-wake");
    runImuMicroWake();  // does not return
  }

  M5.Display.wakeup();
  gBrightnessIdx = rtcBrightnessIdx % BRIGHTNESS_COUNT;
  // Timer-driven hourly wake starts dim; button/cold boot uses saved level.
  gDimSession = (cause == ESP_SLEEP_WAKEUP_TIMER);
  applySessionBrightness();
  uiBegin();
  uiBootSplash();

  gPage = static_cast<Page>(rtcPage % PAGE_COUNT);
  loadCachedMetrics();

  uiBootStatus("WiFi", "scanning nearby...");
  char ssid[33] = {};
  const bool wifiOk = wifiConnectKnownNetworks(ssid, sizeof(ssid));
  if (!wifiOk) {
    // Offline glance: RTC clock + last cached metrics, then deep sleep.
    gMetrics.wifiSsid[0] = '\0';
    uiBootStatus("No WiFi", gMetrics.valid ? "using cache" : "watch only");
    delay(900);
  } else {
    strncpy(gMetrics.wifiSsid, ssid, sizeof(gMetrics.wifiSsid) - 1);

    uiBootStatus("Time Zone", "finding time...");
    syncNetworkTime();

    uiBootStatus("Fetching", "stacking blocks...");
    Metrics fresh = gMetrics;
    const bool ok = fetchAllMetrics(fresh);
    uiBootFinishBlocks();
    if (ok) {
      strncpy(fresh.wifiSsid, ssid, sizeof(fresh.wifiSsid) - 1);
      if (rtcLastHeight > 0 && fresh.blockHeight > rtcLastHeight) {
        beepNewBlock();
        delay(120);
      }
      maybeAlertPrice(fresh.priceUsd);
      if (fresh.blockHeight > 0) {
        rtcLastHeight = fresh.blockHeight;
      }
      gMetrics = fresh;
      saveCachedMetrics();
    } else {
      uiBootStatus("Fetch fail", "using cache");
      delay(900);
      strncpy(gMetrics.wifiSsid, ssid, sizeof(gMetrics.wifiSsid) - 1);
    }
  }

  // Drop the radio before the UI glance — biggest easy battery win.
  radioOffForUi();

  scheduleNextFullWake();
  showPage();
  extendAwake();
}

void loop() {
  M5.update();
  assertPowerHold();
  M5.Imu.update();
  plebStepsPoll();

  if (plebStepsConsumeGoalHit()) {
    beepStepsGoal();
  }

  if (M5.BtnA.wasHold()) {
    clearDimSession();
    gPage = PAGE_WATCH;
    rtcPage = static_cast<uint8_t>(gPage);
    showPage();
    extendAwake();
  } else if (M5.BtnA.wasPressed()) {
    clearDimSession();
    gPage = static_cast<Page>((static_cast<uint8_t>(gPage) + 1) % PAGE_COUNT);
    rtcPage = static_cast<uint8_t>(gPage);
    showPage();
    extendAwake();
  }

  if (M5.BtnB.wasPressed()) {
    clearDimSession();
    gBrightnessIdx = (gBrightnessIdx + 1) % BRIGHTNESS_COUNT;
    rtcBrightnessIdx = gBrightnessIdx;
    applyBrightness();
    if (uiIsWatchPage(gPage) || gPage == PAGE_STEPS) {
      showPage();
    }
    extendAwake();
  }

  if (gPage == PAGE_WATCH) {
    watchFaceUpdateClockIfNeeded(gMetrics, batteryPct(), isCharging(),
                                 gMetrics.wifiSsid[0] != '\0');
  } else if (gPage == PAGE_STEPS) {
    static uint32_t lastStepsDrawn = UINT32_MAX;
    const uint32_t steps = plebStepsToday();
    if (steps != lastStepsDrawn) {
      lastStepsDrawn = steps;
      uiDrawPage(gPage, gMetrics, batteryPct(), isCharging());
    }
  } else if (!uiIsWatchPage(gPage)) {
    uiUpdateHeaderClockIfNeeded(batteryPct(), isCharging());
  }

  if (static_cast<int32_t>(millis() - gAwakeDeadline) >= 0) {
    goDeepSleep();
  }

  delay(20);
}
