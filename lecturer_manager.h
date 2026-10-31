#ifndef LECTURER_MANAGER_H
#define LECTURER_MANAGER_H

#include <Arduino.h>

// Initialize the lecturer manager.
void lecturer_manager_init();

// Bind a lecturer's Firebase UID to a fingerprint ID.
bool lecturer_bindFingerprint(
    const String& lecturerUid,
    const String& fingerprintId
);

// Check whether a lecturer already has a fingerprint.
bool lecturer_exists(const String& lecturerUid);

// Check whether a fingerprint ID belongs to a lecturer.
bool lecturer_fingerprintIdInUse(
    const String& fingerprintId
);

// Retrieve the lecturer UID associated with a fingerprint ID.
String lecturer_getByFingerprint(
    const String& fingerprintId
);

// Retrieve the fingerprint ID associated with a lecturer UID.
String lecturer_getFingerprintId(
    const String& lecturerUid
);

// Remove the lecturer mapping that uses this fingerprint ID.
// Returns true when a mapping was removed.
bool lecturer_unbindFingerprint(
    const String& fingerprintId
);

// Export lecturer mappings as JSON.
String lecturer_getAllJSON();

#endif