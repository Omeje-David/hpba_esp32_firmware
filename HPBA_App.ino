#include <Arduino.h>
#include <time.h>

#include "config.h"

#include "wifi_module.h"
#include "sd_manager.h"
#include "oled_manager.h"

#include "fingerprint_manager.h"
#include "student_manager.h"
#include "lecturer_manager.h"

#include "device_manager.h"
#include "session_manager.h"
#include "attendance_manager.h"

#include "captive_portal.h"
#include "api_server.h"

#include "esp_clock.h"
#include "device_guard.h"
#include "session_store.h"
#include "firebase_sync.h"


// ============================================================
// FINGERPRINT CONTROL
// ============================================================

static unsigned long lastFingerCheck = 0;

static const unsigned long FINGER_CHECK_COOLDOWN_MS = 800;


// ============================================================
// GENERATE EMERGENCY SESSION ID
//
// Named with the date and time the session started, for example
// EMERGENCY_20261029_1430. The time comes from the last phone or
// Firebase sync (see esp_clock.h). A second session in the same
// minute gets _2, _3 and so on.
// ============================================================

static String generateEmergencySessionId(
    const String& lecturerUid,
    int fingerprintId
) {

    (void)lecturerUid;
    (void)fingerprintId;

    String base;

    if (clock_isSet()) {

        base = "EMERGENCY_" + clock_localStamp(clock_now());

    } else {

        // No time source yet. The real time is filled in at sync.
        base = "EMERGENCY_NOCLOCK_" + String(millis() / 1000UL);
    }

    String sessionId = base;

    for (int n = 2; n < 100 && session_idExists(sessionId); n++) {
        sessionId = base + "_" + String(n);
    }

    return sessionId;
}


// ============================================================
// READY SCREEN
//
// Shows how many finished sessions are still waiting on the SD card
// to be uploaded, so nobody forgets about them.
// ============================================================

static void showReadyScreen() {

    int pending = cloud_pendingCount();

    oled_show(
        "HPBA Attendance",
        "System Ready",
        wifi_isConnected()
            ? "STA: ONLINE"
            : "STA: OFFLINE",
        pending > 0
            ? "Unsynced: " + String(pending)
            : "Scan lecturer"
    );
}


// ============================================================
// HANDLE LECTURER FINGERPRINT
// ============================================================

static bool handleLecturerFingerprint(
    int fingerprintId
) {

    String lecturerUid =
        lecturer_getByFingerprint(
            String(fingerprintId)
        );

    // Not a lecturer fingerprint.
    if (
        lecturerUid.length() == 0
    ) {
        return false;
    }

    Serial.println();

    Serial.println(
        "[LECTURER] Authorized lecturer fingerprint recognized"
    );

    Serial.print(
        "[LECTURER] UID: "
    );

    Serial.println(
        lecturerUid
    );


    // --------------------------------------------------------
    // No active session: start emergency session.
    // --------------------------------------------------------

    if (
        !session_isActive()
    ) {

        String sessionId =
            generateEmergencySessionId(
                lecturerUid,
                fingerprintId
            );

        unsigned long startEpoch =
            clock_now();

        bool started =
            session_startEmergency(
                sessionId,
                lecturerUid,
                startEpoch
            );

        if (!started) {

            Serial.println(
                "[LECTURER] Could not start emergency session"
            );

            oled_show(
                "HPBA Attendance",
                "Start failed",
                "Check session",
                ""
            );

            return true;
        }

        oled_show(
            "Session Started",
            "Emergency mode",
            sd_isReady() ? "Scan again to end" : "NO SD CARD!",
            sessionId.substring(0, 21)
        );

        Serial.println(
            "[LECTURER] Emergency session started"
        );

        Serial.println(
            "[LECTURER] Remove finger before the next scan"
        );

        return true;
    }


    // --------------------------------------------------------
    // An active session exists.
    // --------------------------------------------------------

    AttendanceSession current =
        session_get();


    // --------------------------------------------------------
    // Only the lecturer who started the emergency session
    // may end it by fingerprint.
    // --------------------------------------------------------

    if (
        current.fingerprintStarted
    ) {

        if (
            current.startedByLecturerUid != lecturerUid
        ) {

            Serial.println(
                "[LECTURER] This lecturer did not start "
                "the active emergency session"
            );

            oled_show(
                "Session Active",
                "Wrong lecturer",
                "Cannot end",
                ""
            );

            return true;
        }

        // Ignore a second scan in the first seconds so one accidental
        // double touch cannot end the session it just started.
        if (
            millis() - current.startTime <
            10000UL
        ) {

            Serial.println(
                "[LECTURER] Session just started. Ignoring end scan."
            );

            oled_show(
                "Session Active",
                "Just started",
                "Wait 10s to end",
                ""
            );

            return true;
        }

        bool ended =
            session_end();

        if (ended) {

            Serial.println(
                "[LECTURER] Emergency session ended"
            );

            // The session was already written to the SD card by
            // session_end(). Now upload it to Firebase through the
            // phone hotspot. Progress is shown on the OLED.
            String savedName = session_store_lastName();

            oled_show(
                "Session Ended",
                savedName.length() > 0
                    ? "Saved to SD card"
                    : "SD save failed",
                savedName.length() > 0
                    ? savedName.substring(0, 21)
                    : "Kept in RAM only",
                "Syncing next..."
            );

            delay(1500);

            cloud_syncEmergencyQueue();

            showReadyScreen();

        } else {

            oled_show(
                "Session Error",
                "Could not end",
                "",
                ""
            );
        }

        return true;
    }


    // --------------------------------------------------------
    // Normal app-started session.
    //
    // Preserve the normal Flutter workflow. A lecturer
    // fingerprint does not end an app-started session.
    // --------------------------------------------------------

    Serial.println(
        "[LECTURER] A normal app-started session is active"
    );

    Serial.println(
        "[LECTURER] End this session from the Flutter app"
    );

    oled_show(
        "Session Active",
        "Use lecturer app",
        "to end session",
        ""
    );

    return true;
}


// ============================================================
// FINGERPRINT ATTENDANCE AND SESSION CONTROL
// ============================================================

static void checkFingerprintSignIn() {

    unsigned long now = millis();

    if (
        now - lastFingerCheck <
        FINGER_CHECK_COOLDOWN_MS
    ) {
        return;
    }

    lastFingerCheck = now;


    // --------------------------------------------------------
    // Scan for a fingerprint.
    //
    // fingerprint_findMatch() now waits for finger removal
    // before it can report another successful match.
    // --------------------------------------------------------

    int fingerId =
        fingerprint_findMatch();

    if (
        fingerId < 0
    ) {
        return;
    }

    Serial.println();

    Serial.println(
        "[FINGERPRINT] Fingerprint matched"
    );

    Serial.print(
        "[FINGERPRINT] Fingerprint ID: "
    );

    Serial.println(
        fingerId
    );


    // --------------------------------------------------------
    // Lecturer fingerprints have priority.
    //
    // A lecturer fingerprint must never be recorded as
    // student attendance.
    // --------------------------------------------------------

    if (
        handleLecturerFingerprint(
            fingerId
        )
    ) {
        return;
    }


    // --------------------------------------------------------
    // Not a lecturer. Say so clearly on Serial, together with the
    // lecturer mappings on file, so a lecturer who is being treated
    // as a student can be diagnosed in seconds.
    // --------------------------------------------------------

    Serial.print(
        "[FINGERPRINT] Slot "
    );

    Serial.print(
        fingerId
    );

    Serial.println(
        " is NOT linked to any lecturer."
    );

    Serial.print(
        "[FINGERPRINT] Lecturers on file: "
    );

    Serial.println(
        lecturer_getAllJSON()
    );


    // --------------------------------------------------------
    // If no session is active, students cannot mark attendance.
    // --------------------------------------------------------

    if (
        !session_isActive()
    ) {

        Serial.println(
            "[FINGERPRINT] No active session"
        );

        oled_show(
            "No Active Session",
            "Student fingerprint",
            "not recorded",
            ""
        );

        return;
    }


    // --------------------------------------------------------
    // Look up the student fingerprint.
    // --------------------------------------------------------

    String studentId =
        student_getByFingerprint(
            String(fingerId)
        );

    if (
        studentId.length() == 0
    ) {

        Serial.println(
            "[FINGERPRINT] No student mapped to this fingerprint"
        );

        oled_show(
            "Fingerprint",
            "Unknown student",
            "ID: " + String(fingerId),
            "Try again"
        );

        return;
    }


    // --------------------------------------------------------
    // Get active session.
    // --------------------------------------------------------

    AttendanceSession current =
        session_get();

    if (
        !current.active
    ) {

        Serial.println(
            "[FINGERPRINT] Session is no longer active"
        );

        return;
    }


    // --------------------------------------------------------
    // Prevent duplicate attendance.
    // --------------------------------------------------------

    if (
        attendance_alreadyMarked(
            studentId,
            current.sessionId
        )
    ) {

        Serial.println(
            "[FINGERPRINT] Student already marked"
        );

        Student student;

        bool loaded =
            student_get(
                studentId,
                student
            );

        oled_show(
            "Already Marked",
            loaded && student.name.length() > 0
                ? student.name
                : studentId,
            "Fingerprint",
            current.courseCode
        );

        return;
    }


    // --------------------------------------------------------
    // Mark attendance.
    // --------------------------------------------------------

    bool marked =
        attendance_mark(
            studentId,
            current.sessionId,
            ATTENDANCE_FINGERPRINT,
            0
        );


    // --------------------------------------------------------
    // Load student details for display.
    // --------------------------------------------------------

    Student student;

    bool loaded =
        student_get(
            studentId,
            student
        );


    // --------------------------------------------------------
    // Display result.
    // --------------------------------------------------------

    if (marked) {

        Serial.println(
            "[FINGERPRINT] Attendance marked successfully"
        );

        Serial.print(
            "[FINGERPRINT] Student ID: "
        );

        Serial.println(
            studentId
        );

        oled_show(
            "Signed In:",
            loaded && student.name.length() > 0
                ? student.name
                : studentId,
            "Fingerprint OK",
            current.courseCode
        );

    } else {

        Serial.println(
            "[FINGERPRINT] Could not save attendance"
        );

        oled_show(
            "Fingerprint",
            "Could not save",
            "Try again",
            ""
        );
    }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        " HPBA HYBRID ATTENDANCE SYSTEM"
    );

    Serial.println(
        " ESP32 STARTUP"
    );

    Serial.println(
        "========================================"
    );


    // --------------------------------------------------------
    // NETWORK
    // --------------------------------------------------------

    wifi_init();


    // --------------------------------------------------------
    // SD INITIALIZATION
    //
    // Students, lecturer fingerprints, the clock and finished
    // sessions are saved on the card.
    // --------------------------------------------------------

    sd_init();

    clock_init();

    session_store_init();


    // --------------------------------------------------------
    // OLED
    // --------------------------------------------------------

    oled_init();


    // --------------------------------------------------------
    // STUDENT MANAGER
    // --------------------------------------------------------

    student_manager_init();


    // --------------------------------------------------------
    // LECTURER MANAGER
    // --------------------------------------------------------

    lecturer_manager_init();


    // --------------------------------------------------------
    // DEVICE MANAGER
    // --------------------------------------------------------

    device_manager_init();


    // --------------------------------------------------------
    // SESSION MANAGER
    // --------------------------------------------------------

    session_manager_init();


    // --------------------------------------------------------
    // ATTENDANCE MANAGER
    // --------------------------------------------------------

    attendance_manager_init();


    // --------------------------------------------------------
    // FINGERPRINT SENSOR
    // --------------------------------------------------------

    if (
        fingerprint_init()
    ) {

        Serial.println(
            "[R307] Fingerprint sensor ready"
        );

    } else {

        Serial.println(
            "[R307] Fingerprint sensor unavailable"
        );
    }


    // --------------------------------------------------------
    // CAPTIVE PORTAL
    // --------------------------------------------------------

    captive_portal_init();


    // --------------------------------------------------------
    // HTTP API
    // --------------------------------------------------------

    api_server_init();


    // --------------------------------------------------------
    // READY SCREEN
    // --------------------------------------------------------

    showReadyScreen();


    // --------------------------------------------------------
    // SERIAL SUMMARY
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "[SYSTEM] HPBA system ready"
    );

    Serial.print(
        "[SYSTEM] Student Wi-Fi: "
    );

    Serial.println(
        AP_SSID
    );

    Serial.print(
        "[SYSTEM] Student portal: http://"
    );

    Serial.print(
        wifi_getAPIP()
    );

    Serial.println(
        "/"
    );

    if (
        wifi_isConnected()
    ) {

        Serial.print(
            "[SYSTEM] Flutter API: http://"
        );

        Serial.print(
            wifi_getIP()
        );

        Serial.println(
            "/"
        );

    } else {

        Serial.println(
            "[SYSTEM] Flutter API available through "
            "the ESP32 access point"
        );
    }

    Serial.println(
        "[FINGERPRINT] Lecturer fingerprints can "
        "start and end emergency sessions"
    );

    Serial.println(
        "[FINGERPRINT] Student fingerprints mark attendance"
    );

    Serial.println(
        "[FINGERPRINT] Enrollment takes priority"
    );

    Serial.println();
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

    // Continue servicing the API.
    api_server_handle();


    // Continue servicing the captive portal.
    captive_portal_handle();


    // Enrollment has priority over normal scanning.
    if (
        fingerprint_isEnrolling()
    ) {

        fingerprint_processEnroll();

    } else {

        checkFingerprintSignIn();
    }

    // Save student changes to the SD card (batched, every few seconds).
    student_manager_flush(false);

    // Free the Wi-Fi slots of students who have already signed.
    {
        static unsigned long lastRelease = 0;

        if (millis() - lastRelease >= 1000) {

            lastRelease = millis();

            if (session_isActive()) {
                device_guard_releaseSignedClients();
            }
        }
    }

    // Remember the time on the SD card every 10 minutes.
    clock_tick();

    delay(2);
}