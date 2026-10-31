#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include <Arduino.h>
#include <WiFi.h>

void wifi_init();

bool wifi_isConnected();

IPAddress wifi_getIP();

IPAddress wifi_getAPIP();

String wifi_getSSID();

int wifi_getRSSI();

int wifi_getStationRSSI(IPAddress ip);

#endif

