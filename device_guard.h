#ifndef DEVICE_GUARD_H
#define DEVICE_GUARD_H

#include <Arduino.h>
#include <IPAddress.h>

// ============================================================
// DEVICE GUARD
//
// One physical device may sign only once per session, even if it
// disconnects and reconnects. Two independent identifiers are
// remembered for every sign-in:
//
//   1. The Wi-Fi MAC address, read from the access point's own
//      station table (the student cannot edit this from the browser).
//   2. The browser token (the "deviceId" the portal page keeps in
//      localStorage).
//
// A new sign-in is rejected when EITHER identifier was already used
// in this session. Records are cleared when a new session starts.
// ============================================================

struct ClientInfo {

    bool found;        // true when the MAC address could be identified
    uint8_t mac[6];
    int rssi;          // signal strength measured by the access point, 0 if unknown
};

// Finds the MAC address and RSSI of the phone that owns this IP.
ClientInfo device_guard_identify(IPAddress clientIp);

String device_guard_macString(const ClientInfo& info);

// True when the MAC is a random (private) address. Phones generate these
// per network; they set the "locally administered" bit (0x02) of the
// first byte, which a real manufacturer address never has.
bool device_guard_isRandomMac(const ClientInfo& info);

// True when this firmware build can disconnect a single phone.
bool device_guard_kickSupported();

// Call about once a second while a session is running. Disconnects every
// phone that signed in more than SIGNED_CLIENT_KICK_DELAY_MS ago and is
// still connected, to free its Wi-Fi slot.
void device_guard_releaseSignedClients();

// True when this phone (by MAC or token) already signed in this session.
// signedStudent receives the registration number it signed for.
bool device_guard_isDuplicate(
    const ClientInfo& info,
    const String& token,
    String& signedStudent
);

void device_guard_record(
    const ClientInfo& info,
    const String& token,
    const String& studentId
);

// Forget everything. Called whenever a new session starts.
void device_guard_reset();

int device_guard_count();

#endif
