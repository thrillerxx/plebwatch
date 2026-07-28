#pragma once

#include "metrics.h"

void uiBegin();
void uiBootSplash();
void uiShowStatus(const char* line1, const char* line2 = nullptr);
void uiDrawPage(Page page, const Metrics& m, uint8_t batteryPct, bool charging);
void uiEnsureLandscape();
bool uiIsWatchPage(Page page);
void uiUpdateHeaderClockIfNeeded(uint8_t batteryPct, bool charging);
void uiSleepDisplay();
