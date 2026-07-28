#include "watch_face.h"

#include <M5Unified.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "local_clock.h"
#include "splash_image.h"

namespace {

constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_TRUE_BLACK = 0x0000;

int gLastMinute = -1;
int gLastDay = -1;

uint16_t brandOrange() {
#ifdef PLEBWATCH_ORANGE_RGB565
  return PLEBWATCH_ORANGE_RGB565;
#else
  return 0xF542;  // #F6A916
#endif
}

void ensureLandscape(M5GFX& d) {
  if (d.getRotation() != 1) {
    d.setRotation(1);
  }
}

void drawSplashBg(M5GFX& d) {
  d.setSwapBytes(true);
  d.pushImage(0, 0, SPLASH_W, SPLASH_H, SPLASH_RGB565);
  d.setSwapBytes(false);
}

void drawHand(M5GFX& d, int cx, int cy, float angle, int length, int width,
              uint16_t color) {
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float px = -s;  // perpendicular
  const float py = c;
  const int tipX = cx + static_cast<int>(length * c);
  const int tipY = cy + static_cast<int>(length * s);
  const int baseX = cx - static_cast<int>(length * 0.12f * c);
  const int baseY = cy - static_cast<int>(length * 0.12f * s);
  const int half = width / 2;
  const int lx = static_cast<int>(px * half);
  const int ly = static_cast<int>(py * half);
  d.fillTriangle(tipX, tipY, baseX + lx, baseY + ly, baseX - lx, baseY - ly,
                 color);
}

void drawAnalogClock(M5GFX& d, const struct tm* tmInfo) {
  const int cx = d.width() / 2;
  const int cy = d.height() / 2;
  const int r = 56;

  // Dial sits on top of the splash logo; logo peeks around the rim.
  d.fillCircle(cx, cy, r, C_TRUE_BLACK);
  d.drawCircle(cx, cy, r, brandOrange());
  d.drawCircle(cx, cy, r - 1, brandOrange());
  d.drawCircle(cx, cy, r - 3, 0x4208);  // soft inner ring

  // 12 hour markers; quarters thicker + orange.
  for (int i = 0; i < 12; ++i) {
    const float ang = i * (M_PI / 6.0f) - M_PI / 2.0f;
    const bool quarter = (i % 3) == 0;
    const int outer = r - 5;
    const int inner = quarter ? r - 16 : r - 11;
    const int x1 = cx + static_cast<int>(outer * cosf(ang));
    const int y1 = cy + static_cast<int>(outer * sinf(ang));
    const int x2 = cx + static_cast<int>(inner * cosf(ang));
    const int y2 = cy + static_cast<int>(inner * sinf(ang));
    const uint16_t col = quarter ? brandOrange() : C_WHITE;
    if (quarter) {
      d.drawLine(x1, y1, x2, y2, col);
      d.drawLine(x1 + 1, y1, x2 + 1, y2, col);
      d.drawLine(x1, y1 + 1, x2, y2 + 1, col);
    } else {
      d.drawLine(x1, y1, x2, y2, col);
    }
  }

  // Tiny minute ticks for readability.
  for (int i = 0; i < 60; ++i) {
    if (i % 5 == 0) {
      continue;
    }
    const float ang = i * (M_PI / 30.0f) - M_PI / 2.0f;
    const int outer = r - 5;
    const int inner = r - 8;
    d.drawLine(cx + static_cast<int>(outer * cosf(ang)),
               cy + static_cast<int>(outer * sinf(ang)),
               cx + static_cast<int>(inner * cosf(ang)),
               cy + static_cast<int>(inner * sinf(ang)), 0x8410);
  }

  if (!tmInfo) {
    d.fillCircle(cx, cy, 3, brandOrange());
    return;
  }

  const float hour =
      (tmInfo->tm_hour % 12) + (tmInfo->tm_min / 60.0f);
  const float minute = tmInfo->tm_min + (tmInfo->tm_sec / 60.0f);
  const float hAng = hour * (M_PI / 6.0f) - M_PI / 2.0f;
  const float mAng = minute * (M_PI / 30.0f) - M_PI / 2.0f;

  drawHand(d, cx, cy, hAng, r - 24, 7, brandOrange());
  drawHand(d, cx, cy, mAng, r - 12, 4, C_WHITE);

  d.fillCircle(cx, cy, 4, brandOrange());
  d.fillCircle(cx, cy, 2, C_WHITE);
}

void drawWatchFace(M5GFX& d, const struct tm* tmInfo) {
  drawSplashBg(d);
  drawAnalogClock(d, tmInfo);
}

}  // namespace

void watchFaceResetCache() {
  gLastMinute = -1;
  gLastDay = -1;
}

void watchFaceDrawFull(const Metrics& /*m*/, uint8_t /*batteryPct*/,
                       bool /*charging*/, bool /*wifiConnected*/) {
  auto& d = M5.Display;
  ensureLandscape(d);
  if (d.getBrightness() < 40) {
    d.setBrightness(180);
  }

  struct tm tmInfo = {};
  if (localClockReadTm(&tmInfo)) {
    drawWatchFace(d, &tmInfo);
    gLastMinute = tmInfo.tm_min;
    gLastDay = tmInfo.tm_yday;
  } else {
    drawWatchFace(d, nullptr);
  }
}

void watchFaceUpdateClockIfNeeded(const Metrics& /*m*/, uint8_t /*batteryPct*/,
                                  bool /*charging*/, bool /*wifiConnected*/) {
  struct tm tmInfo = {};
  if (!localClockReadTm(&tmInfo)) {
    return;
  }
  if (tmInfo.tm_min == gLastMinute && tmInfo.tm_yday == gLastDay) {
    return;
  }
  auto& d = M5.Display;
  ensureLandscape(d);
  drawWatchFace(d, &tmInfo);
  gLastMinute = tmInfo.tm_min;
  gLastDay = tmInfo.tm_yday;
}

void watchFaceDrawStackMode(uint64_t /*satsBalance*/) {
  auto& d = M5.Display;
  ensureLandscape(d);
  d.fillScreen(C_TRUE_BLACK);

  d.setTextDatum(TC_DATUM);
  d.setFont(&fonts::Font2);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("STACK MODE", d.width() / 2, 12);

  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("1 sat = 1 sat", d.width() / 2, 68);

  d.setFont(&fonts::Font0);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("keep stacking", d.width() / 2, 118);
}
