#pragma once

#include <Arduino.h>

struct LnTopNode {
  char alias[28];
  float capacityBtc;
};

struct BtcNodeVersion {
  char name[28];
  float pct;
};

struct Metrics {
  bool valid;

  // Markets
  float priceUsd;
  uint32_t satsPerDollar;
  uint32_t blockHeight;
  uint32_t tipTimestamp;

  // Fees
  uint32_t feeImmediate;
  uint32_t feeHour;
  uint32_t feeDay;
  uint32_t feeWeek;
  uint32_t mempoolTxCount;
  float mempoolMinFee;

  // Mining / difficulty
  double hashrateEhs;
  double difficulty;
  uint32_t blocksToRetarget;
  float difficultyChangePct;
  float previousRetargetPct;
  float remainingTimeDays;
  float timeAvgMinutes;

  // Halving
  uint32_t blocksToHalving;
  uint8_t subsidyEpoch;
  float subsidyBtc;
  char halvingEstimate[20];

  // Lightning
  float lnCapacityBtc;
  float lnCapacityUsd;
  uint32_t lnNodes;
  uint32_t lnChannels;

  // Top nodes
  LnTopNode lnTop[5];
  uint8_t lnTopCount;
  BtcNodeVersion versions[5];
  uint8_t versionCount;
  uint32_t reachableNodes;

  // Satoshi quote (Nakamoto Institute) — refreshed each successful fetch.
  // Kept short so it fits the Stick with a readable serif face.
  char satoshiQuote[160];
  char satoshiQuoteDate[12];
  bool satoshiQuoteOk;

  char wifiSsid[33];
  uint32_t fetchedAtMs;
};

enum Page : uint8_t {
  PAGE_WATCH = 0,
  PAGE_STACK,
  PAGE_MARKETS,
  PAGE_FEES,
  PAGE_MINING,
  PAGE_HALVING,
  PAGE_LIGHTNING,
  PAGE_TOP_NODES,
  PAGE_QUOTES,
  PAGE_COUNT
};
