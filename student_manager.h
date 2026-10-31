#ifndef STUDENT_MANAGER_H
#define STUDENT_MANAGER_H

#include <Arduino.h>

// ============================================================
// STUDENT MODEL
// ============================================================

struct Student {

    String id;
    String name;
    String matricNumber;
    String fingerprintId;
    String deviceId;

    bool registered;
};


// ============================================================
// INITIALIZATION
// ============================================================

bool student_manager_init();

// Writes the student database to the SD card when it has changed.
// Call from loop(). force = true writes immediately.
void student_manager_flush(bool force = false);


// ============================================================
// STUDENT MANAGEMENT
// ============================================================

bool student_add(
    const String& id,
    const String& name,
    const String& matricNumber
);

bool student_exists(
    const String& id
);

bool student_get(
    const String& studentId,
    Student& student
);


// ============================================================
// FINGERPRINT MANAGEMENT
// ============================================================

bool student_bindFingerprint(
    const String& studentId,
    const String& fingerprintId
);

String student_getByFingerprint(
    const String& fingerprintId
);

int student_nextFingerprintId();

// Clears this fingerprint ID from every student record that holds it.
// Returns how many records were changed.
int student_unbindFingerprint(
    const String& fingerprintId
);


// ============================================================
// DEVICE MANAGEMENT
// ============================================================

bool student_bindDevice(
    const String& studentId,
    const String& deviceId
);

String student_getByDevice(
    const String& deviceId
);


// ============================================================
// REGISTRATION NUMBER LOOKUP
// ============================================================

String student_getByMatric(
    const String& matricNumber
);

String student_ensureByMatric(
    const String& matricNumber
);


// ============================================================
// EXPORT
// ============================================================

String student_getAllJSON();

#endif