#include "local_clock.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <string.h>
#include <sys/time.h>

#include "config.h"

namespace {

// Survives ESP32 deep sleep while HOLD keeps the chip powered.
RTC_DATA_ATTR char gRtcTz[48] = {};

void applyTz(const char* posixTz) {
  if (!posixTz || !posixTz[0]) {
    posixTz = PLEBWATCH_TZ;
  }
  setenv("TZ", posixTz, 1);
  tzset();
}

void persistTz(const char* posixTz) {
  strncpy(gRtcTz, posixTz, sizeof(gRtcTz) - 1);
  gRtcTz[sizeof(gRtcTz) - 1] = '\0';

  Preferences prefs;
  if (prefs.begin("clk", false)) {
    prefs.putString("tz", posixTz);
    prefs.end();
  }
}

}  // namespace

bool localClockIsUtcTz(const char* posixTz) {
  if (!posixTz || !posixTz[0]) {
    return true;
  }
  // Common UTC / zero-offset forms we refuse for display.
  if (!strcmp(posixTz, "UTC0") || !strcmp(posixTz, "UTC") ||
      !strcmp(posixTz, "GMT0") || !strcmp(posixTz, "GMT") ||
      !strcmp(posixTz, "<+00>0") || !strcmp(posixTz, "UTC+0") ||
      !strcmp(posixTz, "Etc/UTC") || !strcmp(posixTz, "Zulu")) {
    return true;
  }
  return false;
}

bool localClockSetTimezone(const char* posixTz, const char* fallbackTz) {
  const char* fb = (fallbackTz && fallbackTz[0]) ? fallbackTz : PLEBWATCH_TZ;
  char tmp[48];
  if (!posixTz || !posixTz[0] || localClockIsUtcTz(posixTz)) {
    strncpy(tmp, fb, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    applyTz(tmp);
    persistTz(tmp);
    Serial.printf("timezone rejected UTC/empty — using %s\n", tmp);
    return false;
  }
  strncpy(tmp, posixTz, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  applyTz(tmp);
  persistTz(tmp);
  Serial.printf("timezone set: %s\n", tmp);
  return true;
}

const char* localClockTz() {
  if (gRtcTz[0] && !localClockIsUtcTz(gRtcTz)) {
    return gRtcTz;
  }
  return PLEBWATCH_TZ;
}

void localClockBegin() {
  const char* tz = PLEBWATCH_TZ;
  if (gRtcTz[0] && !localClockIsUtcTz(gRtcTz)) {
    tz = gRtcTz;
  } else {
    Preferences prefs;
    if (prefs.begin("clk", true)) {
      String saved = prefs.getString("tz", "");
      prefs.end();
      if (saved.length() > 0 && !localClockIsUtcTz(saved.c_str())) {
        strncpy(gRtcTz, saved.c_str(), sizeof(gRtcTz) - 1);
        gRtcTz[sizeof(gRtcTz) - 1] = '\0';
        tz = gRtcTz;
      }
    }
  }
  applyTz(tz);

  // BM8563 stores UTC; reload ESP32 clock, then keep local TZ for display.
  if (M5.Rtc.isEnabled()) {
    M5.Rtc.setSystemTimeFromRtc();
    applyTz(tz);
    Serial.println("clock restored from hardware RTC");
  }
}

bool localClockSetUnixUtc(time_t unixUtc) {
  if (unixUtc < 1700000000) {
    return false;
  }
  timeval tv = {};
  tv.tv_sec = unixUtc;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  return true;
}

void localClockCommitRtc() {
  time_t t = time(nullptr);
  if (t < 1700000000 || !M5.Rtc.isEnabled()) {
    return;
  }
  // Align to the next whole second, then store UTC (M5 recommended).
  t += 1;
  while (t > time(nullptr)) {
    delay(1);
  }
  M5.Rtc.setDateTime(gmtime(&t));
  Serial.println("clock saved to hardware RTC");
}

bool localClockReadTm(struct tm* out) {
  if (!out) {
    return false;
  }
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }
  localtime_r(&now, out);
  return true;
}

void localClockFormatHm(char* buf, size_t len) {
  if (!buf || len == 0) {
    return;
  }
  struct tm tmInfo = {};
  if (!localClockReadTm(&tmInfo)) {
    snprintf(buf, len, "--:--");
    return;
  }
#if defined(PLEBWATCH_12HOUR) && PLEBWATCH_12HOUR
  int hour = tmInfo.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(buf, len, "%d:%02d", hour, tmInfo.tm_min);
#else
  strftime(buf, len, "%H:%M", &tmInfo);
#endif
}
