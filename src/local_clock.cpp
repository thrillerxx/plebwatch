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

void localClockSetTimezone(const char* posixTz) {
  if (!posixTz || !posixTz[0]) {
    posixTz = PLEBWATCH_TZ;
  }
  applyTz(posixTz);
  persistTz(posixTz);
  Serial.printf("timezone set: %s\n", posixTz);
}

void localClockBegin() {
  const char* tz = PLEBWATCH_TZ;
  if (gRtcTz[0]) {
    tz = gRtcTz;
  } else {
    Preferences prefs;
    if (prefs.begin("clk", true)) {
      String saved = prefs.getString("tz", "");
      prefs.end();
      if (saved.length() > 0) {
        strncpy(gRtcTz, saved.c_str(), sizeof(gRtcTz) - 1);
        gRtcTz[sizeof(gRtcTz) - 1] = '\0';
        tz = gRtcTz;
      }
    }
  }
  applyTz(tz);

  // BM8563 keeps UTC across sleep / power-off; reload ESP32 clock from it.
  if (M5.Rtc.isEnabled()) {
    M5.Rtc.setSystemTimeFromRtc();
    applyTz(tz);  // ensure local TZ after RTC helper's GMT0 dance
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
