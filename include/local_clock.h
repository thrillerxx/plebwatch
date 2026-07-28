#pragma once

#include <stddef.h>
#include <time.h>

// Restore timezone + system time from hardware RTC (survives deep sleep).
void localClockBegin();

// Apply a POSIX TZ string and remember it across sleep / power cycles.
// Rejects UTC / empty — use fallbackTz (or PLEBWATCH_TZ) instead.
bool localClockSetTimezone(const char* posixTz, const char* fallbackTz = nullptr);

// Currently active POSIX TZ (never null; never UTC0 unless configured so).
const char* localClockTz();

// True if the string is UTC / GMT with no local offset.
bool localClockIsUtcTz(const char* posixTz);

// After NTP sync: write UTC into the BM8563 so time survives sleep.
void localClockCommitRtc();

// Set ESP32 UTC clock from a unix timestamp (NTP / HTTP time APIs).
bool localClockSetUnixUtc(time_t unixUtc);

bool localClockReadTm(struct tm* out);
void localClockFormatHm(char* buf, size_t len);
