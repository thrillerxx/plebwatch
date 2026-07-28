#pragma once

#include "metrics.h"

// Geo-IP timezone + pull UTC from NTP (HTTP fallback). Call after Wi‑Fi joins.
bool syncNetworkTime();

bool fetchAllMetrics(Metrics& out);
