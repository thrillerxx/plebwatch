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
constexpr int BAND_W = 72;  // orange flag band on splash art

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

// Contrast against flag panels (dark on white, light on orange/black).
uint16_t tickColorAt(int x, int y, int h) {
  if (x > BAND_W && y < h / 2) {
    return C_TRUE_BLACK;
  }
  return C_WHITE;
}

// Map perimeter distance (0 at 12 / top-center, clockwise) to edge + inward normal.
void perimeterPoint(int left, int top, int right, int bottom, float dist,
                    int& x, int& y, int& nx, int& ny) {
  const int w = right - left;
  const int h = bottom - top;
  const float peri = 2.0f * (w + h);
  while (dist < 0) {
    dist += peri;
  }
  while (dist >= peri) {
    dist -= peri;
  }

  const float topRight = w * 0.5f;  // top-center → top-right
  const float rightSide = static_cast<float>(h);
  const float bottomSide = static_cast<float>(w);
  const float leftSide = static_cast<float>(h);
  // remaining: top-left → top-center = w/2

  if (dist <= topRight) {
    x = left + w / 2 + static_cast<int>(dist);
    y = top;
    nx = 0;
    ny = 1;
    return;
  }
  dist -= topRight;

  if (dist <= rightSide) {
    x = right;
    y = top + static_cast<int>(dist);
    nx = -1;
    ny = 0;
    return;
  }
  dist -= rightSide;

  if (dist <= bottomSide) {
    x = right - static_cast<int>(dist);
    y = bottom;
    nx = 0;
    ny = -1;
    return;
  }
  dist -= bottomSide;

  if (dist <= leftSide) {
    x = left;
    y = bottom - static_cast<int>(dist);
    nx = 1;
    ny = 0;
    return;
  }
  dist -= leftSide;

  // Top edge: left → center
  x = left + static_cast<int>(dist);
  y = top;
  nx = 0;
  ny = 1;
}

void drawEdgeTick(M5GFX& d, int x, int y, int nx, int ny, int len, int thick,
                  uint16_t color) {
  const int x2 = x + nx * len;
  const int y2 = y + ny * len;
  // Thickness runs along the edge (tangent = perpendicular to inward normal).
  const int tx = -ny;
  const int ty = nx;
  const int half = thick / 2;
  for (int t = -half; t <= half; ++t) {
    d.drawLine(x + t * tx, y + t * ty, x2 + t * tx, y2 + t * ty, color);
  }
}

// Ticks sit ON the rectangular screen edge, spaced evenly around the perimeter
// (matches the rectangular PlebWatch mockup — not a circular/elliptical dial).
void drawRectEdgeMarkers(M5GFX& d) {
  constexpr int kInset = 1;  // flush to the bezel
  const int left = kInset;
  const int top = kInset;
  const int right = d.width() - 1 - kInset;
  const int bottom = d.height() - 1 - kInset;
  const int w = right - left;
  const int h = bottom - top;
  const float peri = 2.0f * (w + h);

  for (int i = 0; i < 60; ++i) {
    const float dist = (static_cast<float>(i) / 60.0f) * peri;
    int x, y, nx, ny;
    perimeterPoint(left, top, right, bottom, dist, x, y, nx, ny);
    const uint16_t col = tickColorAt(x, y, d.height());

    if (i % 15 == 0) {
      // 12 / 3 / 6 / 9 — chunky edge bars
      drawEdgeTick(d, x, y, nx, ny, 14, 4, col);
    } else if (i % 5 == 0) {
      drawEdgeTick(d, x, y, nx, ny, 9, 2, col);
    } else {
      drawEdgeTick(d, x, y, nx, ny, 5, 1, col);
    }
  }
}

void drawSwordHand(M5GFX& d, int cx, int cy, float angle, int length, int width,
                   uint16_t bodyColor, uint16_t slitColor) {
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float px = -s;
  const float py = c;
  const int tipX = cx + static_cast<int>(length * c);
  const int tipY = cy + static_cast<int>(length * s);
  const int baseX = cx - static_cast<int>(length * 0.12f * c);
  const int baseY = cy - static_cast<int>(length * 0.12f * s);
  const int half = width / 2;
  const int lx = static_cast<int>(px * half);
  const int ly = static_cast<int>(py * half);
  d.fillTriangle(tipX, tipY, baseX + lx, baseY + ly, baseX - lx, baseY - ly,
                 bodyColor);
  // Skeleton slit down the middle
  const int tipInX = cx + static_cast<int>((length - 4) * c);
  const int tipInY = cy + static_cast<int>((length - 4) * s);
  d.drawLine(baseX, baseY, tipInX, tipInY, slitColor);
  if (width >= 5) {
    d.drawLine(baseX + (lx > 0 ? 1 : -1), baseY + (ly > 0 ? 1 : -1), tipInX,
               tipInY, slitColor);
  }
}

void drawHands(M5GFX& d, const struct tm* tmInfo) {
  const int cx = d.width() / 2;
  const int cy = d.height() / 2;
  // Reach toward the edge ticks (rect face).
  const int hourLen = static_cast<int>(d.height() * 0.32f);
  const int minLen = static_cast<int>(d.width() * 0.38f);
  const int secLen = static_cast<int>(d.width() * 0.42f);

  if (!tmInfo) {
    d.fillCircle(cx, cy, 4, C_TRUE_BLACK);
    d.fillCircle(cx, cy, 2, brandOrange());
    return;
  }

  const float hour = (tmInfo->tm_hour % 12) + (tmInfo->tm_min / 60.0f) +
                     (tmInfo->tm_sec / 3600.0f);
  const float minute = tmInfo->tm_min + (tmInfo->tm_sec / 60.0f);
  const float second = static_cast<float>(tmInfo->tm_sec);
  const float hAng = hour * (M_PI / 6.0f) - M_PI / 2.0f;
  const float mAng = minute * (M_PI / 30.0f) - M_PI / 2.0f;
  const float sAng = second * (M_PI / 30.0f) - M_PI / 2.0f;

  drawSwordHand(d, cx, cy, hAng, hourLen, 8, C_TRUE_BLACK, C_WHITE);
  drawSwordHand(d, cx, cy, mAng, minLen, 5, C_TRUE_BLACK, C_WHITE);

  const int tipX = cx + static_cast<int>(secLen * cosf(sAng));
  const int tipY = cy + static_cast<int>(secLen * sinf(sAng));
  const int tailX = cx - static_cast<int>(secLen * 0.16f * cosf(sAng));
  const int tailY = cy - static_cast<int>(secLen * 0.16f * sinf(sAng));
  d.drawLine(tailX, tailY, tipX, tipY, brandOrange());
  d.drawLine(tailX + 1, tailY, tipX + 1, tipY, brandOrange());

  d.fillCircle(cx, cy, 5, C_TRUE_BLACK);
  d.fillCircle(cx, cy, 2, brandOrange());
}

void drawWatchFace(M5GFX& d, const struct tm* tmInfo) {
  drawSplashBg(d);
  drawRectEdgeMarkers(d);
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
