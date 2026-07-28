#include "wifi_connect.h"

#include <WiFi.h>

#include "config.h"

bool wifiConnectKnownNetworks(char* connectedSsid, size_t ssidLen,
                              uint32_t perNetworkTimeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(true);

  // Prefer a network that is currently visible (home vs hackerspace).
  const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  int order[8];
  size_t orderCount = 0;
  for (size_t i = 0; i < WIFI_NETWORK_COUNT && orderCount < 8; ++i) {
    bool seen = (n <= 0);  // if scan failed, try all in listed order
    for (int s = 0; s < n; ++s) {
      if (WiFi.SSID(s) == WIFI_NETWORKS[i].ssid) {
        seen = true;
        break;
      }
    }
    if (seen) {
      order[orderCount++] = static_cast<int>(i);
    }
  }
  // Append any not already queued
  for (size_t i = 0; i < WIFI_NETWORK_COUNT && orderCount < 8; ++i) {
    bool already = false;
    for (size_t j = 0; j < orderCount; ++j) {
      if (order[j] == static_cast<int>(i)) {
        already = true;
        break;
      }
    }
    if (!already) {
      order[orderCount++] = static_cast<int>(i);
    }
  }

  for (size_t oi = 0; oi < orderCount; ++oi) {
    const WifiCred& net = WIFI_NETWORKS[order[oi]];
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.begin(net.ssid, net.pass);

    const uint32_t start = millis();
    while (millis() - start < perNetworkTimeoutMs) {
      if (WiFi.status() == WL_CONNECTED) {
        if (connectedSsid && ssidLen > 0) {
          strncpy(connectedSsid, net.ssid, ssidLen - 1);
          connectedSsid[ssidLen - 1] = '\0';
        }
        return true;
      }
      delay(150);
    }
  }

  if (connectedSsid && ssidLen > 0) {
    connectedSsid[0] = '\0';
  }
  return false;
}

void wifiDisconnectFull() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}
