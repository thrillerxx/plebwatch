#include "ui.h"

#include <M5Unified.h>
#include <math.h>

namespace {

constexpr uint16_t PLEB_GOLD = 0xF540;   // ~#F2A91D
constexpr uint16_t PLEB_GOLD_DIM = 0xC3E0;  // darker gold
constexpr uint16_t PLEB_WHITE = 0xFFFF;
constexpr uint16_t PLEB_BLACK = 0x0000;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void fillStar(LGFX_Device& d, int cx, int cy, int outerR, int innerR,
              uint16_t color) {
  // 5-point star as 10-triangle fan from center
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

void drawPlebFlagFrame(LGFX_Device& d, int bandW) {
  const int w = d.width();
  const int h = d.height();
  d.fillRect(0, 0, bandW, h, PLEB_GOLD);
  d.fillRect(bandW, 0, w - bandW, h / 2, PLEB_WHITE);
  d.fillRect(bandW, h / 2, w - bandW, h - h / 2, PLEB_BLACK);
  fillStar(d, bandW / 2, h / 2, bandW / 3, bandW / 7, PLEB_WHITE);
}

void header(const char* title, uint8_t batteryPct, bool charging) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(top_left);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.setCursor(4, 2);
  d.printf("%s", title);

  d.setTextDatum(top_right);
  d.setTextColor(charging ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
  d.drawString(String(batteryPct) + (charging ? "%*" : "%"), d.width() - 4, 2);
  d.drawFastHLine(0, 18, d.width(), TFT_DARKGREY);
}

void line(int y, const char* label, const String& value,
          uint16_t valueColor = TFT_WHITE) {
  auto& d = M5.Display;
  d.setTextDatum(top_left);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_SILVER, TFT_BLACK);
  d.setCursor(4, y);
  d.print(label);
  d.setTextColor(valueColor, TFT_BLACK);
  d.setCursor(4, y + 12);
  d.setFont(&fonts::Font2);
  d.print(value);
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
  d.setBrightness(200);
  d.fillScreen(TFT_BLACK);
}

void uiBootSplash() {
  auto& d = M5.Display;
  d.setBrightness(220);
  const int w = d.width();
  const int h = d.height();
  const int bandTarget = w / 3;

  // Animate yellow band + star growing in (PlebLab Texas-flag vibe).
  for (int bandW = 4; bandW <= bandTarget; bandW += 6) {
    d.fillScreen(PLEB_BLACK);
    drawPlebFlagFrame(d, bandW);
    delay(18);
  }
  drawPlebFlagFrame(d, bandTarget);

  // Script-ish wordmark on the white field
  const int textX = bandTarget + (w - bandTarget) / 2;
  const int textY = h / 4;
  d.setTextDatum(MC_DATUM);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(PLEB_BLACK, PLEB_WHITE);
  d.drawString("PlebWatch_", textX, textY);

  delay(180);

  // Code tagline on the black field — same energy as come() && make(it)
  d.setFont(&fonts::Font0);
  d.setTextColor(PLEB_GOLD, PLEB_BLACK);
  const char* tag = "watch() && stack(sats)";
  // Typewriter reveal
  char buf[40] = {};
  for (size_t i = 0; tag[i] != '\0' && i + 1 < sizeof(buf); ++i) {
    buf[i] = tag[i];
    buf[i + 1] = '\0';
    d.setTextDatum(MC_DATUM);
    d.setTextColor(PLEB_BLACK, PLEB_BLACK);
    d.drawString("watch() && stack(sats)", textX, (h * 3) / 4);  // clear
    d.fillRect(bandTarget, h / 2, w - bandTarget, h / 2, PLEB_BLACK);
    d.setTextColor(PLEB_GOLD, PLEB_BLACK);
    d.drawString(buf, textX, (h * 3) / 4);
    delay(28);
  }

  // Accent underline pulse under title
  for (int i = 0; i < 3; ++i) {
    d.drawFastHLine(bandTarget + 12, textY + 16, w - bandTarget - 24,
                    i % 2 ? PLEB_GOLD : PLEB_WHITE);
    delay(120);
  }

  delay(700);
}

void uiShowStatus(const char* line1, const char* line2) {
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.setFont(&fonts::Font2);
  d.drawString(line1, d.width() / 2, d.height() / 2 - 10);
  if (line2) {
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setFont(&fonts::Font0);
    d.drawString(line2, d.width() / 2, d.height() / 2 + 12);
  }
}

void uiDrawPage(Page page, const Metrics& m, uint8_t batteryPct, bool charging,
                uint8_t topNodesSubView) {
  auto& d = M5.Display;
  d.setBrightness(80);

  switch (page) {
    case PAGE_MARKETS:
      header("MARKETS", batteryPct, charging);
      line(24, "BTC USD", m.valid ? fmtUsd(m.priceUsd) : "--", TFT_YELLOW);
      line(56, "SATS / $", m.valid ? String(m.satsPerDollar) : "--", TFT_CYAN);
      line(88, "BLOCK", m.valid ? String(m.blockHeight) : "--", TFT_WHITE);
      break;

    case PAGE_FEES:
      header("FEE EST", batteryPct, charging);
      line(22, "IMMEDIATE", String(m.feeImmediate) + " sat/vB", TFT_RED);
      line(48, "HOUR", String(m.feeHour) + " sat/vB", TFT_ORANGE);
      line(74, "DAY", String(m.feeDay) + " sat/vB", TFT_YELLOW);
      line(100, "WEEK / MEMPOOL",
           String(m.feeWeek) + " | " + String(m.mempoolTxCount) + " tx",
           TFT_LIGHTGREY);
      break;

    case PAGE_MINING: {
      header("MINING", batteryPct, charging);
      line(22, "HASHRATE", fmtEh(m.hashrateEhs), TFT_CYAN);
      char diffBuf[28];
      snprintf(diffBuf, sizeof(diffBuf), "%.2e", m.difficulty);
      line(48, "DIFFICULTY", String(diffBuf), TFT_WHITE);
      char chBuf[40];
      snprintf(chBuf, sizeof(chBuf), "%ld blk  %+.1f%%",
               static_cast<long>(m.blocksToRetarget), m.difficultyChangePct);
      line(74, "RETARGET", String(chBuf), TFT_ORANGE);
      char tBuf[40];
      snprintf(tBuf, sizeof(tBuf), "%.1f min  prev %+.1f%%", m.timeAvgMinutes,
               m.previousRetargetPct);
      line(100, "BLOCK TIME", String(tBuf), TFT_LIGHTGREY);
      break;
    }

    case PAGE_HALVING: {
      header("HALVING", batteryPct, charging);
      line(24, "BLOCKS LEFT", String(m.blocksToHalving), TFT_YELLOW);
      char subBuf[32];
      snprintf(subBuf, sizeof(subBuf), "%.3f BTC  ep %u", m.subsidyBtc,
               m.subsidyEpoch);
      line(56, "SUBSIDY", String(subBuf), TFT_CYAN);
      line(88, "ESTIMATE", String(m.halvingEstimate), TFT_WHITE);
      break;
    }

    case PAGE_LIGHTNING: {
      header("LIGHTNING", batteryPct, charging);
      char capBuf[32];
      snprintf(capBuf, sizeof(capBuf), "%.2f BTC", m.lnCapacityBtc);
      line(24, "CAPACITY", String(capBuf), TFT_MAGENTA);
      line(56, "VALUE", fmtUsd(m.lnCapacityUsd), TFT_YELLOW);
      char ncBuf[40];
      snprintf(ncBuf, sizeof(ncBuf), "%lu n / %lu ch",
               static_cast<unsigned long>(m.lnNodes),
               static_cast<unsigned long>(m.lnChannels));
      line(88, "NETWORK", String(ncBuf), TFT_CYAN);
      break;
    }

    case PAGE_TOP_NODES: {
      if (topNodesSubView == 0) {
        header("TOP LN", batteryPct, charging);
        int y = 22;
        for (uint8_t i = 0; i < m.lnTopCount && i < 5; ++i) {
          d.setTextDatum(top_left);
          d.setFont(&fonts::Font0);
          d.setTextColor(TFT_SILVER, TFT_BLACK);
          d.setCursor(4, y);
          char row[48];
          snprintf(row, sizeof(row), "%u. %.2f %s", i + 1,
                   m.lnTop[i].capacityBtc, m.lnTop[i].alias);
          d.print(row);
          y += 18;
        }
        if (m.lnTopCount == 0) {
          line(40, "LN TOP", "no data", TFT_DARKGREY);
        }
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.setFont(&fonts::Font0);
        d.setCursor(4, 120);
        d.print("B: versions");
      } else {
        header("NODE VER", batteryPct, charging);
        if (m.reachableNodes > 0) {
          d.setTextDatum(top_left);
          d.setFont(&fonts::Font0);
          d.setTextColor(TFT_DARKGREY, TFT_BLACK);
          d.setCursor(4, 20);
          d.printf("reachable ~%lu",
                   static_cast<unsigned long>(m.reachableNodes));
        }
        int y = 34;
        for (uint8_t i = 0; i < m.versionCount && i < 5; ++i) {
          d.setTextDatum(top_left);
          d.setFont(&fonts::Font0);
          d.setTextColor(TFT_CYAN, TFT_BLACK);
          d.setCursor(4, y);
          char row[48];
          snprintf(row, sizeof(row), "%u. %s %.0f%%", i + 1,
                   m.versions[i].name, m.versions[i].pct);
          d.print(row);
          y += 16;
        }
        if (m.versionCount == 0) {
          line(50, "VERSIONS", "sample unavailable", TFT_DARKGREY);
        }
        d.setTextColor(TFT_DARKGREY, TFT_BLACK);
        d.setFont(&fonts::Font0);
        d.setCursor(4, 120);
        d.print("B: LN top");
      }
      break;
    }

    default:
      break;
  }

  // Footer wifi hint
  d.setTextDatum(bottom_left);
  d.setFont(&fonts::Font0);
  d.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (m.wifiSsid[0]) {
    d.drawString(m.wifiSsid, 4, d.height() - 2);
  }
}

void uiSleepDisplay() {
  // Don't call Display.sleep() on Plus2 — can look "bricked" with HOLD quirks.
  M5.Display.setBrightness(0);
  M5.Display.fillScreen(TFT_BLACK);
}
