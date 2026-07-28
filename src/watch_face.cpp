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
  const float w = static_cast<float>(right - left);
  const float h = static_cast<float>(bottom - top);
  const float peri = 2.0f * (w + h);
  while (dist < 0.0f) {
    dist += peri;
  }
  while (dist >= peri) {
    dist -= peri;
  }

  const float cx = (left + right) * 0.5f;
  const float topRight = w * 0.5f;  // top-center → top-right
  const float rightSide = h;
  const float bottomSide = w;
  const float leftSide = h;

  auto roundi = [](float v) { return static_cast<int>(lroundf(v)); };

  if (dist <= topRight) {
    x = roundi(cx + dist);
    y = top;
    nx = 0;
    ny = 1;
    return;
  }
  dist -= topRight;

  if (dist <= rightSide) {
    x = right;
    y = roundi(static_cast<float>(top) + dist);
    nx = -1;
    ny = 0;
    return;
  }
  dist -= rightSide;

  if (dist <= bottomSide) {
    x = roundi(static_cast<float>(right) - dist);
    y = bottom;
    nx = 0;
    ny = -1;
    return;
  }
  dist -= bottomSide;

  if (dist <= leftSide) {
    x = left;
    y = roundi(static_cast<float>(bottom) - dist);
    nx = 1;
    ny = 0;
    return;
  }
  dist -= leftSide;

  // Top edge: left → center
  x = roundi(static_cast<float>(left) + dist);
  y = top;
  nx = 0;
  ny = 1;
}

// Fraction around the dial: 0 = 12, 0.25 = 3, 0.5 = 6, 0.75 = 9 (clockwise).
void dialTarget(int left, int top, int right, int bottom, float frac, int cx,
                int cy, int length, float& angle, int& tipX, int& tipY) {
  while (frac < 0.0f) {
    frac += 1.0f;
  }
  while (frac >= 1.0f) {
    frac -= 1.0f;
  }
  const float peri =
      2.0f * static_cast<float>((right - left) + (bottom - top));
  int px = 0;
  int py = 0;
  int nx = 0;
  int ny = 0;
  perimeterPoint(left, top, right, bottom, frac * peri, px, py, nx, ny);

  float dx = static_cast<float>(px - cx);
  float dy = static_cast<float>(py - cy);
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) {
    angle = -M_PI / 2.0f;
    tipX = cx;
    tipY = cy - length;
    return;
  }
  dx /= len;
  dy /= len;
  angle = atan2f(dy, dx);
  tipX = cx + static_cast<int>(lroundf(dx * length));
  tipY = cy + static_cast<int>(lroundf(dy * length));
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

void dialBounds(M5GFX& d, int& left, int& top, int& right, int& bottom) {
  constexpr int kInset = 1;  // flush to the bezel
  left = kInset;
  top = kInset;
  right = d.width() - 1 - kInset;
  bottom = d.height() - 1 - kInset;
}

// Ticks sit ON the rectangular screen edge, spaced evenly around the perimeter
// (matches the rectangular PlebWatch mockup — not a circular/elliptical dial).
void drawRectEdgeMarkers(M5GFX& d) {
  int left, top, right, bottom;
  dialBounds(d, left, top, right, bottom);
  const float peri =
      2.0f * static_cast<float>((right - left) + (bottom - top));

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
  const int tipX = cx + static_cast<int>(lroundf(length * c));
  const int tipY = cy + static_cast<int>(lroundf(length * s));
  const int baseX = cx - static_cast<int>(lroundf(length * 0.12f * c));
  const int baseY = cy - static_cast<int>(lroundf(length * 0.12f * s));
  const int half = width / 2;
  const int lx = static_cast<int>(lroundf(px * half));
  const int ly = static_cast<int>(lroundf(py * half));
  d.fillTriangle(tipX, tipY, baseX + lx, baseY + ly, baseX - lx, baseY - ly,
                 bodyColor);
  // Skeleton slit down the middle
  const int tipInX = cx + static_cast<int>(lroundf((length - 4) * c));
  const int tipInY = cy + static_cast<int>(lroundf((length - 4) * s));
  d.drawLine(baseX, baseY, tipInX, tipInY, slitColor);
  if (width >= 5) {
    d.drawLine(baseX + (lx > 0 ? 1 : -1), baseY + (ly > 0 ? 1 : -1), tipInX,
               tipInY, slitColor);
  }
}

void drawHands(M5GFX& d, const struct tm* tmInfo) {
  int left, top, right, bottom;
  dialBounds(d, left, top, right, bottom);
  // Same geometric center the perimeter ticks are measured from.
  const int cx = static_cast<int>(lroundf((left + right) * 0.5f));
  const int cy = static_cast<int>(lroundf((top + bottom) * 0.5f));
  // Reach toward the edge ticks (rect face).
  const int hourLen = static_cast<int>(d.height() * 0.32f);
  const int minLen = static_cast<int>(d.width() * 0.38f);
  const int secLen = static_cast<int>(d.width() * 0.42f);

  if (!tmInfo) {
    d.fillCircle(cx, cy, 4, C_TRUE_BLACK);
    d.fillCircle(cx, cy, 2, brandOrange());
    return;
  }

  // Map time onto the same perimeter fractions as the 60 edge markers.
  const float hourFrac =
      ((tmInfo->tm_hour % 12) + (tmInfo->tm_min / 60.0f) +
       (tmInfo->tm_sec / 3600.0f)) /
      12.0f;
  const float minuteFrac =
      (tmInfo->tm_min + (tmInfo->tm_sec / 60.0f)) / 60.0f;
  const float secondFrac = tmInfo->tm_sec / 60.0f;

  float hAng = 0;
  float mAng = 0;
  float sAng = 0;
  int unusedTipX = 0;
  int unusedTipY = 0;
  dialTarget(left, top, right, bottom, hourFrac, cx, cy, hourLen, hAng,
             unusedTipX, unusedTipY);
  dialTarget(left, top, right, bottom, minuteFrac, cx, cy, minLen, mAng,
             unusedTipX, unusedTipY);

  drawSwordHand(d, cx, cy, hAng, hourLen, 8, C_TRUE_BLACK, C_WHITE);
  drawSwordHand(d, cx, cy, mAng, minLen, 5, C_TRUE_BLACK, C_WHITE);

  int tipX = 0;
  int tipY = 0;
  dialTarget(left, top, right, bottom, secondFrac, cx, cy, secLen, sAng, tipX,
             tipY);
  const int tailX =
      cx - static_cast<int>(lroundf(secLen * 0.16f * cosf(sAng)));
  const int tailY =
      cy - static_cast<int>(lroundf(secLen * 0.16f * sinf(sAng)));
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
