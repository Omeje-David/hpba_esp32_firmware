#include <Arduino.h>
#include <HardwareSerial.h>
#include <Adafruit_Fingerprint.h>

#include "fingerprint_manager.h"
#include "oled_manager.h"

static const int FINGERPRINT_RX_PIN = 16;
static const int FINGERPRINT_TX_PIN = 17;
static const uint32_t FINGERPRINT_BAUD_RATE = 57600;

static HardwareSerial fingerprintSerial(2);
static Adafruit_Fingerprint finger(&fingerprintSerial);

// ============================================================
// STATE
// ============================================================

// ENROLLMENT status only. Normal scanning never changes this.
static FingerprintStatus currentStatus = FP_IDLE;

static bool sensorReady = false;
static bool enrollmentActive = false;

static uint16_t enrollmentId = 0;
static uint8_t enrollmentStep = 0;
static String enrollmentLabel = "";

static unsigned long enrollmentStartedAt = 0;
static const unsigned long ENROLLMENT_TIMEOUT_MS = 60000UL;

static bool waitingForFingerRemoval = false;

// ============================================================
// OLED HELPER
// ============================================================

static void showEnroll(
    const String& l2,
    const String& l3 = "",
    const String& l4 = ""
) {
    String title = "ENROLL";
    if (enrollmentLabel.length() > 0) {
        title += " " + enrollmentLabel;
    }
    oled_show(title, l2, l3, l4);
}

// ============================================================
// INIT
// ============================================================

bool fingerprint_init() {

    fingerprintSerial.begin(
        FINGERPRINT_BAUD_RATE,
        SERIAL_8N1,
        FINGERPRINT_RX_PIN,
        FINGERPRINT_TX_PIN
    );

    delay(100);

    if (!finger.verifyPassword()) {
        Serial.println("[R307] Fingerprint sensor not detected");
        sensorReady = false;
        currentStatus = FP_FAILED;
        return false;
    }

    sensorReady = true;
    currentStatus = FP_IDLE;

    Serial.println("[R307] Fingerprint sensor initialized");
    Serial.printf(
        "[R307] RX: GPIO %d, TX: GPIO %d, baud: %lu\n",
        FINGERPRINT_RX_PIN,
        FINGERPRINT_TX_PIN,
        (unsigned long)FINGERPRINT_BAUD_RATE
    );

    return true;
}

// ============================================================
// STATUS
// ============================================================

FingerprintStatus fingerprint_getStatus() {
    return currentStatus;
}

String fingerprint_statusString(FingerprintStatus status) {
    switch (status) {
        case FP_IDLE:          return "Idle";
        case FP_WAITING:       return "Waiting for finger";
        case FP_REMOVE_FINGER: return "Remove finger";
        case FP_PLACE_AGAIN:   return "Place finger again";
        case FP_SUCCESS:       return "Success";
        case FP_FAILED:        return "Failed";
        case FP_TIMEOUT:       return "Timed out";
        default:               return "Unknown";
    }
}

String fingerprint_statusCode(FingerprintStatus status) {
    switch (status) {
        case FP_IDLE:          return "IDLE";
        case FP_WAITING:       return "WAITING";
        case FP_REMOVE_FINGER: return "REMOVE_FINGER";
        case FP_PLACE_AGAIN:   return "PLACE_AGAIN";
        case FP_SUCCESS:       return "SUCCESS";
        case FP_FAILED:        return "FAILED";
        case FP_TIMEOUT:       return "TIMEOUT";
        default:               return "UNKNOWN";
    }
}

bool fingerprint_isEnrolling() {
    return enrollmentActive;
}

// ============================================================
// MATCHING (does NOT touch enrollment status)
// ============================================================

int fingerprint_findMatch() {

    if (!sensorReady) return -1;
    if (fingerprint_isEnrolling()) return -1;

    if (waitingForFingerRemoval) {

        uint8_t removalResult = finger.getImage();

        if (removalResult == FINGERPRINT_NOFINGER) {
            waitingForFingerRemoval = false;
            Serial.println("[R307] Finger removed. Scanner ready.");
        }

        return -1;
    }

    uint8_t result = finger.getImage();

    if (result == FINGERPRINT_NOFINGER) return -1;

    if (result != FINGERPRINT_OK) {
        Serial.printf("[R307] getImage failed: %u\n", result);
        return -1;
    }

    result = finger.image2Tz();

    if (result != FINGERPRINT_OK) {
        Serial.printf("[R307] image2Tz failed: %u\n", result);
        return -1;
    }

    result = finger.fingerFastSearch();

    if (result == FINGERPRINT_NOTFOUND) {
        Serial.println("[R307] Fingerprint not recognized");
        // Make the user lift the finger so we don't spam the sensor.
        waitingForFingerRemoval = true;
        oled_show("Fingerprint", "Not recognized", "Try again", "");
        return -1;
    }

    if (result != FINGERPRINT_OK) {
        Serial.printf("[R307] Fingerprint search failed: %u\n", result);
        return -1;
    }

    waitingForFingerRemoval = true;

    Serial.printf(
        "[R307] Match found. ID: %u, confidence: %u\n",
        finger.fingerID,
        finger.confidence
    );

    return finger.fingerID;
}

bool fingerprint_idExists(uint16_t id) {
    if (!sensorReady || id == 0) return false;
    return finger.loadModel(id) == FINGERPRINT_OK;
}

int fingerprint_getTemplateCount() {

    if (!sensorReady) return -1;

    uint8_t result = finger.getTemplateCount();

    if (result != FINGERPRINT_OK) {
        Serial.printf("[R307] Could not read template count: %u\n", result);
        return -1;
    }

    return finger.templateCount;
}

// ============================================================
// START ENROLLMENT
// ============================================================

void fingerprint_startEnroll(uint16_t id, const String& label) {

    if (!sensorReady) {
        Serial.println("[R307] Cannot enroll: sensor unavailable");
        currentStatus = FP_FAILED;
        oled_show("ENROLL FAILED", "Sensor not found", "Check wiring", "");
        return;
    }

    if (enrollmentActive) {
        Serial.println("[R307] Enrollment already in progress");
        return;
    }

    if (id == 0) {
        Serial.println("[R307] Invalid fingerprint ID: 0");
        currentStatus = FP_FAILED;
        oled_show("ENROLL FAILED", "Invalid sensor ID", "", "");
        return;
    }

    enrollmentId = id;
    enrollmentLabel = label;
    enrollmentStep = 0;
    enrollmentStartedAt = millis();
    enrollmentActive = true;
    waitingForFingerRemoval = false;
    currentStatus = FP_WAITING;

    Serial.printf("[R307] Enrollment started for ID %u\n", enrollmentId);

    showEnroll(
        "Place finger",
        "on the sensor",
        "Slot: " + String(enrollmentId)
    );
}

// ============================================================
// PROCESS ENROLLMENT (non-blocking)
// ============================================================

void fingerprint_processEnroll() {

    if (!sensorReady || !enrollmentActive) return;

    if (millis() - enrollmentStartedAt >= ENROLLMENT_TIMEOUT_MS) {

        Serial.println("[R307] Enrollment timed out");

        enrollmentActive = false;
        enrollmentStep = 0;
        currentStatus = FP_TIMEOUT;

        showEnroll("TIMED OUT", "Not enrolled", "Try again in app");
        return;
    }

    // ---- STEP 0: first image ----
    if (enrollmentStep == 0) {

        currentStatus = FP_WAITING;

        uint8_t result = finger.getImage();

        if (result == FINGERPRINT_NOFINGER) return;

        if (result != FINGERPRINT_OK) {
            // Transient read problem. Keep waiting.
            Serial.printf("[R307] First image error (retrying): %u\n", result);
            return;
        }

        result = finger.image2Tz(1);

        if (result != FINGERPRINT_OK) {
            Serial.printf("[R307] First conversion failed: %u\n", result);
            showEnroll("Bad scan", "Press firmly and", "try again");
            delay(700);
            showEnroll("Place finger", "on the sensor");
            return;
        }

        currentStatus = FP_REMOVE_FINGER;
        enrollmentStep = 1;

        Serial.println("[R307] First scan captured. Remove finger.");
        showEnroll("Scan 1 of 2 OK", "Remove your", "finger now");
        return;
    }

    // ---- STEP 1: wait for removal ----
    if (enrollmentStep == 1) {

        currentStatus = FP_REMOVE_FINGER;

        uint8_t result = finger.getImage();

        if (result != FINGERPRINT_NOFINGER) return;

        enrollmentStep = 2;
        currentStatus = FP_PLACE_AGAIN;

        Serial.println("[R307] Finger removed. Place the same finger again.");
        showEnroll("Place the SAME", "finger again", "Scan 2 of 2");
        return;
    }

    // ---- STEP 2: second image ----
    if (enrollmentStep == 2) {

        currentStatus = FP_PLACE_AGAIN;

        uint8_t result = finger.getImage();

        if (result == FINGERPRINT_NOFINGER) return;

        if (result != FINGERPRINT_OK) {
            Serial.printf("[R307] Second image error (retrying): %u\n", result);
            return;
        }

        result = finger.image2Tz(2);

        if (result != FINGERPRINT_OK) {
            Serial.printf("[R307] Second conversion failed: %u\n", result);
            showEnroll("Bad scan", "Place finger", "again");
            return;
        }

        enrollmentStep = 3;
        Serial.println("[R307] Second scan captured.");
        showEnroll("Scan 2 of 2 OK", "Saving...", "");
        return;
    }

    // ---- STEP 3: create + store model ----
    if (enrollmentStep == 3) {

        uint8_t result = finger.createModel();

        if (result != FINGERPRINT_OK) {

            Serial.printf("[R307] Fingerprints did not match: %u\n", result);

            enrollmentActive = false;
            enrollmentStep = 0;
            currentStatus = FP_FAILED;

            showEnroll("FAILED", "Scans did not", "match. Try again");
            return;
        }

        result = finger.storeModel(enrollmentId);

        if (result != FINGERPRINT_OK) {

            Serial.printf(
                "[R307] Could not store fingerprint ID %u: %u\n",
                enrollmentId,
                result
            );

            enrollmentActive = false;
            enrollmentStep = 0;
            currentStatus = FP_FAILED;

            showEnroll("FAILED", "Could not save", "to sensor");
            return;
        }

        Serial.printf("[R307] Enrollment successful. ID: %u\n", enrollmentId);

        enrollmentActive = false;
        enrollmentStep = 0;
        waitingForFingerRemoval = true;

        // Latched until the next enrollment starts.
        currentStatus = FP_SUCCESS;

        showEnroll("SUCCESS!", "Fingerprint saved", "Slot: " + String(enrollmentId));
        return;
    }
}

// ============================================================
// CANCEL
// ============================================================

void fingerprint_cancelEnroll() {

    if (!enrollmentActive) return;

    enrollmentActive = false;
    enrollmentStep = 0;
    enrollmentId = 0;
    currentStatus = FP_IDLE;

    Serial.println("[R307] Enrollment cancelled");
    oled_show("Enrollment", "Cancelled", "", "");
}

// ============================================================
// DELETE ONE TEMPLATE
// ============================================================

bool fingerprint_deleteTemplate(uint16_t id) {

    if (!sensorReady || id == 0) return false;

    if (enrollmentActive) {
        Serial.println("[R307] Cannot delete while enrolling");
        return false;
    }

    uint8_t result = finger.deleteModel(id);

    if (result != FINGERPRINT_OK) {
        Serial.printf("[R307] Could not delete ID %u: %u\n", id, result);
        return false;
    }

    Serial.printf("[R307] Deleted fingerprint ID %u\n", id);

    return true;
}
