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
constexpr int BAND_W = 72;  // orange flag band width on splash art

int gLastMinute = -1;
int gLastDay = -1;
int gLastSecond = -1;

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

// Tick contrast against the flag art (white panel needs dark ticks).
uint16_t tickColorAt(int x, int y, int h) {
  if (x > BAND_W && y < h / 2) {
    return C_TRUE_BLACK;
  }
  return C_WHITE;
}

void ellipsePoint(int cx, int cy, float a, float b, float ang, int& x,
                  int& y) {
  x = cx + static_cast<int>(a * cosf(ang));
  y = cy + static_cast<int>(b * sinf(ang));
}

void drawRadialTick(M5GFX& d, int cx, int cy, float a, float b, float ang,
                    float outerScale, float innerScale, int thickness,
                    uint16_t color) {
  int x1, y1, x2, y2;
  ellipsePoint(cx, cy, a * outerScale, b * outerScale, ang, x1, y1);
  ellipsePoint(cx, cy, a * innerScale, b * innerScale, ang, x2, y2);
  for (int t = -(thickness / 2); t <= thickness / 2; ++t) {
    // Offset perpendicular to the radial for thicker ticks.
    const float px = -sinf(ang);
    const float py = cosf(ang);
    const int ox = static_cast<int>(px * t);
    const int oy = static_cast<int>(py * t);
    d.drawLine(x1 + ox, y1 + oy, x2 + ox, y2 + oy, color);
  }
}

void drawSwordHand(M5GFX& d, int cx, int cy, float angle, int length, int width,
                   uint16_t bodyColor, uint16_t edgeColor) {
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float px = -s;
  const float py = c;
  const int tipX = cx + static_cast<int>(length * c);
  const int tipY = cy + static_cast<int>(length * s);
  const int baseX = cx - static_cast<int>(length * 0.14f * c);
  const int baseY = cy - static_cast<int>(length * 0.14f * s);
  const int half = width / 2;
  const int lx = static_cast<int>(px * half);
  const int ly = static_cast<int>(py * half);
  d.fillTriangle(tipX, tipY, baseX + lx, baseY + ly, baseX - lx, baseY - ly,
                 bodyColor);
  // Luminous center stripe (as in the mockup).
  const int tipInX = cx + static_cast<int>((length - 3) * c);
  const int tipInY = cy + static_cast<int>((length - 3) * s);
  d.drawLine(baseX, baseY, tipInX, tipInY, edgeColor);
}

void drawRectDialMarkers(M5GFX& d) {
  const int cx = d.width() / 2;
  const int cy = d.height() / 2;
  const float a = d.width() / 2.0f - 6.0f;   // horizontal radius
  const float b = d.height() / 2.0f - 5.0f;  // vertical radius

  // Minute ticks around the elliptical perimeter.
  for (int i = 0; i < 60; ++i) {
    const float ang = i * (M_PI / 30.0f) - M_PI / 2.0f;
    int ox, oy;
    ellipsePoint(cx, cy, a, b, ang, ox, oy);
    const uint16_t col = tickColorAt(ox, oy, d.height());
    if (i % 15 == 0) {
      // 12 / 3 / 6 / 9 — thick bars
      drawRadialTick(d, cx, cy, a, b, ang, 1.0f, 0.78f, 3, col);
    } else if (i % 5 == 0) {
      // Other hour marks
      drawRadialTick(d, cx, cy, a, b, ang, 1.0f, 0.86f, 2, col);
    } else {
      drawRadialTick(d, cx, cy, a, b, ang, 1.0f, 0.93f, 1, col);
    }
  }
}

void drawHands(M5GFX& d, const struct tm* tmInfo) {
  const int cx = d.width() / 2;
  const int cy = d.height() / 2;
  // Hands stay inside the marker ring.
  const int hourLen = static_cast<int>(d.height() * 0.28f);
  const int minLen = static_cast<int>(d.height() * 0.40f);
  const int secLen = static_cast<int>(d.height() * 0.44f);

  if (!tmInfo) {
    d.fillCircle(cx, cy, 4, C_TRUE_BLACK);
    d.fillCircle(cx, cy, 2, brandOrange());
    return;
  }

  const float hour =
      (tmInfo->tm_hour % 12) + (tmInfo->tm_min / 60.0f) + (tmInfo->tm_sec / 3600.0f);
  const float minute = tmInfo->tm_min + (tmInfo->tm_sec / 60.0f);
  const float second = tmInfo->tm_sec;
  const float hAng = hour * (M_PI / 6.0f) - M_PI / 2.0f;
  const float mAng = minute * (M_PI / 30.0f) - M_PI / 2.0f;
  const float sAng = second * (M_PI / 30.0f) - M_PI / 2.0f;

  // Black sword hands with white center stripe (mockup).
  drawSwordHand(d, cx, cy, hAng, hourLen, 7, C_TRUE_BLACK, C_WHITE);
  drawSwordHand(d, cx, cy, mAng, minLen, 5, C_TRUE_BLACK, C_WHITE);

  // Slim orange seconds hand.
  const int tipX = cx + static_cast<int>(secLen * cosf(sAng));
  const int tipY = cy + static_cast<int>(secLen * sinf(sAng));
  const int tailX = cx - static_cast<int>(secLen * 0.18f * cosf(sAng));
  const int tailY = cy - static_cast<int>(secLen * 0.18f * sinf(sAng));
  d.drawLine(tailX, tailY, tipX, tipY, brandOrange());
  d.drawLine(tailX + 1, tailY, tipX + 1, tipY, brandOrange());

  d.fillCircle(cx, cy, 5, C_TRUE_BLACK);
  d.fillCircle(cx, cy, 2, brandOrange());
}

void drawWatchFace(M5GFX& d, const struct tm* tmInfo) {
  drawSplashBg(d);
  drawRectDialMarkers(d);
  drawHands(d, tmInfo);
}

}  // namespace

void watchFaceResetCache() {
  gLastMinute = -1;
  gLastDay = -1;
  gLastSecond = -1;
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
    gLastSecond = tmInfo.tm_sec;
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
  // Redraw on the second so the orange seconds hand stays alive while awake.
  if (tmInfo.tm_sec == gLastSecond && tmInfo.tm_min == gLastMinute &&
      tmInfo.tm_yday == gLastDay) {
    return;
  }
  auto& d = M5.Display;
  ensureLandscape(d);
  drawWatchFace(d, &tmInfo);
  gLastMinute = tmInfo.tm_min;
  gLastDay = tmInfo.tm_yday;
  gLastSecond = tmInfo.tm_sec;
}

void watchFaceDrawStackMode(uint64_t /*satsBalance*/) {
  auto& d = M5.Display;
  ensureLandscape(d);
  d.fillScreen(C_TRUE_BLACK);

  d.setTextDatum(TC_DATUM);
  d.setFont(&fonts::Font2);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("BASED MODE", d.width() / 2, 12);

  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("1 sat = 1 sat", d.width() / 2, 68);

  d.setFont(&fonts::Font0);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("keep stacking", d.width() / 2, 118);
}
