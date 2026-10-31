#ifndef ESP_CLOCK_H
#define ESP_CLOCK_H

#include <Arduino.h>

// ============================================================
// ESP32 CLOCK (no RTC fitted)
//
// Time comes from, in order of trust:
//   1. The phone, when the app starts a session (precise).
//   2. The Firebase server "Date" header after a sync (precise).
//   3. The last time saved on the SD card at power-up (approximate:
//      it is the time of the last sync, plus the time since boot).
//
// Sessions recorded while the clock is only approximate are
// corrected at upload time when the ESP32 has not rebooted since.
// ============================================================

// Loads the last saved time from the SD card. Call after sd_init().
void clock_init();

// Call from loop(). Saves the time to the SD card every 10 minutes.
void clock_tick();

// Sets a precise time (UTC epoch seconds).
void clock_setEpoch(unsigned long epoch);

// httpDate example: "Thu, 29 Oct 2026 14:30:00 GMT"
bool clock_setFromHttpDate(const String& httpDate);

bool clock_isSet();

// True when the time has not been confirmed since this boot.
bool clock_isApprox();

// UTC epoch seconds, or 0 if there is no time at all.
unsigned long clock_now();

// Epoch seconds for a past millis() value of this boot (0 if no time).
unsigned long clock_epochAtMillis(unsigned long ms);

// Random id for this power-up, used to know if a session file was
// written during the current boot.
String clock_bootId();

// "2026-10-29T13:30:00Z"
String clock_isoUtc(unsigned long epoch);

// Local time as "20261029_1430".
String clock_localStamp(unsigned long epoch);

#endif
