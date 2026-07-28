#include "wifi_connect.h"

#include <WiFi.h>

#include "config.h"

bool wifiConnectKnownNetworks(char* connectedSsid, size_t ssidLen,
                              uint32_t perNetworkTimeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(true);

  for (size_t i = 0; i < WIFI_NETWORK_COUNT; ++i) {
    const WifiCred& net = WIFI_NETWORKS[i];
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.begin(net.ssid, net.pass);

    const uint32_t start = millis();
    while (millis() - start < perNetworkTimeoutMs) {
      const wl_status_t st = WiFi.status();
      if (st == WL_CONNECTED) {
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
