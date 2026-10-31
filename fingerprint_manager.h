#ifndef FINGERPRINT_MANAGER_H
#define FINGERPRINT_MANAGER_H

#include <Arduino.h>

enum FingerprintStatus {
    FP_IDLE,
    FP_WAITING,
    FP_REMOVE_FINGER,
    FP_PLACE_AGAIN,
    FP_SUCCESS,
    FP_FAILED,
    FP_TIMEOUT
};

bool fingerprint_init();

// Enrollment status. Latched: stays SUCCESS / FAILED / TIMEOUT until the
// next enrollment starts, so the app can always read the result.
FingerprintStatus fingerprint_getStatus();

// Friendly text, e.g. "Waiting for finger"
String fingerprint_statusString(FingerprintStatus status);

// Machine code for the app: IDLE, WAITING, REMOVE_FINGER,
// PLACE_AGAIN, SUCCESS, FAILED, TIMEOUT
String fingerprint_statusCode(FingerprintStatus status);

// Returns matched fingerprint ID or -1.
int fingerprint_findMatch();

bool fingerprint_idExists(uint16_t id);
int fingerprint_getTemplateCount();

// label is shown on the OLED, e.g. "Student" or "Lecturer".
void fingerprint_startEnroll(uint16_t id, const String& label = "");
void fingerprint_processEnroll();
bool fingerprint_isEnrolling();
void fingerprint_cancelEnroll();

// Removes one template from the sensor. Returns true when the slot is
// now empty. Refused while an enrollment is running.
bool fingerprint_deleteTemplate(uint16_t id);

#endif