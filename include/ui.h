#pragma once

#include "metrics.h"

void uiBegin();
void uiBootSplash();
void uiShowStatus(const char* line1, const char* line2 = nullptr);
// Animated boot stage (Wi‑Fi / time sync / fetch) — large type + motion.
void uiBootStatus(const char* title, const char* detail = nullptr);
// Advance spinner while a long boot step is running (call from wait loops).
void uiBootBusyTick();
// After metrics fetch, finish the current 3×3 block stack so no row is left half-done.
void uiBootFinishBlocks();
void uiDrawPage(Page page, const Metrics& m, uint8_t batteryPct, bool charging);
void uiEnsureLandscape();
bool uiIsWatchPage(Page page);
void uiUpdateHeaderClockIfNeeded(uint8_t batteryPct, bool charging);
void uiSleepDisplay();
