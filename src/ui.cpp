#include "ui.h"

#include <M5Unified.h>

#include "splash_image.h"

namespace {

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

  if (m.wifiSsid[0]) {
    d.setTextDatum(bottom_right);
    d.setFont(&fonts::Font0);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.drawString(m.wifiSsid, d.width() - 4, d.height() - 2);
  }
}

void uiSleepDisplay() {
  M5.Display.setBrightness(0);
  M5.Display.fillScreen(TFT_BLACK);
}
