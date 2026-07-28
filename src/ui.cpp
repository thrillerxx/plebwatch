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
  // Shrink long titles so they don't collide with the clock/battery.
  d.setFont(&fonts::FreeSansBold9pt7b);
  if (d.textWidth(title) > d.width() / 2 - 6) {
    d.setFont(&fonts::Font2);
  }
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
// dryRun: measure only. Sets *truncated if text does not fully fit in maxLines.
// Returns Y just below last line (or y if dryRun / empty).
int wrapText(M5GFX& d, const char* text, int x, int y, int maxWidth,
             int lineHeight, int maxLines, uint16_t color, bool dryRun,
             bool* truncated) {
  if (truncated) {
    *truncated = false;
  }
  if (!text || !text[0] || maxLines <= 0) {
    return y;
  }
  if (!dryRun) {
    d.setTextDatum(top_left);
    d.setTextColor(color, TFT_BLACK);
  }

  char lineBuf[72];
  int linePos = 0;
  int cursorY = y;
  int linesDrawn = 0;
  const char* p = text;

  auto flushLine = [&]() {
    if (linePos == 0 || linesDrawn >= maxLines) {
      linePos = 0;
      return;
    }
    lineBuf[linePos] = '\0';
    while (linePos > 0 && lineBuf[linePos - 1] == ' ') {
      lineBuf[--linePos] = '\0';
    }
    if (!dryRun) {
      d.setCursor(x, cursorY);
      d.print(lineBuf);
    }
    cursorY += lineHeight;
    linePos = 0;
    ++linesDrawn;
  };

  for (; *p && linesDrawn < maxLines; ++p) {
    if (*p == '\n') {
      flushLine();
      continue;
    }
    if (linePos >= static_cast<int>(sizeof(lineBuf) - 1)) {
      flushLine();
      if (linesDrawn >= maxLines) {
        break;
      }
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
    char carry[72];
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

  // Skip trailing spaces when checking leftover text.
  while (*p == ' ') {
    ++p;
  }
  if (*p || linePos > 0) {
    if (truncated) {
      *truncated = true;
    }
  }
  return cursorY;
}

int drawWrappedText(M5GFX& d, const char* text, int x, int y, int maxWidth,
                    int lineHeight, int maxLines, uint16_t color) {
  return wrapText(d, text, x, y, maxWidth, lineHeight, maxLines, color,
                  /*dryRun=*/false, nullptr);
}

bool wrappedTextFits(M5GFX& d, const char* text, int maxWidth, int maxLines) {
  bool truncated = false;
  wrapText(d, text, 0, 0, maxWidth, 1, maxLines, TFT_WHITE, /*dryRun=*/true,
           &truncated);
  return !truncated;
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
uint32_t gBootLastTickMs = 0;

// Keep copy in the top band; animations stay below this Y.
constexpr int kBootAnimTop = 56;

bool bootTitleIsWifi() {
  return strstr(gBootTitle, "WiFi") != nullptr ||
         strstr(gBootTitle, "Wi-Fi") != nullptr ||
         strstr(gBootTitle, "wifi") != nullptr;
}

bool bootTitleIsTimezone() {
  return strstr(gBootTitle, "Time") != nullptr ||
         strstr(gBootTitle, "Zone") != nullptr ||
         strstr(gBootTitle, "NTP") != nullptr;
}

void drawBootTitleBlock(M5GFX& d) {
  d.setTextDatum(TC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(bootOrange(), TFT_BLACK);
  d.drawString(gBootTitle[0] ? gBootTitle : "...", d.width() / 2, 6);

  if (gBootDetail[0]) {
    d.setFont(&fonts::Font2);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.drawString(gBootDetail, d.width() / 2, 30);
  }
  // Hairline separator so text and motion don't feel packed.
  d.drawFastHLine(28, 48, d.width() - 56, 0x3186);
}

// Full-bleed Wi‑Fi mark — sized for the lower animation band.
void drawBootWifiFull(M5GFX& d, uint8_t frame) {
  const int cx = d.width() / 2;
  const int apexY = 108;
  const int phase = frame % 4;
  const uint16_t dim = 0x4208;
  const uint16_t hot = bootOrange();

  d.fillCircle(cx, apexY, 4, hot);

  // Upper-facing arcs (LGFX degrees: 0° = 3 o'clock, CCW).
  for (int a = 0; a < 3; ++a) {
    const int r1 = 11 + a * 11;  // 11 / 22 / 33 — stays under the text band
    const int r0 = r1 - 3;
    const bool on = a <= phase;
    const bool tip = on && a == phase;
    d.fillArc(cx, apexY, r0, r1, 205, 335, tip ? hot : (on ? 0xFBE0 : dim));
  }
}

// Geo-locate vibe: globe, sweeping meridian, traveling ping.
void drawBootTimezoneGlobe(M5GFX& d, uint8_t frame) {
  const int cx = d.width() / 2;
  const int cy = 92;
  const int rx = 36;
  const int ry = 20;
  const uint16_t dim = 0x3186;
  const uint16_t mid = 0x8410;
  const uint16_t hot = bootOrange();

  d.drawEllipse(cx, cy, rx, ry, mid);
  d.drawEllipse(cx, cy, rx - 1, ry - 1, dim);

  // Latitude bands
  for (int i = -2; i <= 2; ++i) {
    if (i == 0) {
      d.drawFastHLine(cx - rx + 2, cy, 2 * rx - 4, mid);
      continue;
    }
    const float t = i / 2.5f;
    const float w = rx * sqrtf(1.0f - t * t);
    const int yy = cy + static_cast<int>(ry * t);
    d.drawFastHLine(cx - static_cast<int>(w), yy, static_cast<int>(2 * w), dim);
  }

  // Rotating meridian (longitude sweep)
  const float lon = frame * 0.22f;
  int prevX = 0;
  int prevY = 0;
  for (int step = 0; step <= 20; ++step) {
    const float lat = -0.5f * M_PI + (M_PI * step) / 20.0f;
    const float cl = cosf(lat);
    const int x = cx + static_cast<int>(rx * cl * sinf(lon));
    const int y = cy + static_cast<int>(ry * sinf(lat));
    if (step > 0) {
      d.drawLine(prevX, prevY, x, y, hot);
    }
    prevX = x;
    prevY = y;
  }

  // Opposite dim meridian for depth
  const float lon2 = lon + M_PI;
  for (int step = 0; step <= 20; ++step) {
    const float lat = -0.5f * M_PI + (M_PI * step) / 20.0f;
    const float cl = cosf(lat);
    const int x = cx + static_cast<int>(rx * cl * sinf(lon2));
    const int y = cy + static_cast<int>(ry * sinf(lat));
    if (step > 0) {
      d.drawLine(prevX, prevY, x, y, dim);
    }
    prevX = x;
    prevY = y;
  }

  // Traveling "you are here" ping
  const float ping = frame * 0.32f;
  const int px = cx + static_cast<int>(rx * 0.62f * cosf(ping));
  const int py = cy + static_cast<int>(ry * 0.38f * sinf(ping * 1.35f));
  const int ring = 3 + (frame % 5);
  d.drawCircle(px, py, ring, hot);
  d.drawCircle(px, py, ring + 3, dim);
  d.fillCircle(px, py, 2, TFT_WHITE);

  // Tiny orbiting sat
  const float sat = frame * 0.42f;
  const int sx = cx + static_cast<int>((rx + 7) * cosf(sat));
  const int sy = cy + static_cast<int>((ry + 4) * sinf(sat));
  d.fillCircle(sx, sy, 2, hot);
}

// Mempool fetch: small square blocks, 3-across rows. Each row fully finishes
// (with a short pause) before the next section starts.
constexpr int kBlockCols = 3;
constexpr int kBlockRows = 3;
constexpr int kBlockMax = kBlockCols * kBlockRows;
constexpr int kBlockSize = 16;
constexpr int kBlockGap = 3;
constexpr int kBlockDropFrames = 4;
constexpr int kBlockRowPause = 7;   // hold after each completed row of 3
constexpr int kBlockHoldFull = 12;  // hold full 3×3 before looping

int blockRowPhaseLen() {
  return kBlockCols * kBlockDropFrames + kBlockRowPause;
}

int blockCycleLen() {
  return kBlockRows * blockRowPhaseLen() + kBlockHoldFull;
}

// Decode timeline → settled count, optional falling block + drop step, row pulse.
void blockAnimState(int phase, int& settled, int& falling, int& dropStep,
                    int& completeRow) {
  settled = 0;
  falling = -1;
  dropStep = 0;
  completeRow = -1;

  const int rowLen = blockRowPhaseLen();
  const int buildLen = kBlockRows * rowLen;
  if (phase >= buildLen) {
    settled = kBlockMax;
    return;
  }

  const int row = phase / rowLen;
  const int within = phase % rowLen;
  settled = row * kBlockCols;

  if (within >= kBlockCols * kBlockDropFrames) {
    // Row of 3 is finished — pause on the completed section.
    settled = (row + 1) * kBlockCols;
    completeRow = row;
    return;
  }

  const int inRow = within / kBlockDropFrames;  // 0..2
  dropStep = within % kBlockDropFrames;
  settled = row * kBlockCols + inRow;
  falling = settled;
}

void drawBootBlocksStack(M5GFX& d, uint8_t frame) {
  const int gridW = kBlockCols * kBlockSize + (kBlockCols - 1) * kBlockGap;
  const int left0 = (d.width() - gridW) / 2;
  const int baseY = d.height() - 10;
  const int startTop = kBootAnimTop + 4;

  static const uint16_t kColors[kBlockMax] = {
      0xF542, 0x07FF, 0xFFE0, 0xF81F, 0x07E0, 0x001F, 0xFD20, 0xAFE5, 0xF800,
  };

  auto cellPos = [&](int index, int& left, int& top) {
    const int col = index % kBlockCols;
    const int row = index / kBlockCols;  // 0 = bottom row
    left = left0 + col * (kBlockSize + kBlockGap);
    top = baseY - (row + 1) * kBlockSize - row * kBlockGap;
  };

  auto drawBlock = [&](int left, int top, uint16_t fill, bool highlight) {
    d.fillRect(left, top, kBlockSize, kBlockSize, fill);
    d.drawRect(left, top, kBlockSize, kBlockSize,
               highlight ? TFT_WHITE : 0x4208);
    if (highlight) {
      d.drawRect(left + 1, top + 1, kBlockSize - 2, kBlockSize - 2, TFT_WHITE);
    }
  };

  const int phase = frame % blockCycleLen();
  int settled = 0;
  int falling = -1;
  int dropStep = 0;
  int completeRow = -1;
  blockAnimState(phase, settled, falling, dropStep, completeRow);

  for (int i = 0; i < settled; ++i) {
    int left = 0;
    int top = 0;
    cellPos(i, left, top);
    const int row = i / kBlockCols;
    const bool rowGlow = (completeRow >= 0 && row == completeRow);
    drawBlock(left, top, kColors[i], rowGlow);
  }

  if (falling >= 0 && falling < kBlockMax) {
    int targetLeft = 0;
    int targetTop = 0;
    cellPos(falling, targetLeft, targetTop);
    const int top =
        startTop +
        ((targetTop - startTop) * dropStep) / (kBlockDropFrames - 1);
    drawBlock(targetLeft, top, kColors[falling], true);
  }

  // Full grid complete — outline the finished stack.
  if (settled >= kBlockMax) {
    d.drawRect(left0 - 3,
               baseY - kBlockRows * kBlockSize - (kBlockRows - 1) * kBlockGap - 3,
               gridW + 6,
               kBlockRows * kBlockSize + (kBlockRows - 1) * kBlockGap + 2,
               bootOrange());
  }
}

void paintBootStatus() {
  auto& d = M5.Display;
  uiEnsureLandscape();
  // Always wipe the full framebuffer so splash orange never bleeds through.
  d.fillScreen(TFT_BLACK);

  drawBootTitleBlock(d);

  if (bootTitleIsWifi()) {
    drawBootWifiFull(d, gBootFrame);
  } else if (bootTitleIsTimezone()) {
    drawBootTimezoneGlobe(d, gBootFrame);
  } else {
    drawBootBlocksStack(d, gBootFrame);
  }
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
  gBootLastTickMs = 0;

  // Hold the first frame so title + subtitle can be read, then ease in.
  paintBootStatus();
  delay(1200);
  for (int i = 0; i < 12; ++i) {
    gBootFrame++;
    paintBootStatus();
    delay(130);
  }
  gBootLastTickMs = millis();
}

void uiBootBusyTick() {
  if (!gBootTitle[0]) {
    return;
  }
  // Keep motion readable — don't redraw faster than ~8 fps.
  const uint32_t now = millis();
  if (gBootLastTickMs != 0 && (now - gBootLastTickMs) < 120) {
    return;
  }
  gBootLastTickMs = now;
  gBootFrame++;
  paintBootStatus();
}

void uiBootFinishBlocks() {
  // Only meaningful on the fetch / blocks boot stage.
  if (!gBootTitle[0]) {
    return;
  }
  if (bootTitleIsWifi() || bootTitleIsTimezone()) {
    return;
  }

  const int cycle = blockCycleLen();
  const int buildLen = kBlockRows * blockRowPhaseLen();
  // Continue from current frame until this cycle's full 3×3 is held.
  int guard = cycle + 8;
  while (guard-- > 0) {
    const int phase = static_cast<int>(gBootFrame) % cycle;
    paintBootStatus();
    if (phase >= buildLen) {
      // Finish the hold so the completed stack is readable.
      const int holdLeft = cycle - phase;
      for (int i = 0; i < holdLeft; ++i) {
        gBootFrame++;
        paintBootStatus();
        delay(110);
      }
      return;
    }
    gBootFrame++;
    delay(110);
  }
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
      header("SATOSHI QUOTES", batteryPct, charging);
      if (m.satoshiQuoteOk && m.satoshiQuote[0]) {
        // Pick the largest serif that still fits above the date strip.
        const int qX = 4;
        const int qY = 24;
        const int qW = d.width() - 8;
        struct QuoteStyle {
          const lgfx::IFont* font;
          int lineHeight;
          int maxLines;
        };
        const QuoteStyle styles[] = {
            {&fonts::FreeSerifBold12pt7b, 20, 4},
            {&fonts::FreeSerif12pt7b, 19, 5},
            {&fonts::FreeSerifBold9pt7b, 16, 5},
            {&fonts::FreeSerif9pt7b, 15, 6},
        };
        const QuoteStyle* chosen = &styles[sizeof(styles) / sizeof(styles[0]) - 1];
        for (const auto& s : styles) {
          d.setFont(s.font);
          if (wrappedTextFits(d, m.satoshiQuote, qW, s.maxLines)) {
            chosen = &s;
            break;
          }
        }
        d.setFont(chosen->font);
        drawWrappedText(d, m.satoshiQuote, qX, qY, qW, chosen->lineHeight,
                        chosen->maxLines, TFT_WHITE);

        d.setFont(&fonts::Font2);
        d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        d.setTextDatum(bottom_left);
        if (m.satoshiQuoteDate[0]) {
          d.drawString(m.satoshiQuoteDate, 6, d.height() - 3);
        } else {
          d.drawString("Satoshi Nakamoto", 6, d.height() - 3);
        }
      } else {
        d.setFont(&fonts::FreeSerif12pt7b);
        d.setTextDatum(MC_DATUM);
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.drawString("quote on next wake", d.width() / 2, 55);
      }
      break;
    }

    default:
      break;
  }

  // Keep quotes page bottom clear for the date.
  if (m.wifiSsid[0] && page != PAGE_QUOTES) {
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
