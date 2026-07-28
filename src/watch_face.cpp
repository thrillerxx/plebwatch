#include "watch_face.h"

#include <M5Unified.h>
#include <math.h>
#include <time.h>

#include "config.h"

namespace {

constexpr uint16_t C_BLACK = 0x18C3;  // #231F20
constexpr uint16_t C_WHITE = 0xF7BE;  // #F5F5F5
constexpr uint16_t C_TRUE_BLACK = 0x0000;

// Landscape flag proportions (240x135): left band ~1/3
constexpr int BAND_W = 72;

int gLastMinute = -1;
int gLastDay = -1;

uint16_t brandOrange() {
#ifdef PLEBWATCH_ORANGE_RGB565
  return PLEBWATCH_ORANGE_RGB565;
#else
  return 0xF542;  // #F6A916
#endif
}

void fillStar(M5GFX& d, int cx, int cy, int outerR, int innerR,
              uint16_t color) {
  float verts[10][2];
  for (int i = 0; i < 10; ++i) {
    const float ang = -M_PI / 2.0f + i * (M_PI / 5.0f);
    const float r = (i % 2 == 0) ? outerR : innerR;
    verts[i][0] = cx + r * cosf(ang);
    verts[i][1] = cy + r * sinf(ang);
  }
  for (int i = 0; i < 10; ++i) {
    const int j = (i + 1) % 10;
    d.fillTriangle(cx, cy, static_cast<int>(verts[i][0]),
                   static_cast<int>(verts[i][1]), static_cast<int>(verts[j][0]),
                   static_cast<int>(verts[j][1]), color);
  }
}

bool readLocalTm(struct tm& out) {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return false;
  }
  localtime_r(&now, &out);
  return true;
}

void ensureLandscape(M5GFX& d) {
  if (d.getRotation() != 1) {
    d.setRotation(1);
  }
}

void drawChrome(M5GFX& d) {
  const int w = d.width();   // 240
  const int h = d.height();  // 135
  d.fillRect(0, 0, BAND_W, h, brandOrange());
  d.fillRect(BAND_W, 0, w - BAND_W, h / 2, C_WHITE);
  d.fillRect(BAND_W, h / 2, w - BAND_W, h - h / 2, C_BLACK);
  fillStar(d, BAND_W / 2, h / 2, 18, 7, C_WHITE);
}

void drawWordmark(M5GFX& d) {
  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(C_TRUE_BLACK, C_WHITE);
  d.drawString("PlebWatch", BAND_W + (d.width() - BAND_W) / 2, d.height() / 4);
}

void drawStatusRow(M5GFX& d, uint8_t batteryPct, bool charging, bool wifiOk) {
  d.setFont(&fonts::Font0);
  d.setTextDatum(top_left);
  d.setTextColor(brandOrange(), C_BLACK);
  char row[32];
  snprintf(row, sizeof(row), "%s  %d%%%s", wifiOk ? "WiFi" : "noWiFi",
           batteryPct, charging ? "*" : "");
  d.drawString(row, BAND_W + 6, d.height() / 2 + 4);
}

void drawClockBlock(M5GFX& d, const struct tm& tmInfo, bool clearBg) {
  const int cx = BAND_W + (d.width() - BAND_W) / 2;
  const int midY = d.height() / 2;
  if (clearBg) {
    d.fillRect(BAND_W, midY + 18, d.width() - BAND_W, 36, C_BLACK);
  }

  char timeBuf[8];
#if defined(PLEBWATCH_12HOUR) && PLEBWATCH_12HOUR
  int hour = tmInfo.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour, tmInfo.tm_min);
#else
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmInfo);
#endif

  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold18pt7b);
  d.setTextColor(C_WHITE, C_BLACK);
  d.drawString(timeBuf, cx, midY + 28);

  char dateBuf[16];
  strftime(dateBuf, sizeof(dateBuf), "%a %b %d", &tmInfo);
  d.setFont(&fonts::Font0);
  d.setTextColor(C_WHITE, C_BLACK);
  d.drawString(dateBuf, cx, midY + 48);
}

void drawSlogan(M5GFX& d) {
  d.setTextDatum(bottom_right);
  d.setFont(&fonts::Font0);
  // Split colors: watch()/stack(sats) orange, && white — draw as one line
  // for sharpness on the small panel.
  d.setTextColor(brandOrange(), C_BLACK);
  d.drawString("watch() && stack(sats)", d.width() - 6, d.height() - 4);
}

}  // namespace

void watchFaceResetCache() {
  gLastMinute = -1;
  gLastDay = -1;
}

void watchFaceDrawFull(const Metrics& m, uint8_t batteryPct, bool charging,
                       bool wifiConnected) {
  auto& d = M5.Display;
  ensureLandscape(d);
  if (d.getBrightness() < 40) {
    d.setBrightness(180);
  }
  drawChrome(d);
  drawWordmark(d);
  drawStatusRow(d, batteryPct, charging, wifiConnected || m.wifiSsid[0]);

  struct tm tmInfo = {};
  if (readLocalTm(tmInfo)) {
    drawClockBlock(d, tmInfo, false);
    gLastMinute = tmInfo.tm_min;
    gLastDay = tmInfo.tm_yday;
  } else {
    d.setTextDatum(MC_DATUM);
    d.setFont(&fonts::FreeSansBold18pt7b);
    d.setTextColor(C_WHITE, C_BLACK);
    d.drawString("--:--", BAND_W + (d.width() - BAND_W) / 2,
                 d.height() / 2 + 28);
  }
  drawSlogan(d);
}

void watchFaceUpdateClockIfNeeded(const Metrics& m, uint8_t batteryPct,
                                  bool charging, bool wifiConnected) {
  struct tm tmInfo = {};
  if (!readLocalTm(tmInfo)) {
    return;
  }
  if (tmInfo.tm_min == gLastMinute && tmInfo.tm_yday == gLastDay) {
    return;
  }
  auto& d = M5.Display;
  ensureLandscape(d);
  drawClockBlock(d, tmInfo, true);
  d.fillRect(BAND_W, d.height() / 2, d.width() - BAND_W, 16, C_BLACK);
  drawStatusRow(d, batteryPct, charging, wifiConnected || m.wifiSsid[0]);
  drawSlogan(d);
  gLastMinute = tmInfo.tm_min;
  gLastDay = tmInfo.tm_yday;
}

void watchFaceDrawStackMode(uint64_t satsBalance) {
  auto& d = M5.Display;
  ensureLandscape(d);
  d.fillScreen(C_TRUE_BLACK);

  d.setTextDatum(TC_DATUM);
  d.setFont(&fonts::Font2);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("STACK MODE", d.width() / 2, 8);

  d.setFont(&fonts::Font0);
  d.setTextColor(C_WHITE, C_TRUE_BLACK);
  d.drawString("1 BTC = 100,000,000 sats", d.width() / 2, 28);

  char satsBuf[24];
  snprintf(satsBuf, sizeof(satsBuf), "%llu",
           static_cast<unsigned long long>(satsBalance));
  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold18pt7b);
  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString(satsBuf, d.width() / 2, 72);

  d.setFont(&fonts::Font0);
  d.setTextColor(C_WHITE, C_TRUE_BLACK);
  d.drawString("sats", d.width() / 2, 96);

  d.setTextColor(brandOrange(), C_TRUE_BLACK);
  d.drawString("keep stacking", d.width() / 2, 120);
}
