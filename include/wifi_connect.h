#pragma once

#include <Arduino.h>

bool wifiConnectKnownNetworks(char* connectedSsid, size_t ssidLen,
                              uint32_t perNetworkTimeoutMs = 8000);
void wifiDisconnectFull();
