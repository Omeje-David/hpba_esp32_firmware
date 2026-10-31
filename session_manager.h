#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <Arduino.h>

// ============================================================
// SESSION MODEL
// ============================================================

struct AttendanceSession {

    String sessionId;

    String courseId;
    String courseCode;
    String courseTitle;

    String sessionType;

    // UID of the lecturer who started a fingerprint session.
    String startedByLecturerUid;

    unsigned long startTime;
    unsigned long startEpoch;
    unsigned long duration;

    bool active;
    bool fingerprintOnly;

    // True if the lecturer started this session by fingerprint.
    bool fingerprintStarted;
};


// ============================================================
// INITIALIZATION
// ============================================================

bool session_manager_init();


// ============================================================
// NORMAL FLUTTER-STARTED SESSION
// ============================================================

bool session_start(
    const String& sessionId,
    const String& courseId,
    const String& courseCode,
    const String& courseTitle,
    unsigned long duration,
    unsigned long startEpoch,
    bool fingerprintOnly = false,
    const String& sessionType = "class"
);


// ============================================================
// FINGERPRINT-STARTED EMERGENCY SESSION
// ============================================================

bool session_startEmergency(
    const String& sessionId,
    const String& lecturerUid,
    unsigned long startEpoch = 0
);


// ============================================================
// END SESSION
// ============================================================

bool session_end();


// ============================================================
// SESSION STATUS
// ============================================================

bool session_isActive();

bool session_isFingerprintOnly();

AttendanceSession session_get();

unsigned long session_getEpochNow();

// True if this id is the active session or any completed one this boot.
bool session_idExists(const String& sessionId);


// ============================================================
// JSON
// ============================================================

String session_getJSON();

String session_getHistoryJSON(
    const String& courseId = ""
);

#endif