#ifndef ATTENDANCE_MANAGER_H
#define ATTENDANCE_MANAGER_H

#include <Arduino.h>

enum AttendanceMethod {

    ATTENDANCE_DEVICE,
    ATTENDANCE_FINGERPRINT,
    ATTENDANCE_BOTH
};

bool attendance_manager_init();

// rssi is stored as TEXT in every record so the field never changes type:
//   fingerprint  -> "biometric"
//   anything else -> the number as text, for example "-62"
bool attendance_mark(
    const String& studentId,
    const String& sessionId,
    AttendanceMethod method,
    int rssi
);

bool attendance_alreadyMarked(
    const String& studentId,
    const String& sessionId
);

String attendance_getSessionJSON(
    const String& sessionId
);

String attendance_getAllJSON(
    const String& sessionId = ""
);

// Writes this session's records as JSON objects separated by ",\n"
// (no surrounding brackets). Returns the number of records written.
// Used by the SD card writer so no large JSON buffer is needed.
int attendance_writeSessionRecords(
    const String& sessionId,
    Print& out
);

// Frees RAM by clearing old records once the buffer is more than
// 60% full. Only call this when no session is active.
void attendance_compactIfNeeded();

#endif
