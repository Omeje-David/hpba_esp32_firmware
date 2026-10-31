#include "device_guard.h"
#include "config.h"

#include <string.h>
#include <esp_wifi.h>

#if __has_include("esp_wifi_ap_get_sta_list.h")
#include "esp_wifi_ap_get_sta_list.h"
#define HPBA_HAS_STA_IP_LIST 1
#endif

// esp_wifi_ap_get_sta_aid() (needed to disconnect ONE phone) exists from
// ESP-IDF 4.4, which is Arduino-ESP32 core 2.0.0 and newer.
#if SIGNED_CLIENT_KICK_ENABLED && __has_include("esp_idf_version.h")
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#define HPBA_HAS_STA_AID 1
#endif
#endif

struct GuardEntry {

    bool hasMac;
    uint8_t mac[6];

    String token;
    String studentId;

    uint32_t signedAt;   // millis() when this phone signed in
    uint32_t lastKick;   // millis() of the last disconnect, 0 = never
};

static GuardEntry entries[DEVICE_GUARD_MAX_ENTRIES];
static int entryCount = 0;


// ------------------------------------------------------------
// Identify the phone behind a TCP connection
// ------------------------------------------------------------

ClientInfo device_guard_identify(IPAddress clientIp) {

    ClientInfo info;
    memset(&info, 0, sizeof(info));

    wifi_sta_list_t staList;
    memset(&staList, 0, sizeof(staList));

    if (
        esp_wifi_ap_get_sta_list(&staList) != ESP_OK ||
        staList.num <= 0
    ) {
        return info;
    }

#ifdef HPBA_HAS_STA_IP_LIST

    // Match the client's IP address to the MAC address the DHCP
    // server leased it to.
    wifi_sta_mac_ip_list_t macIpList;
    memset(&macIpList, 0, sizeof(macIpList));

    if (esp_wifi_ap_get_sta_list_with_ip(&staList, &macIpList) == ESP_OK) {

        uint32_t wanted = (uint32_t)clientIp;

        for (int i = 0; i < macIpList.num; i++) {

            if (macIpList.sta[i].ip.addr == wanted) {

                memcpy(info.mac, macIpList.sta[i].mac, 6);
                info.found = true;
                break;
            }
        }
    }

#endif

    // Only one phone connected: it must be this one.
    if (!info.found && staList.num == 1) {

        memcpy(info.mac, staList.sta[0].mac, 6);
        info.found = true;
    }

    if (info.found) {

        for (int i = 0; i < staList.num; i++) {

            if (memcmp(staList.sta[i].mac, info.mac, 6) == 0) {

                info.rssi = staList.sta[i].rssi;
                break;
            }
        }
    }

    return info;
}

String device_guard_macString(const ClientInfo& info) {

    if (!info.found) return "unknown";

    char buf[18];

    snprintf(
        buf,
        sizeof(buf),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        info.mac[0], info.mac[1], info.mac[2],
        info.mac[3], info.mac[4], info.mac[5]
    );

    return String(buf);
}


// ------------------------------------------------------------
// Duplicate detection
// ------------------------------------------------------------

bool device_guard_isDuplicate(
    const ClientInfo& info,
    const String& token,
    String& signedStudent
) {

    for (int i = 0; i < entryCount; i++) {

        bool sameMac =
            info.found &&
            entries[i].hasMac &&
            memcmp(entries[i].mac, info.mac, 6) == 0;

        bool sameToken =
            token.length() > 0 &&
            entries[i].token == token;

        if (sameMac || sameToken) {

            signedStudent = entries[i].studentId;
            return true;
        }
    }

    return false;
}

void device_guard_record(
    const ClientInfo& info,
    const String& token,
    const String& studentId
) {

    if (!info.found && token.length() == 0) {

        Serial.println(
            "[GUARD] No MAC or token for this sign-in. "
            "It could not be recorded."
        );

        return;
    }

    if (entryCount >= DEVICE_GUARD_MAX_ENTRIES) {

        Serial.println("[GUARD] Device list is full");
        return;
    }

    GuardEntry& e = entries[entryCount++];

    e.hasMac = info.found;

    if (info.found) {
        memcpy(e.mac, info.mac, 6);
    } else {
        memset(e.mac, 0, 6);
    }

    e.token = token;
    e.studentId = studentId;

    e.signedAt = millis();
    e.lastKick = 0;

    Serial.print("[GUARD] Recorded device ");
    Serial.print(device_guard_macString(info));
    Serial.print(" for ");
    Serial.println(studentId);
}

void device_guard_reset() {

    for (int i = 0; i < entryCount; i++) {

        entries[i].token = "";
        entries[i].studentId = "";
        entries[i].hasMac = false;
        entries[i].signedAt = 0;
        entries[i].lastKick = 0;
    }

    entryCount = 0;

    Serial.println("[GUARD] Device list cleared for the new session");
}

int device_guard_count() {
    return entryCount;
}


// ------------------------------------------------------------
// Random (private) MAC addresses
// ------------------------------------------------------------

bool device_guard_isRandomMac(const ClientInfo& info) {

    if (!info.found) return false;

    // Bit 1 of the first byte = "locally administered". Phones set it on
    // the random addresses they invent per Wi-Fi network.
    return (info.mac[0] & 0x02) != 0;
}


// ------------------------------------------------------------
// Free the Wi-Fi slots of phones that already signed
// ------------------------------------------------------------

bool device_guard_kickSupported() {

#ifdef HPBA_HAS_STA_AID
    return true;
#else
    return false;
#endif
}

void device_guard_releaseSignedClients() {

#ifdef HPBA_HAS_STA_AID

    if (entryCount == 0) return;

    wifi_sta_list_t staList;
    memset(&staList, 0, sizeof(staList));

    if (
        esp_wifi_ap_get_sta_list(&staList) != ESP_OK ||
        staList.num <= 0
    ) {
        return;
    }

    uint32_t now = millis();

    for (int i = 0; i < entryCount; i++) {

        GuardEntry& e = entries[i];

        // Only phones whose MAC is known can be singled out.
        if (!e.hasMac) continue;

        // Give the student time to read the confirmation first.
        if (now - e.signedAt < SIGNED_CLIENT_KICK_DELAY_MS) continue;

        // A phone that reconnects on its own is removed again, but not
        // in a tight loop.
        if (
            e.lastKick != 0 &&
            now - e.lastKick < SIGNED_CLIENT_KICK_RETRY_MS
        ) {
            continue;
        }

        bool connected = false;

        for (int s = 0; s < staList.num; s++) {

            if (memcmp(staList.sta[s].mac, e.mac, 6) == 0) {

                connected = true;
                break;
            }
        }

        if (!connected) continue;

        uint16_t aid = 0;

        if (
            esp_wifi_ap_get_sta_aid(e.mac, &aid) != ESP_OK ||
            aid == 0
        ) {
            continue;
        }

        if (esp_wifi_deauth_sta(aid) == ESP_OK) {

            e.lastKick = (now == 0) ? 1 : now;

            ClientInfo info;
            memset(&info, 0, sizeof(info));
            memcpy(info.mac, e.mac, 6);
            info.found = true;

            Serial.print("[GUARD] Disconnected signed phone ");
            Serial.print(device_guard_macString(info));
            Serial.print(" (");
            Serial.print(e.studentId);
            Serial.println(") to free its Wi-Fi slot");
        }
    }

#endif
}
