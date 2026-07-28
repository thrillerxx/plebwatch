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

void dialBounds(M5GFX& d, float& left, float& top, float& right, float& bottom,
                float& cx, float& cy) {
  constexpr float kInset = 1.0f;
  left = kInset;
  top = kInset;
  right = static_cast<float>(d.width() - 1) - kInset;
  bottom = static_cast<float>(d.height() - 1) - kInset;
  cx = (left + right) * 0.5f;
  cy = (top + bottom) * 0.5f;
}

// Clock angle: 0 at 12 o'clock, increasing clockwise (radians).
float clockAngle(float frac01) {
  while (frac01 < 0.0f) {
    frac01 += 1.0f;
  }
  while (frac01 >= 1.0f) {
    frac01 -= 1.0f;
  }
  // Convert to math angle (0 = +x / 3 o'clock, CCW).
  return frac01 * (2.0f * static_cast<float>(M_PI)) - 0.5f * static_cast<float>(M_PI);
}

// Ray from center along unit (ux,uy) → first hit on the rectangle edge.
// Returns inward normal of the hit side.
void rayToRectEdge(float cx, float cy, float ux, float uy, float left,
                   float top, float right, float bottom, float& hx, float& hy,
                   float& nx, float& ny) {
  float bestT = 1.0e9f;
  hx = cx;
  hy = cy;
  nx = 0.0f;
  ny = 0.0f;

  auto consider = [&](float t, float hitNx, float hitNy) {
    if (t > 1.0e-4f && t < bestT) {
      const float x = cx + ux * t;
      const float y = cy + uy * t;
      // Keep hits that land on the segment (with a tiny epsilon).
      if (x < left - 0.51f || x > right + 0.51f || y < top - 0.51f ||
          y > bottom + 0.51f) {
        return;
      }
      bestT = t;
      hx = x;
      hy = y;
      nx = hitNx;
      ny = hitNy;
    }
  };

  if (fabsf(ux) > 1.0e-6f) {
    consider((left - cx) / ux, 1.0f, 0.0f);
    consider((right - cx) / ux, -1.0f, 0.0f);
  }
  if (fabsf(uy) > 1.0e-6f) {
    consider((top - cy) / uy, 0.0f, 1.0f);
    consider((bottom - cy) / uy, 0.0f, -1.0f);
  }

  // Clamp onto the rect in case of float drift.
  if (hx < left) {
    hx = left;
  }
  if (hx > right) {
    hx = right;
  }
  if (hy < top) {
    hy = top;
  }
  if (hy > bottom) {
    hy = bottom;
  }
}

void edgeForFrac(float cx, float cy, float left, float top, float right,
                 float bottom, float frac01, float& hx, float& hy, float& nx,
                 float& ny, float& ux, float& uy) {
  const float ang = clockAngle(frac01);
  ux = cosf(ang);
  uy = sinf(ang);
  rayToRectEdge(cx, cy, ux, uy, left, top, right, bottom, hx, hy, nx, ny);
}

void drawEdgeTick(M5GFX& d, float x, float y, float nx, float ny, int len,
                  int thick, uint16_t color) {
  const int x0 = static_cast<int>(lroundf(x));
  const int y0 = static_cast<int>(lroundf(y));
  const int x1 = static_cast<int>(lroundf(x + nx * len));
  const int y1 = static_cast<int>(lroundf(y + ny * len));
  // Thickness runs along the edge (tangent = perpendicular to inward normal).
  const int tx = static_cast<int>(lroundf(-ny));
  const int ty = static_cast<int>(lroundf(nx));
  const int half = thick / 2;
  for (int t = -half; t <= half; ++t) {
    d.drawLine(x0 + t * tx, y0 + t * ty, x1 + t * tx, y1 + t * ty, color);
  }
}

// Markers share the exact same angles as the hands (ray → rectangle).
void drawRectEdgeMarkers(M5GFX& d) {
  float left, top, right, bottom, cx, cy;
  dialBounds(d, left, top, right, bottom, cx, cy);

  for (int i = 0; i < 60; ++i) {
    const float frac = static_cast<float>(i) / 60.0f;
    float hx, hy, nx, ny, ux, uy;
    edgeForFrac(cx, cy, left, top, right, bottom, frac, hx, hy, nx, ny, ux, uy);
    const uint16_t col =
        tickColorAt(static_cast<int>(lroundf(hx)), static_cast<int>(lroundf(hy)),
                    d.height());

    if (i % 15 == 0) {
      drawEdgeTick(d, hx, hy, nx, ny, 14, 4, col);
    } else if (i % 5 == 0) {
      drawEdgeTick(d, hx, hy, nx, ny, 10, 2, col);
    } else {
      drawEdgeTick(d, hx, hy, nx, ny, 6, 1, col);
    }
  }
}

void drawSwordHand(M5GFX& d, float cx, float cy, float ux, float uy, float length,
                   int width, uint16_t bodyColor, uint16_t slitColor) {
  const float px = -uy;
  const float py = ux;
  const int tipX = static_cast<int>(lroundf(cx + length * ux));
  const int tipY = static_cast<int>(lroundf(cy + length * uy));
  const int baseX = static_cast<int>(lroundf(cx - length * 0.12f * ux));
  const int baseY = static_cast<int>(lroundf(cy - length * 0.12f * uy));
  const float half = width * 0.5f;
  const int lx = static_cast<int>(lroundf(px * half));
  const int ly = static_cast<int>(lroundf(py * half));
  d.fillTriangle(tipX, tipY, baseX + lx, baseY + ly, baseX - lx, baseY - ly,
                 bodyColor);
  const int tipInX = static_cast<int>(lroundf(cx + (length - 4.0f) * ux));
  const int tipInY = static_cast<int>(lroundf(cy + (length - 4.0f) * uy));
  d.drawLine(baseX, baseY, tipInX, tipInY, slitColor);
  if (width >= 5) {
    d.drawLine(baseX + (lx > 0 ? 1 : -1), baseY + (ly > 0 ? 1 : -1), tipInX,
               tipInY, slitColor);
  }
}

void handTowardFrac(float cx, float cy, float left, float top, float right,
                    float bottom, float frac, float reach, float& ux, float& uy,
                    float& length) {
  float hx, hy, nx, ny;
  edgeForFrac(cx, cy, left, top, right, bottom, frac, hx, hy, nx, ny, ux, uy);
  // Aim at the inner tip of the tick so the hand visually locks onto the mark.
  constexpr float kTickInset = 8.0f;
  const float aimX = hx + nx * kTickInset;
  const float aimY = hy + ny * kTickInset;
  float dx = aimX - cx;
  float dy = aimY - cy;
  float dist = sqrtf(dx * dx + dy * dy);
  if (dist < 1.0f) {
    ux = 0.0f;
    uy = -1.0f;
    length = reach;
    return;
  }
  ux = dx / dist;
  uy = dy / dist;
  // Reach most of the way to the marker without covering it.
  length = dist * reach;
}

void drawHands(M5GFX& d, const struct tm* tmInfo) {
  float left, top, right, bottom, cx, cy;
  dialBounds(d, left, top, right, bottom, cx, cy);
  const int icx = static_cast<int>(lroundf(cx));
  const int icy = static_cast<int>(lroundf(cy));

  if (!tmInfo) {
    d.fillCircle(icx, icy, 4, C_TRUE_BLACK);
    d.fillCircle(icx, icy, 2, brandOrange());
    return;
  }

  const float hourFrac =
      ((tmInfo->tm_hour % 12) + (tmInfo->tm_min / 60.0f) +
       (tmInfo->tm_sec / 3600.0f)) /
      12.0f;
  const float minuteFrac =
      (tmInfo->tm_min + (tmInfo->tm_sec / 60.0f)) / 60.0f;
  const float secondFrac = tmInfo->tm_sec / 60.0f;

  float hUx, hUy, hLen;
  float mUx, mUy, mLen;
  float sUx, sUy, sLen;
  handTowardFrac(cx, cy, left, top, right, bottom, hourFrac, 0.55f, hUx, hUy,
                 hLen);
  handTowardFrac(cx, cy, left, top, right, bottom, minuteFrac, 0.78f, mUx, mUy,
                 mLen);
  handTowardFrac(cx, cy, left, top, right, bottom, secondFrac, 0.88f, sUx, sUy,
                 sLen);

  drawSwordHand(d, cx, cy, hUx, hUy, hLen, 7, C_TRUE_BLACK, C_WHITE);
  drawSwordHand(d, cx, cy, mUx, mUy, mLen, 5, C_TRUE_BLACK, C_WHITE);

  const int tipX = static_cast<int>(lroundf(cx + sLen * sUx));
  const int tipY = static_cast<int>(lroundf(cy + sLen * sUy));
  const int tailX = static_cast<int>(lroundf(cx - sLen * 0.16f * sUx));
  const int tailY = static_cast<int>(lroundf(cy - sLen * 0.16f * sUy));
  // Single centered second hand (no +1 bias).
  d.drawLine(tailX, tailY, tipX, tipY, brandOrange());

  d.fillCircle(icx, icy, 5, C_TRUE_BLACK);
  d.fillCircle(icx, icy, 2, brandOrange());
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
