#include "pleb_steps.h"

#include <M5Unified.h>
#include <math.h>

#include "local_clock.h"

namespace {

constexpr float kStepThresholdG = 1.18f;
constexpr float kStepResetG = 1.05f;
constexpr uint32_t kMinStepGapMs = 280;

RTC_DATA_ATTR uint32_t rtcStepsToday = 0;
RTC_DATA_ATTR uint32_t rtcStepsDayKey = 0;
RTC_DATA_ATTR uint8_t rtcStepArmed = 1;
RTC_DATA_ATTR uint8_t rtcGoalHitPending = 0;
RTC_DATA_ATTR uint8_t rtcGoalCelebrated = 0;
RTC_DATA_ATTR uint32_t rtcLastStepMs = 0;

uint32_t dayKeyNow() {
  struct tm tmInfo = {};
  if (!localClockReadTm(&tmInfo)) {
    return 0;
  }
  return static_cast<uint32_t>(tmInfo.tm_year + 1900) * 10000u +
         static_cast<uint32_t>(tmInfo.tm_mon + 1) * 100u +
         static_cast<uint32_t>(tmInfo.tm_mday);
}

void rollDayIfNeeded() {
  const uint32_t today = dayKeyNow();
  if (today == 0) {
    return;
  }
  if (rtcStepsDayKey != today) {
    rtcStepsDayKey = today;
    rtcStepsToday = 0;
    rtcStepArmed = 1;
    rtcGoalHitPending = 0;
    rtcGoalCelebrated = 0;
    rtcLastStepMs = 0;
  }
}

void noteStep() {
  const uint32_t before = rtcStepsToday;
  ++rtcStepsToday;
  if (!rtcGoalCelebrated && before < PLEB_STEPS_GOAL &&
      rtcStepsToday >= PLEB_STEPS_GOAL) {
    rtcGoalHitPending = 1;
    rtcGoalCelebrated = 1;
  }
}

void considerSample(float magG, uint32_t nowMs) {
  if (magG >= kStepThresholdG && rtcStepArmed) {
    if (rtcLastStepMs == 0 ||
        static_cast<uint32_t>(nowMs - rtcLastStepMs) >= kMinStepGapMs) {
      noteStep();
      rtcLastStepMs = nowMs;
      rtcStepArmed = 0;
    }
  } else if (magG < kStepResetG) {
    rtcStepArmed = 1;
  }
}

bool readMagG(float* out) {
  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.isEnabled() || !M5.Imu.getAccel(&ax, &ay, &az)) {
    return false;
  }
  *out = sqrtf(ax * ax + ay * ay + az * az);
  return true;
}

}  // namespace

void plebStepsBegin() {
  rollDayIfNeeded();
}

void plebStepsPoll() {
  rollDayIfNeeded();
  float mag = 0;
  if (!readMagG(&mag)) {
    return;
  }
  considerSample(mag, millis());
}

void plebStepsSampleWindow(uint32_t windowMs) {
  rollDayIfNeeded();
  if (!M5.Imu.isEnabled()) {
    return;
  }
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < windowMs) {
    M5.Imu.update();
    float mag = 0;
    if (readMagG(&mag)) {
      considerSample(mag, millis());
    }
    delay(20);
  }
}

uint32_t plebStepsToday() {
  rollDayIfNeeded();
  return rtcStepsToday;
}

uint32_t plebStepsGoal() { return PLEB_STEPS_GOAL; }

uint32_t plebStepsDayKey() {
  rollDayIfNeeded();
  return rtcStepsDayKey;
}

bool plebStepsConsumeGoalHit() {
  if (!rtcGoalHitPending) {
    return false;
  }
  rtcGoalHitPending = 0;
  return true;
}
