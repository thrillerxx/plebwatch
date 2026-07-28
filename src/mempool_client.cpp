#include "mempool_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "local_clock.h"

namespace {

WiFiClientSecure gClient;
WiFiClient gPlainClient;
HTTPClient gHttp;

bool httpGet(const char* url, String& body, uint32_t timeoutMs = 12000) {
  gHttp.setTimeout(timeoutMs);
  gHttp.setReuse(true);
  if (!gHttp.begin(gClient, url)) {
    return false;
  }
  const int code = gHttp.GET();
  if (code != HTTP_CODE_OK) {
    gHttp.end();
    return false;
  }
  body = gHttp.getString();
  gHttp.end();
  return true;
}

bool httpGetPlain(const char* url, String& body, uint32_t timeoutMs = 8000) {
  gHttp.setTimeout(timeoutMs);
  gHttp.setReuse(false);
  if (!gHttp.begin(gPlainClient, url)) {
    return false;
  }
  const int code = gHttp.GET();
  if (code != HTTP_CODE_OK) {
    gHttp.end();
    return false;
  }
  body = gHttp.getString();
  gHttp.end();
  return true;
}

// Map common IANA zones to POSIX (DST-aware). ESP32 has no zoneinfo DB.
bool applyIanaTimezone(const char* iana) {
  if (!iana || !iana[0]) {
    return false;
  }
  struct Map {
    const char* iana;
    const char* posix;
  };
  static const Map kMaps[] = {
      {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
      {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
      {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
      {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
      {"America/Phoenix", "MST7"},
      {"UTC", "UTC0"},
      {"Etc/UTC", "UTC0"},
  };
  for (const auto& m : kMaps) {
    if (strcmp(iana, m.iana) == 0) {
      Serial.printf("timezone from geo-IP IANA %s\n", iana);
      localClockSetTimezone(m.posix);
      return true;
    }
  }
  return false;
}

// ESP32 localtime uses POSIX TZ. API offsets are seconds east of UTC;
// POSIX wants the hours to *add* to local time to get UTC (sign flipped).
void applyPosixOffset(long offsetEastSeconds) {
  long posix = -offsetEastSeconds;  // seconds west of UTC
  const bool west = posix >= 0;
  if (posix < 0) {
    posix = -posix;
  }
  const long hours = posix / 3600;
  const long mins = (posix % 3600) / 60;
  char tz[48];
  if (west) {
    if (mins == 0) {
      snprintf(tz, sizeof(tz), "<+%02ld>%ld", hours, hours);
    } else {
      snprintf(tz, sizeof(tz), "<+%02ld:%02ld>%ld:%02ld", hours, mins, hours,
               mins);
    }
  } else if (mins == 0) {
    snprintf(tz, sizeof(tz), "<-%02ld>-%ld", hours, hours);
  } else {
    snprintf(tz, sizeof(tz), "<-%02ld:%02ld>-%ld:%02ld", hours, mins, hours,
             mins);
  }
  // Examples: offset -18000 → <+05>5   |  offset +7200 → <-02>-2
  Serial.printf("timezone from geo-IP offset %+ld s -> %s\n", offsetEastSeconds,
                tz);
  localClockSetTimezone(tz);
}

bool detectTimezoneFromIp() {
  String body;
  // ip-api.com: free, HTTP, timezone from the Wi‑Fi's public IP.
  if (httpGetPlain("http://ip-api.com/json/?fields=status,offset,timezone",
                   body, 8000)) {
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok &&
        strcmp(doc["status"] | "", "success") == 0) {
      const char* iana = doc["timezone"] | "";
      if (applyIanaTimezone(iana)) {
        return true;
      }
      if (!doc["offset"].isNull()) {
        applyPosixOffset(doc["offset"].as<long>());
        return true;
      }
    }
  }

  // Fallback: worldtimeapi (offset + unix time).
  body = "";
  if (httpGetPlain("http://worldtimeapi.org/api/ip", body, 8000)) {
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      const char* iana = doc["timezone"] | "";
      if (!applyIanaTimezone(iana)) {
        const long raw = doc["raw_offset"] | 0L;
        const long dst = doc["dst_offset"] | 0L;
        if (doc["raw_offset"].isNull() && doc["dst_offset"].isNull()) {
          return false;
        }
        applyPosixOffset(raw + dst);
      }
      const time_t unixUtc = doc["unixtime"] | 0L;
      if (unixUtc > 1700000000) {
        localClockSetUnixUtc(unixUtc);
        Serial.println("clock seeded from worldtimeapi");
      }
      return true;
    }
  }
  return false;
}

bool pullTimeFromHttp() {
  String body;
  if (!httpGetPlain("http://worldtimeapi.org/api/ip", body, 8000)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return false;
  }
  const time_t unixUtc = doc["unixtime"] | 0L;
  if (!localClockSetUnixUtc(unixUtc)) {
    return false;
  }
  Serial.printf("clock pulled via HTTP unixtime=%ld\n",
                static_cast<long>(unixUtc));
  return true;
}

bool waitForNtpSync(uint32_t timeoutMs = 10000) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      Serial.println("clock pulled via NTP");
      return true;
    }
    delay(200);
  }
  return false;
}

double jsonNumberAfterKey(const String& body, const char* key) {
  const String needle = String("\"") + key + "\":";
  const int idx = body.indexOf(needle);
  if (idx < 0) {
    return 0.0;
  }
  return body.substring(idx + needle.length()).toDouble();
}

void formatHalvingDate(uint32_t blocksToHalving, float avgBlockMinutes,
                       char* out, size_t outLen) {
  if (outLen == 0) {
    return;
  }
  if (avgBlockMinutes <= 0.1f) {
    avgBlockMinutes = 10.0f;
  }
  const double seconds =
      static_cast<double>(blocksToHalving) * avgBlockMinutes * 60.0;
  time_t now = time(nullptr);
  if (now < 1700000000) {
    snprintf(out, outLen, "~%ud", static_cast<unsigned>(seconds / 86400.0));
    return;
  }
  time_t target = now + static_cast<time_t>(seconds);
  struct tm tmInfo;
  gmtime_r(&target, &tmInfo);
  strftime(out, outLen, "%Y-%m-%d", &tmInfo);
}

bool fetchPrices(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/v1/prices", body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  m.priceUsd = doc["USD"] | 0.0f;
  if (m.priceUsd > 0.0f) {
    m.satsPerDollar = static_cast<uint32_t>(lroundf(1.0e8f / m.priceUsd));
  }
  return m.priceUsd > 0.0f;
}

bool fetchHeight(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/blocks/tip/height", body)) {
    return false;
  }
  m.blockHeight = static_cast<uint32_t>(body.toInt());
  return m.blockHeight > 0;
}

bool fetchTipTimestamp(Metrics& m) {
  String hashBody;
  if (!httpGet("https://mempool.space/api/blocks/tip/hash", hashBody)) {
    return false;
  }
  hashBody.trim();
  String url = String("https://mempool.space/api/block/") + hashBody;
  String body;
  if (!httpGet(url.c_str(), body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  m.tipTimestamp = doc["timestamp"] | 0;
  return m.tipTimestamp > 0;
}

bool fetchFees(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/v1/fees/recommended", body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  m.feeImmediate = doc["fastestFee"] | 0;
  m.feeHour = doc["hourFee"] | 0;
  m.feeDay = doc["economyFee"] | 0;
  m.feeWeek = doc["minimumFee"] | doc["economyFee"] | 0;
  return true;
}

bool fetchMempool(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/mempool", body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  m.mempoolTxCount = doc["count"] | 0;
  m.mempoolMinFee = doc["min_fee"] | 0.0f;
  return true;
}

bool fetchDifficulty(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/v1/difficulty-adjustment", body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  m.blocksToRetarget = doc["remainingBlocks"] | 0;
  m.difficultyChangePct = doc["difficultyChange"] | 0.0f;
  m.previousRetargetPct = doc["previousRetarget"] | 0.0f;
  m.remainingTimeDays = (doc["remainingTime"] | 0.0) / 86400000.0;
  m.timeAvgMinutes = (doc["timeAvg"] | 600000.0) / 60000.0;
  if (doc["difficulty"].is<double>() || doc["difficulty"].is<long>()) {
    m.difficulty = doc["difficulty"] | 0.0;
  }
  return true;
}

bool fetchHashrate(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/v1/mining/hashrate/3d", body, 15000)) {
    return false;
  }
  // Response includes a large history array; pull scalars by key scan.
  const double hr = jsonNumberAfterKey(body, "currentHashrate");
  const double diff = jsonNumberAfterKey(body, "currentDifficulty");
  if (hr > 0.0) {
    m.hashrateEhs = hr / 1.0e18;
  }
  if (diff > 0.0) {
    m.difficulty = diff;
  }
  return m.hashrateEhs > 0.0;
}

void computeHalving(Metrics& m) {
  const uint32_t epochLen = 210000;
  const uint32_t height = m.blockHeight;
  m.subsidyEpoch = static_cast<uint8_t>(height / epochLen);
  const uint32_t intoEpoch = height % epochLen;
  m.blocksToHalving = epochLen - intoEpoch;
  m.subsidyBtc = 50.0f;
  for (uint8_t i = 0; i < m.subsidyEpoch && i < 64; ++i) {
    m.subsidyBtc *= 0.5f;
  }
  formatHalvingDate(m.blocksToHalving,
                    m.timeAvgMinutes > 0 ? m.timeAvgMinutes : 10.0f,
                    m.halvingEstimate, sizeof(m.halvingEstimate));
}

bool fetchLightning(Metrics& m) {
  String body;
  if (!httpGet("https://mempool.space/api/v1/lightning/statistics/latest",
               body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  JsonObject latest = doc["latest"].as<JsonObject>();
  if (latest.isNull()) {
    latest = doc.as<JsonObject>();
  }
  m.lnNodes = latest["node_count"] | latest["nodeCount"] | 0;
  m.lnChannels = latest["channel_count"] | latest["channelCount"] | 0;
  const uint64_t capacitySats =
      latest["total_capacity"] | latest["totalCapacity"] | 0ULL;
  m.lnCapacityBtc = static_cast<float>(capacitySats) / 1.0e8f;
  m.lnCapacityUsd = m.lnCapacityBtc * m.priceUsd;
  return m.lnNodes > 0 || m.lnChannels > 0;
}

bool fetchLnTop(Metrics& m) {
  String body;
  if (!httpGet(
          "https://mempool.space/api/v1/lightning/nodes/rankings/liquidity",
          body)) {
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return false;
  }
  JsonArray arr = doc.as<JsonArray>();
  if (arr.isNull() && doc["nodes"].is<JsonArray>()) {
    arr = doc["nodes"].as<JsonArray>();
  }
  m.lnTopCount = 0;
  for (JsonObject node : arr) {
    if (m.lnTopCount >= 5) {
      break;
    }
    const char* alias = node["alias"] | node["name"] | "?";
    strncpy(m.lnTop[m.lnTopCount].alias, alias, sizeof(m.lnTop[0].alias) - 1);
    m.lnTop[m.lnTopCount].alias[sizeof(m.lnTop[0].alias) - 1] = '\0';
    const uint64_t cap =
        node["capacity"] | node["totalCapacity"] | node["liquidity"] | 0ULL;
    m.lnTop[m.lnTopCount].capacityBtc = static_cast<float>(cap) / 1.0e8f;
    m.lnTopCount++;
  }
  return m.lnTopCount > 0;
}

bool fetchNodeVersions(Metrics& m) {
  String body;
  if (!httpGet("https://bitnodes.io/api/v1/snapshots/latest/", body, 8000)) {
    return false;
  }

  const int totalIdx = body.indexOf("\"total_nodes\":");
  if (totalIdx >= 0) {
    m.reachableNodes =
        static_cast<uint32_t>(body.substring(totalIdx + 14).toInt());
  }

  struct Count {
    char ua[28];
    uint16_t n;
  };
  Count counts[8] = {};
  uint8_t used = 0;
  const int limit = min(static_cast<int>(body.length()), 48000);
  int pos = 0;
  while (pos < limit) {
    const int uaPos = body.indexOf("/Satoshi:", pos);
    if (uaPos < 0 || uaPos > limit) {
      break;
    }
    int end = body.indexOf('/', uaPos + 1);
    if (end < 0) {
      end = uaPos + 24;
    }
    end = min(end, uaPos + 26);
    char ua[28];
    const int len = min(end - uaPos + 1, 27);
    strncpy(ua, body.c_str() + uaPos, len);
    ua[len] = '\0';

    int found = -1;
    for (uint8_t i = 0; i < used; ++i) {
      if (strncmp(counts[i].ua, ua, sizeof(ua)) == 0) {
        found = i;
        break;
      }
    }
    if (found >= 0) {
      counts[found].n++;
    } else if (used < 8) {
      strncpy(counts[used].ua, ua, sizeof(counts[0].ua) - 1);
      counts[used].n = 1;
      used++;
    }
    pos = uaPos + 8;
  }

  for (uint8_t i = 0; i < used; ++i) {
    for (uint8_t j = i + 1; j < used; ++j) {
      if (counts[j].n > counts[i].n) {
        Count tmp = counts[i];
        counts[i] = counts[j];
        counts[j] = tmp;
      }
    }
  }

  uint32_t sampleTotal = 0;
  for (uint8_t i = 0; i < used; ++i) {
    sampleTotal += counts[i].n;
  }
  m.versionCount = 0;
  for (uint8_t i = 0; i < used && m.versionCount < 5; ++i) {
    strncpy(m.versions[m.versionCount].name, counts[i].ua,
            sizeof(m.versions[0].name) - 1);
    m.versions[m.versionCount].pct =
        sampleTotal ? (100.0f * counts[i].n / sampleTotal) : 0.0f;
    m.versionCount++;
  }
  return m.versionCount > 0 || m.reachableNodes > 0;
}

}  // namespace

bool syncNetworkTime() {
  // 1) Timezone from Wi‑Fi public IP (config TZ fallback).
  if (!detectTimezoneFromIp()) {
    localClockSetTimezone(PLEBWATCH_TZ);
    Serial.printf("timezone fallback: %s\n", PLEBWATCH_TZ);
  }

  // 2) Pull UTC via NTP. IMPORTANT: use configTzTime — configTime(0,0,...)
  //    overwrites TZ to UTC on ESP32 Arduino, which made CST show as 02:xx.
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(localClockTz(), "pool.ntp.org", "time.nist.gov",
               "time.google.com");

  bool ok = waitForNtpSync(12000);
  if (!ok) {
    Serial.println("NTP timeout — trying HTTP time");
    ok = pullTimeFromHttp();
  }

  // Re-assert TZ after SNTP (some IDF builds reset it).
  localClockSetTimezone(localClockTz());

  // 3) Persist whatever we got so deep sleep keeps the clock.
  if (ok || time(nullptr) > 1700000000) {
    localClockCommitRtc();
    struct tm localTm = {};
    if (localClockReadTm(&localTm)) {
      Serial.printf("local clock now %02d:%02d TZ=%s\n", localTm.tm_hour,
                    localTm.tm_min, localClockTz());
    }
  }
  return ok || time(nullptr) > 1700000000;
}

bool fetchAllMetrics(Metrics& out) {
  Metrics m = {};
  gClient.setInsecure();
  gClient.setTimeout(12);

  const bool pricesOk = fetchPrices(m);
  const bool heightOk = fetchHeight(m);
  fetchTipTimestamp(m);
  fetchFees(m);
  fetchMempool(m);
  fetchDifficulty(m);
  fetchHashrate(m);
  if (heightOk) {
    computeHalving(m);
  }
  fetchLightning(m);
  fetchLnTop(m);
  fetchNodeVersions(m);

  m.valid = pricesOk || heightOk;
  m.fetchedAtMs = millis();
  out = m;
  return m.valid;
}
