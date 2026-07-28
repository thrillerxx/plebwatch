#pragma once

#include "metrics.h"

void watchFaceDrawFull(const Metrics& m, uint8_t batteryPct, bool charging,
                       bool wifiConnected);
void watchFaceUpdateClockIfNeeded(const Metrics& m, uint8_t batteryPct,
                                  bool charging, bool wifiConnected);
void watchFaceDrawStackMode(uint64_t satsBalance);
void watchFaceResetCache();
