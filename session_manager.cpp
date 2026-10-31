#include "session_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "esp_clock.h"
#include "device_guard.h"
#include "session_store.h"
#include "attendance_manager.h"


// ============================================================
// CURRENT SESSION
// ============================================================

static AttendanceSession currentSession;


// ============================================================
// COMPLETED SESSION HISTORY
// ============================================================

static const int MAX_COMPLETED_SESSIONS = 100;


struct CompletedSession {

    String sessionId;

    String courseId;
    String courseCode;
    String courseTitle;

    String sessionType;
    String startedByLecturerUid;

    unsigned long startEpoch;
    unsigned long endedEpoch;
    unsigned long duration;

    bool fingerprintOnly;
    bool fingerprintStarted;
};


static CompletedSession completedSessions[
    MAX_COMPLETED_SESSIONS
];

static int completedSessionCount = 0;


// ============================================================
// SESSION ID CHECK
// ============================================================

static bool sessionIdExistsInHistory(
    const String& sessionId
) {

    for (
        int i = 0;
        i < completedSessionCount;
        i++
    ) {

        if (
            completedSessions[i].sessionId ==
            sessionId
        ) {
            return true;
        }
    }

    return false;
}


// ============================================================
// INITIALIZATION
// ============================================================

bool session_manager_init() {

    currentSession.sessionId = "";

    currentSession.courseId = "";
    currentSession.courseCode = "";
    currentSession.courseTitle = "";

    currentSession.sessionType = "class";

    currentSession.startedByLecturerUid = "";

    currentSession.startTime = 0;
    currentSession.startEpoch = 0;
    currentSession.duration = 0;

    currentSession.active = false;
    currentSession.fingerprintOnly = false;
    currentSession.fingerprintStarted = false;

    completedSessionCount = 0;

    Serial.println(
        "[SESSION] Session manager ready"
    );

    Serial.println(
        "[SESSION] RAM-based session history enabled"
    );

    return true;
}


// ============================================================
// NORMAL SESSION STARTED FROM FLUTTER
// ============================================================

bool session_start(
    const String& sessionId,
    const String& courseId,
    const String& courseCode,
    const String& courseTitle,
    unsigned long duration,
    unsigned long startEpoch,
    bool fingerprintOnly,
    const String& sessionType
) {

    // Check for an existing active session.
    if (session_isActive()) {

        Serial.println(
            "[SESSION] A session is already active"
        );

        return false;
    }

    String cleanSessionId = sessionId;
    String cleanCourseId = courseId;
    String cleanCourseCode = courseCode;

    cleanSessionId.trim();
    cleanCourseId.trim();
    cleanCourseCode.trim();

    // Normal Flutter sessions still require a course.
    if (
        cleanSessionId.length() == 0 ||
        cleanCourseId.length() == 0 ||
        cleanCourseCode.length() == 0
    ) {

        Serial.println(
            "[SESSION] Cannot start normal session: "
            "missing session or course information"
        );

        return false;
    }

    // Prevent reuse of a completed session ID.
    if (
        sessionIdExistsInHistory(cleanSessionId)
    ) {

        Serial.println(
            "[SESSION] Session ID already completed"
        );

        return false;
    }

    String normalizedType = sessionType;

    normalizedType.trim();
    normalizedType.toLowerCase();

    if (normalizedType != "exam") {
        normalizedType = "class";
    }

    // The phone sends the real time. Use it for the ESP32 clock.
    if (startEpoch > 0) {
        clock_setEpoch(startEpoch);
    } else if (clock_isSet()) {
        startEpoch = clock_now();
    }

    // New session: forget the devices of the previous one and free
    // RAM if the attendance buffer is getting full.
    device_guard_reset();
    attendance_compactIfNeeded();

    currentSession.sessionId = cleanSessionId;

    currentSession.courseId = cleanCourseId;
    currentSession.courseCode = cleanCourseCode;
    currentSession.courseTitle = courseTitle;

    currentSession.sessionType = normalizedType;

    // Normal app-started sessions are not tied to a
    // fingerprint-starting lecturer in this implementation.
    currentSession.startedByLecturerUid = "";

    currentSession.startTime = millis();
    currentSession.startEpoch = startEpoch;
    currentSession.duration = duration;

    currentSession.active = true;
    currentSession.fingerprintOnly = fingerprintOnly;

    currentSession.fingerprintStarted = false;

    Serial.println();
    Serial.println(
        "[SESSION] NORMAL SESSION STARTED"
    );

    Serial.print(
        "[SESSION] ID: "
    );

    Serial.println(
        currentSession.sessionId
    );

    Serial.print(
        "[SESSION] Course: "
    );

    Serial.println(
        currentSession.courseCode
    );

    Serial.print(
        "[SESSION] Type: "
    );

    Serial.println(
        currentSession.sessionType
    );

    Serial.print(
        "[SESSION] Duration: "
    );

    Serial.print(duration);

    Serial.println(
        " seconds"
    );

    Serial.print(
        "[SESSION] Fingerprint-only: "
    );

    Serial.println(
        fingerprintOnly ? "YES" : "NO"
    );

    return true;
}


// ============================================================
// EMERGENCY SESSION STARTED BY LECTURER FINGERPRINT
// ============================================================

bool session_startEmergency(
    const String& sessionId,
    const String& lecturerUid,
    unsigned long startEpoch
) {

    if (session_isActive()) {

        Serial.println(
            "[SESSION] Cannot start emergency session: "
            "another session is active"
        );

        return false;
    }

    String cleanSessionId = sessionId;
    String cleanLecturerUid = lecturerUid;

    cleanSessionId.trim();
    cleanLecturerUid.trim();

    if (
        cleanSessionId.length() == 0 ||
        cleanLecturerUid.length() == 0
    ) {

        Serial.println(
            "[SESSION] Emergency session requires "
            "a session ID and lecturer UID"
        );

        return false;
    }

    if (
        sessionIdExistsInHistory(cleanSessionId)
    ) {

        Serial.println(
            "[SESSION] Emergency session ID already exists"
        );

        return false;
    }

    if (startEpoch == 0 && clock_isSet()) {
        startEpoch = clock_now();
    }

    device_guard_reset();
    attendance_compactIfNeeded();

    // Course assignment is deliberately postponed.
    currentSession.sessionId = cleanSessionId;

    currentSession.courseId = "";
    currentSession.courseCode = "";
    currentSession.courseTitle = "";

    // The lecturer will choose class or exam when claiming
    // the completed session through the app.
    currentSession.sessionType = "unassigned";

    currentSession.startedByLecturerUid =
        cleanLecturerUid;

    currentSession.startTime = millis();
    currentSession.startEpoch = startEpoch;

    // Zero means no automatic duration expiry.
    currentSession.duration = 0;

    currentSession.active = true;

    // This emergency session is controlled by the lecturer's
    // fingerprint. Do not interpret this as disabling student
    // attendance by fingerprint.
    currentSession.fingerprintOnly = false;

    currentSession.fingerprintStarted = true;

    Serial.println();
    Serial.println(
        "[SESSION] EMERGENCY SESSION STARTED"
    );

    Serial.print(
        "[SESSION] Session ID: "
    );

    Serial.println(
        currentSession.sessionId
    );

    Serial.print(
        "[SESSION] Started by lecturer: "
    );

    Serial.println(
        currentSession.startedByLecturerUid
    );

    Serial.println(
        "[SESSION] Course assignment postponed"
    );

    Serial.println(
        "[SESSION] Scan the same lecturer fingerprint "
        "again to end this session"
    );

    return true;
}


// ============================================================
// END SESSION AND SAVE HISTORY
// ============================================================

bool session_end() {

    if (!currentSession.active) {

        Serial.println(
            "[SESSION] No active session to end"
        );

        return false;
    }

    unsigned long endedEpoch =
        session_getEpochNow();

    if (
        completedSessionCount <
        MAX_COMPLETED_SESSIONS
    ) {

        CompletedSession& saved =
            completedSessions[
                completedSessionCount
            ];

        saved.sessionId =
            currentSession.sessionId;

        saved.courseId =
            currentSession.courseId;

        saved.courseCode =
            currentSession.courseCode;

        saved.courseTitle =
            currentSession.courseTitle;

        saved.sessionType =
            currentSession.sessionType;

        saved.startedByLecturerUid =
            currentSession.startedByLecturerUid;

        saved.startEpoch =
            currentSession.startEpoch;

        saved.endedEpoch =
            endedEpoch;

        saved.duration =
            currentSession.duration;

        saved.fingerprintOnly =
            currentSession.fingerprintOnly;

        saved.fingerprintStarted =
            currentSession.fingerprintStarted;

        completedSessionCount++;

        Serial.println(
            "[SESSION] Completed session saved to RAM history"
        );

        Serial.print(
            "[SESSION] Completed ID: "
        );

        Serial.println(
            saved.sessionId
        );

    } else {

        Serial.println(
            "[SESSION] WARNING: Session history is full"
        );
    }

    // Quietly archive the finished session on the SD card. This never
    // blocks or fails the session: errors are only logged on Serial.
    session_store_saveCompleted(
        currentSession,
        endedEpoch,
        millis()
    );

    currentSession.active = false;

    Serial.println(
        "[SESSION] Session ended successfully"
    );

    return true;
}


// ============================================================
// ACTIVE SESSION AND AUTOMATIC EXPIRY
// ============================================================

bool session_isActive() {

    if (!currentSession.active) {
        return false;
    }

    // Emergency sessions have duration == 0, so they do not
    // automatically expire.
    if (currentSession.duration > 0) {

        unsigned long elapsedSeconds =
            (
                millis() -
                currentSession.startTime
            ) / 1000UL;

        if (
            elapsedSeconds >=
            currentSession.duration
        ) {

            Serial.println(
                "[SESSION] Session duration expired"
            );

            session_end();

            return false;
        }
    }

    return currentSession.active;
}


// ============================================================
// SESSION INFORMATION
// ============================================================

bool session_isFingerprintOnly() {

    return currentSession.active &&
           currentSession.fingerprintOnly;
}


AttendanceSession session_get() {

    return currentSession;
}


bool session_idExists(const String& sessionId) {

    if (
        currentSession.active &&
        currentSession.sessionId == sessionId
    ) {
        return true;
    }

    return sessionIdExistsInHistory(sessionId);
}


unsigned long session_getEpochNow() {

    // No start time recorded: use the ESP32 clock directly.
    if (
        currentSession.startEpoch == 0
    ) {
        return clock_now();
    }

    unsigned long elapsedSeconds =
        (
            millis() -
            currentSession.startTime
        ) / 1000UL;

    return
        currentSession.startEpoch +
        elapsedSeconds;
}


// ============================================================
// CURRENT SESSION JSON
// ============================================================

String session_getJSON() {

    DynamicJsonDocument doc(3072);

    doc["active"] =
        currentSession.active;

    doc["sessionId"] =
        currentSession.sessionId;

    doc["courseId"] =
        currentSession.courseId;

    doc["courseCode"] =
        currentSession.courseCode;

    doc["courseTitle"] =
        currentSession.courseTitle;

    doc["sessionType"] =
        currentSession.sessionType;

    doc["startedByLecturerUid"] =
        currentSession.startedByLecturerUid;

    doc["startTime"] =
        currentSession.startTime;

    doc["startEpoch"] =
        currentSession.startEpoch;

    doc["duration"] =
        currentSession.duration;

    doc["fingerprintOnly"] =
        currentSession.fingerprintOnly;

    doc["fingerprintStarted"] =
        currentSession.fingerprintStarted;

    String output;

    serializeJson(
        doc,
        output
    );

    return output;
}


// ============================================================
// SESSION HISTORY
//
// Empty courseId returns all sessions.
// A specified courseId returns sessions assigned to that
// course. Unclaimed emergency sessions have an empty courseId
// and therefore appear when requesting all sessions.
// ============================================================

String session_getHistoryJSON(
    const String& courseId
) {

    DynamicJsonDocument doc(24576);

    JsonArray sessions =
        doc.createNestedArray("sessions");


    // --------------------------------------------------------
    // Include the currently active session.
    // --------------------------------------------------------

    if (
        currentSession.active &&
        (
            courseId.length() == 0 ||
            currentSession.courseId == courseId
        )
    ) {

        JsonObject item =
            sessions.createNestedObject();

        item["sessionId"] =
            currentSession.sessionId;

        item["courseId"] =
            currentSession.courseId;

        item["courseCode"] =
            currentSession.courseCode;

        item["courseTitle"] =
            currentSession.courseTitle;

        item["sessionType"] =
            currentSession.sessionType;

        item["startedByLecturerUid"] =
            currentSession.startedByLecturerUid;

        item["startEpoch"] =
            currentSession.startEpoch;

        item["endedEpoch"] = 0;

        item["duration"] =
            currentSession.duration;

        item["active"] = true;

        item["fingerprintOnly"] =
            currentSession.fingerprintOnly;

        item["fingerprintStarted"] =
            currentSession.fingerprintStarted;
    }


    // --------------------------------------------------------
    // Completed sessions, newest first.
    // --------------------------------------------------------

    for (
        int i = completedSessionCount - 1;
        i >= 0;
        i--
    ) {

        CompletedSession& saved =
            completedSessions[i];

        if (
            courseId.length() > 0 &&
            saved.courseId != courseId
        ) {
            continue;
        }

        JsonObject item =
            sessions.createNestedObject();

        item["sessionId"] =
            saved.sessionId;

        item["courseId"] =
            saved.courseId;

        item["courseCode"] =
            saved.courseCode;

        item["courseTitle"] =
            saved.courseTitle;

        item["sessionType"] =
            saved.sessionType;

        item["startedByLecturerUid"] =
            saved.startedByLecturerUid;

        item["startEpoch"] =
            saved.startEpoch;

        item["endedEpoch"] =
            saved.endedEpoch;

        item["duration"] =
            saved.duration;

        item["active"] = false;

        item["fingerprintOnly"] =
            saved.fingerprintOnly;

        item["fingerprintStarted"] =
            saved.fingerprintStarted;
    }


    String output;

    serializeJson(
        doc,
        output
    );

    return output;
}