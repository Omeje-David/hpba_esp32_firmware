#ifndef FIREBASE_SYNC_H
#define FIREBASE_SYNC_H

#include <Arduino.h>

// ============================================================
// FIREBASE SYNC (emergency sessions only)
//
// Sessions started by an admin fingerprint have no course. After such
// a session ends they are uploaded to the "hardware_logs" collection,
// tagged with the admin's Firebase UID. The admin then sees them in the
// app's Action Center and adds them to one of their courses.
//
// Sequence (shown on the OLED, "Do not power off"):
//   1. Stop the student Wi-Fi, join the phone hotspot.
//   2. Sign in to Firebase as the ESP32 account (also sets the clock).
//   3. Upload every session on the SD card that has no .OK marker.
//   4. Leave the hotspot and restart the student Wi-Fi.
//
// Nothing is deleted. If the hotspot or Firebase is unavailable the
// files stay on the SD card and are retried after the next session.
// ============================================================

// Returns true when everything pending was uploaded (or nothing was pending).
bool cloud_syncEmergencyQueue();

// Number of emergency sessions on the SD card not yet uploaded.
int cloud_pendingCount();

#endif
