#include "ui.h"

#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "local_clock.h"
#include "splash_image.h"
#include "watch_face.h"

namespace {

int gHeaderLastMinute = -1;
int gHeaderLastDay = -1;

void drawHeaderClock(uint8_t batteryPct, bool charging) {
  auto& d = M5.Display;
  char hm[12];
  localClockFormatHm(hm, sizeof(hm));
  const String bat = String(batteryPct) + (charging ? "%*" : "%");
  // Clear prior text so minute flips don't leave ghosts.
  d.fillRect(d.width() / 2, 0, d.width() / 2, 20, TFT_BLACK);
  d.setTextDatum(top_right);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.drawString(String(hm) + "  " + bat, d.width() - 4, 4);
}

void header(const char* title, uint8_t batteryPct, bool charging) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(top_left);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setCursor(4, 4);
  d.printf("%s", title);

  drawHeaderClock(batteryPct, charging);
  d.drawFastHLine(0, 22, d.width(), TFT_DARKGREY);

  struct tm tmInfo = {};
  if (localClockReadTm(&tmInfo)) {
    gHeaderLastMinute = tmInfo.tm_min;
    gHeaderLastDay = tmInfo.tm_yday;
  } else {
    gHeaderLastMinute = -1;
    gHeaderLastDay = -1;
  }
}

// Word-wrap using the currently selected font.
void drawWrappedText(M5GFX& d, const char* text, int x, int y, int maxWidth,
                     int lineHeight, int maxLines, uint16_t color) {
  if (!text || !text[0] || maxLines <= 0) {
    return;
  }
  d.setTextDatum(top_left);
  d.setTextColor(color, TFT_BLACK);

  char lineBuf[48];
  int linePos = 0;
  int cursorY = y;
  int linesDrawn = 0;

  auto flushLine = [&]() {
    if (linePos == 0 || linesDrawn >= maxLines) {
      linePos = 0;
      return;
    }
    lineBuf[linePos] = '\0';
    while (linePos > 0 && lineBuf[linePos - 1] == ' ') {
      lineBuf[--linePos] = '\0';
    }
    d.setCursor(x, cursorY);
    d.print(lineBuf);
    cursorY += lineHeight;
    linePos = 0;
    ++linesDrawn;
  };

  for (const char* p = text; *p && linesDrawn < maxLines; ++p) {
    if (*p == '\n') {
      flushLine();
      continue;
    }
    if (linePos >= static_cast<int>(sizeof(lineBuf) - 1)) {
      flushLine();
    }
    lineBuf[linePos++] = *p;
    lineBuf[linePos] = '\0';
    if (d.textWidth(lineBuf) <= maxWidth) {
      continue;
    }
    int breakAt = linePos - 1;
    while (breakAt > 0 && lineBuf[breakAt] != ' ') {
      --breakAt;
    }
    char carry[48];
    int carryLen = 0;
    if (breakAt > 0) {
      carryLen = linePos - breakAt - 1;
      if (carryLen > 0) {
        memcpy(carry, lineBuf + breakAt + 1, carryLen);
      }
      linePos = breakAt;
    } else {
      carryLen = 1;
      carry[0] = lineBuf[linePos - 1];
      linePos -= 1;
    }
    flushLine();
    if (carryLen > 0 && linesDrawn < maxLines) {
      memcpy(lineBuf, carry, carryLen);
      linePos = carryLen;
      lineBuf[linePos] = '\0';
    }
  }
  flushLine();
}

void line(int y, const char* label, const String& value,
          uint16_t valueColor = TFT_WHITE) {
  auto& d = M5.Display;
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_SILVER, TFT_BLACK);
  d.setCursor(4, y);
  d.print(label);
  d.setTextColor(valueColor, TFT_BLACK);
  d.setCursor(4, y + 16);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.print(value);
}

// Single-row metric for denser 4-line pages (still larger type).
void row(int y, const char* label, const String& value,
         uint16_t valueColor = TFT_WHITE) {
  auto& d = M5.Display;
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_SILVER, TFT_BLACK);
  d.setCursor(4, y);
  d.print(label);
  d.setTextDatum(top_right);
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(valueColor, TFT_BLACK);
  d.drawString(value, d.width() - 4, y);
}

String fmtUsd(float v) {
  if (v >= 1000.0f) {
    return String("$") + String(v, 0);
  }
  return String("$") + String(v, 2);
}

String fmtEh(double v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f EH/s", v);
  return String(buf);
}

}  // namespace

void uiBegin() {
  auto& d = M5.Display;
  d.setRotation(1);
  d.setBrightness(220);
  d.fillScreen(TFT_BLACK);
}

void uiBootSplash() {
  auto& d = M5.Display;
  d.setBrightness(220);
  d.fillScreen(TFT_BLACK);

  // Full-bleed splash from codex bridge art (240x135).
  d.setSwapBytes(true);
  d.pushImage(0, 0, SPLASH_W, SPLASH_H, SPLASH_RGB565);
  d.setSwapBytes(false);

  // Hold at least 8 seconds so the boot art can be enjoyed.
  delay(8000);
}

namespace {

uint16_t bootOrange() {
#ifdef PLEBWATCH_ORANGE_RGB565
  return PLEBWATCH_ORANGE_RGB565;
#else
  return TFT_ORANGE;
#endif
}

char gBootTitle[28] = {};
char gBootDetail[36] = {};
uint8_t gBootFrame = 0;

void drawBootChrome(M5GFX& d) {
  d.fillScreen(TFT_BLACK);
  // Flag-style accent band
  d.fillRect(0, 0, 10, d.height(), bootOrange());
  d.fillCircle(5, d.height() / 2, 3, TFT_WHITE);
  d.drawFastHLine(16, 18, d.width() - 24, 0x4208);
  d.drawFastHLine(16, d.height() - 18, d.width() - 24, 0x4208);
}

void drawBootSpinner(M5GFX& d, int cx, int cy, uint8_t frame) {
  // Orbiting sats around a center pip
  d.fillCircle(cx, cy, 3, bootOrange());
  for (int i = 0; i < 8; ++i) {
    const float ang = (frame + i) * (M_PI / 4.0f);
    const int x = cx + static_cast<int>(18.0f * cosf(ang));
    const int y = cy + static_cast<int>(10.0f * sinf(ang));
    const bool hot = ((frame + i) % 8) < 3;
    d.fillCircle(x, y, hot ? 3 : 2, hot ? bootOrange() : 0x8410);
  }
}

void drawBootWifiArcs(M5GFX& d, int cx, int cy, uint8_t frame) {
  // Expanding wifi-style arcs
  const int phase = frame % 4;
  d.fillCircle(cx, cy + 10, 3, bootOrange());
  for (int a = 0; a <= phase; ++a) {
    const int r = 10 + a * 9;
    d.drawCircle(cx, cy + 10, r, a == phase ? bootOrange() : 0x8410);
    // Clip bottom half visually by overpainting (simple arcs)
    d.fillRect(cx - r - 1, cy + 12, 2 * r + 2, r + 4, TFT_BLACK);
  }
}

void paintBootStatus(bool animateWifi) {
  auto& d = M5.Display;
  uiEnsureLandscape();
  drawBootChrome(d);

  d.setTextDatum(TC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(bootOrange(), TFT_BLACK);
  d.drawString(gBootTitle[0] ? gBootTitle : "...", d.width() / 2 + 4, 28);

  if (gBootDetail[0]) {
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.drawString(gBootDetail, d.width() / 2 + 4, 56);
  }

  const int cx = d.width() / 2 + 4;
  const int cy = 96;
  if (animateWifi) {
    drawBootWifiArcs(d, cx, cy, gBootFrame);
  } else {
    drawBootSpinner(d, cx, cy, gBootFrame);
  }

  // Marching dots
  char dots[5] = "    ";
  const int n = (gBootFrame % 4);
  for (int i = 0; i < n; ++i) {
    dots[i] = '.';
  }
  d.setFont(&fonts::FreeSansBold9pt7b);
  d.setTextColor(bootOrange(), TFT_BLACK);
  d.setTextDatum(MC_DATUM);
  d.drawString(dots, cx, 122);
}

bool bootTitleIsWifi() {
  return strstr(gBootTitle, "WiFi") != nullptr ||
         strstr(gBootTitle, "Wi-Fi") != nullptr ||
         strstr(gBootTitle, "wifi") != nullptr;
}

}  // namespace

void uiShowStatus(const char* line1, const char* line2) {
  // Keep legacy callers working — route through animated boot UI.
  uiBootStatus(line1, line2);
}

void uiBootStatus(const char* title, const char* detail) {
  strncpy(gBootTitle, title ? title : "", sizeof(gBootTitle) - 1);
  gBootTitle[sizeof(gBootTitle) - 1] = '\0';
  if (detail) {
    strncpy(gBootDetail, detail, sizeof(gBootDetail) - 1);
    gBootDetail[sizeof(gBootDetail) - 1] = '\0';
  } else {
    gBootDetail[0] = '\0';
  }
  gBootFrame = 0;

  // Intro animation burst so the stage feels alive before work continues.
  const bool wifi = bootTitleIsWifi();
  for (int i = 0; i < 8; ++i) {
    paintBootStatus(wifi);
    gBootFrame++;
    delay(70);
  }
}

void uiBootBusyTick() {
  if (!gBootTitle[0]) {
    return;
  }
  gBootFrame++;
  paintBootStatus(bootTitleIsWifi());
}

void uiEnsureLandscape() {
  if (M5.Display.getRotation() != 1) {
    M5.Display.setRotation(1);
  }
}

bool uiIsWatchPage(Page page) {
  return page == PAGE_WATCH || page == PAGE_STACK;
}

void uiDrawPage(Page page, const Metrics& m, uint8_t batteryPct, bool charging) {
  auto& d = M5.Display;

  if (page == PAGE_WATCH) {
    watchFaceDrawFull(m, batteryPct, charging, m.wifiSsid[0] != '\0');
    return;
  }
  if (page == PAGE_STACK) {
    watchFaceDrawStackMode(PLEBWATCH_SATS_BALANCE);
    return;
  }

  uiEnsureLandscape();
  d.setBrightness(d.getBrightness() < 40 ? 80 : d.getBrightness());

  switch (page) {
    case PAGE_MARKETS:
      header("MARKETS", batteryPct, charging);
      line(26, "BTC USD", m.valid ? fmtUsd(m.priceUsd) : "--", TFT_YELLOW);
      line(62, "SATS / $", m.valid ? String(m.satsPerDollar) : "--", TFT_CYAN);
      line(98, "BLOCK", m.valid ? String(m.blockHeight) : "--", TFT_WHITE);
      break;

    case PAGE_FEES:
      header("FEE EST", batteryPct, charging);
      row(28, "NOW", String(m.feeImmediate) + " sat/vB", TFT_RED);
      row(54, "HOUR", String(m.feeHour) + " sat/vB", TFT_ORANGE);
      row(80, "DAY", String(m.feeDay) + " sat/vB", TFT_YELLOW);
      row(106, "WEEK",
          String(m.feeWeek) + " | " + String(m.mempoolTxCount) + " tx",
          TFT_LIGHTGREY);
      break;

    case PAGE_MINING: {
      header("MINING", batteryPct, charging);
      row(28, "HASHRATE", fmtEh(m.hashrateEhs), TFT_CYAN);
      char diffBuf[28];
      snprintf(diffBuf, sizeof(diffBuf), "%.2e", m.difficulty);
      row(54, "DIFFICULTY", String(diffBuf), TFT_WHITE);
      char chBuf[40];
      snprintf(chBuf, sizeof(chBuf), "%ld  %+.1f%%",
               static_cast<long>(m.blocksToRetarget), m.difficultyChangePct);
      row(80, "RETARGET", String(chBuf), TFT_ORANGE);
      char tBuf[40];
      snprintf(tBuf, sizeof(tBuf), "%.1fm  %+.1f%%", m.timeAvgMinutes,
               m.previousRetargetPct);
      row(106, "BLOCK TIME", String(tBuf), TFT_LIGHTGREY);
      break;
    }

    case PAGE_HALVING: {
      header("HALVING", batteryPct, charging);
      line(26, "BLOCKS LEFT", String(m.blocksToHalving), TFT_YELLOW);
      char subBuf[32];
      snprintf(subBuf, sizeof(subBuf), "%.3f BTC  ep %u", m.subsidyBtc,
               m.subsidyEpoch);
      line(62, "SUBSIDY", String(subBuf), TFT_CYAN);
      line(98, "ESTIMATE", String(m.halvingEstimate), TFT_WHITE);
      break;
    }

    case PAGE_LIGHTNING: {
      header("LIGHTNING", batteryPct, charging);
      char capBuf[32];
      snprintf(capBuf, sizeof(capBuf), "%.2f BTC", m.lnCapacityBtc);
      line(26, "CAPACITY", String(capBuf), TFT_MAGENTA);
      line(62, "VALUE", fmtUsd(m.lnCapacityUsd), TFT_YELLOW);
      char ncBuf[40];
      snprintf(ncBuf, sizeof(ncBuf), "%lu n / %lu ch",
               static_cast<unsigned long>(m.lnNodes),
               static_cast<unsigned long>(m.lnChannels));
      line(98, "NETWORK", String(ncBuf), TFT_CYAN);
      break;
    }

    case PAGE_TOP_NODES: {
      static const uint16_t kRankColor[] = {TFT_ORANGE, TFT_YELLOW, TFT_CYAN,
                                           TFT_MAGENTA};
      header("TOP LN", batteryPct, charging);
      int y = 26;
      for (uint8_t i = 0; i < m.lnTopCount && i < 4; ++i) {
        const uint16_t rank = kRankColor[i];
        d.setTextDatum(top_left);
        d.setFont(&fonts::Font2);
        d.setCursor(4, y);
        d.setTextColor(rank, TFT_BLACK);
        d.printf("%u.", i + 1);
        d.setTextColor(TFT_YELLOW, TFT_BLACK);
        d.printf(" %.2f", m.lnTop[i].capacityBtc);
        d.setTextColor(TFT_SILVER, TFT_BLACK);
        d.print(" btc ");
        d.setTextColor(TFT_WHITE, TFT_BLACK);
        char alias[20];
        strncpy(alias, m.lnTop[i].alias, sizeof(alias) - 1);
        alias[sizeof(alias) - 1] = '\0';
        d.print(alias);
        y += 22;
      }
      if (m.lnTopCount == 0) {
        line(40, "LN TOP", "no data", TFT_DARKGREY);
      }
      break;
    }

    case PAGE_QUOTES: {
      header("QUOTES", batteryPct, charging);
      if (m.satoshiQuoteOk && m.satoshiQuote[0]) {
        d.setFont(&fonts::FreeSerif9pt7b);
        drawWrappedText(d, m.satoshiQuote, 6, 26, d.width() - 12, 15, 6,
                        TFT_WHITE);
        d.setFont(&fonts::Font0);
        d.setTextColor(TFT_ORANGE, TFT_BLACK);
        d.setTextDatum(bottom_left);
        if (m.satoshiQuoteDate[0]) {
          d.drawString(m.satoshiQuoteDate, 6, d.height() - 4);
        } else {
          d.drawString("Satoshi Nakamoto", 6, d.height() - 4);
        }
      } else {
        d.setFont(&fonts::FreeSerif9pt7b);
        d.setTextDatum(MC_DATUM);
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.drawString("quote on next wake", d.width() / 2, 55);
      }
      break;
    }

    default:
      break;
  }

  if (m.wifiSsid[0]) {
    d.setTextDatum(bottom_right);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.drawString(m.wifiSsid, d.width() - 4, d.height() - 2);
  }
}

void uiUpdateHeaderClockIfNeeded(uint8_t batteryPct, bool charging) {
  struct tm tmInfo = {};
  if (!localClockReadTm(&tmInfo)) {
    return;
  }
  if (tmInfo.tm_min == gHeaderLastMinute && tmInfo.tm_yday == gHeaderLastDay) {
    return;
  }
  uiEnsureLandscape();
  drawHeaderClock(batteryPct, charging);
  gHeaderLastMinute = tmInfo.tm_min;
  gHeaderLastDay = tmInfo.tm_yday;
}

void uiSleepDisplay() {
  M5.Display.setBrightness(0);
  M5.Display.fillScreen(TFT_BLACK);
}
