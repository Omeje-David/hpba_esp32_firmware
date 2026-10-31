#ifndef SESSION_STORE_H
#define SESSION_STORE_H

#include <Arduino.h>
#include "session_manager.h"

// ============================================================
// SESSION STORE (SD card)
//
// Every finished session is written to the SD card, quietly.
// A failure here is logged on Serial only and never affects the
// attendance session or the app.
//
//   App-started session:
//     /COURSES/<CODE>/<CODE>_<YYYYMMDD>_<HHMM>.JSN
//   Fingerprint-started (emergency) session:
//     /EMERG/EMERGENCY_<YYYYMMDD>_<HHMM>.JSN
//
// If the same name already exists, _2, _3 ... is added.
// ============================================================

// Creates the folders. Call after sd_init().
void session_store_init();

// Called by session_end().
bool session_store_saveCompleted(
    const AttendanceSession& session,
    unsigned long endEpoch,
    unsigned long endMillis
);

// Name (without extension) of the file written last, "" if none.
String session_store_lastName();

#endif
