#include "captive_portal.h"
#include "config.h"
#include "wifi_module.h"

#include <DNSServer.h>

static DNSServer dnsServer;
static bool dnsReady = false;

bool captive_portal_init() {

    Serial.println();
    Serial.println("========================================");
    Serial.println(" HPBA - STUDENT CAPTIVE PORTAL");
    Serial.println("========================================");

    IPAddress apIP = wifi_getAPIP();

    // Safe to call again after the access point is restarted.
    dnsServer.stop();

    if (dnsServer.start(DNS_PORT, "*", apIP)) {
        dnsReady = true;

        Serial.print("[PORTAL] DNS wildcard redirect: ");
        Serial.println(apIP);

        Serial.println("[PORTAL] Captive portal DNS ready");
    } else {
        dnsReady = false;
        Serial.println("[PORTAL] ERROR: DNS server failed to start");
    }

    Serial.print("[PORTAL] Student Wi-Fi: ");
    Serial.println(AP_SSID);

    Serial.print("[PORTAL] Portal URL: http://");
    Serial.print(apIP);
    Serial.println("/");

    return dnsReady;
}

void captive_portal_handle() {

    if (!dnsReady) return;

    dnsServer.processNextRequest();
}
