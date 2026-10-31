#ifndef API_SERVER_H
#define API_SERVER_H

#include <Arduino.h>

bool api_server_init();

void api_server_handle();

// Re-opens the HTTP server after the Wi-Fi mode was switched.
void api_server_restart();

#endif