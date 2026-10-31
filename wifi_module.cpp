#include "wifi_module.h"
#include "config.h"

#include <WiFi.h>

// ============================================================
// HPBA WIFI MODULE
//
// Current architecture:
//
//   Phone
//      |
//      | Wi-Fi
//      v
//   Attendance-SignIn
//      |
//      v
//   ESP32
//   192.168.4.1
//
// The TP-Link router / STA network is intentionally NOT used
// for the local attendance connection.
//
// This makes the ESP32 AP the single local network used by:
//   - Student captive portal
//   - Student attendance
//   - Flutter lecturer app
// ============================================================

void wifi_init() {

    Serial.println();
    Serial.println("========================================");
    Serial.println(" HPBA - WIFI INITIALIZATION");
    Serial.println("========================================");

    // --------------------------------------------------------
    // AP ONLY
    // --------------------------------------------------------

    WiFi.mode(WIFI_AP);

    // Keep Wi-Fi awake for reliable local communication.
    WiFi.setSleep(false);

    IPAddress apIP(
        192, 168, 4, 1
    );

    IPAddress apGateway(
        192, 168, 4, 1
    );

    IPAddress apSubnet(
        255, 255, 255, 0
    );

    if (!WiFi.softAPConfig(
            apIP,
            apGateway,
            apSubnet
        )) {

        Serial.println(
            "[WiFi] ERROR: AP configuration failed"
        );

    } else {

        Serial.println(
            "[WiFi] AP configuration successful"
        );
    }

    // --------------------------------------------------------
    // START ACCESS POINT
    // --------------------------------------------------------

    if (WiFi.softAP(
            AP_SSID,
            AP_PASSWORD,
            1,      // channel
            false,  // visible SSID
            8       // maximum clients
        )) {

        Serial.println(
            "[WiFi] ESP32 Access Point started"
        );

    } else {

        Serial.println(
            "[WiFi] ERROR: Access Point failed"
        );

        return;
    }

    // Give the Wi-Fi stack a moment to establish the AP.
    delay(200);

    // --------------------------------------------------------
    // FINAL STATUS
    // --------------------------------------------------------

    Serial.println();
    Serial.println("----------------------------------------");

    Serial.print(
        "[WiFi] SSID: "
    );

    Serial.println(
        AP_SSID
    );

    Serial.print(
        "[WiFi] AP IP: "
    );

    Serial.println(
        WiFi.softAPIP()
    );

    Serial.print(
        "[WiFi] Gateway: "
    );

    Serial.println(
        WiFi.softAPIP()
    );

    Serial.print(
        "[WiFi] Connected clients: "
    );

    Serial.println(
        WiFi.softAPgetStationNum()
    );

    Serial.println(
        "[WiFi] AP-only mode"
    );

    Serial.println(
        "[WiFi] Direct phone <-> ESP32 communication enabled"
    );

    Serial.println(
        "----------------------------------------"
    );

    Serial.println();
}


// ============================================================
// AP CONNECTION STATUS
//
// In AP-only mode this means the ESP32 AP is running.
// ============================================================

bool wifi_isConnected() {

    return WiFi.getMode() == WIFI_AP;
}


// ============================================================
// AP IP
//
// wifi_getIP() is kept for compatibility with the rest of
// the HPBA code.
//
// It now returns the ESP32 AP address.
// ============================================================

IPAddress wifi_getIP() {

    return WiFi.softAPIP();
}


// ============================================================
// AP IP
// ============================================================

IPAddress wifi_getAPIP() {

    return WiFi.softAPIP();
}


// ============================================================
// SSID
// ============================================================

String wifi_getSSID() {

    return String(AP_SSID);
}


// ============================================================
// RSSI
//
// The ESP32 itself does not have an STA RSSI in AP-only mode.
// Return 0 because there is no router connection.
// ============================================================

int wifi_getRSSI() {

    return 0;
}


// ============================================================
// STUDENT DEVICE RSSI
//
// The ESP32 Arduino Wi-Fi API does not provide a simple
// IP -> RSSI lookup through the WebServer client.
//
// Keep the function for compatibility with the attendance
// system.
//
// Attendance does NOT depend on RSSI.
// ============================================================

int wifi_getStationRSSI(IPAddress ip) {

    (void)ip;

    return 0;
}

