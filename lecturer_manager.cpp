#include "lecturer_manager.h"
#include "config.h"
#include "sd_manager.h"

#include <ArduinoJson.h>
#include <FS.h>

// ============================================================
// LECTURER STORAGE
// ============================================================

#define MAX_LECTURERS 127

struct LecturerRecord {
    String lecturerUid;
    String fingerprintId;
};

static LecturerRecord lecturers[MAX_LECTURERS];

static int lecturerCount = 0;

// ============================================================
// SD CARD PERSISTENCE
//
// The fingerprint sensor keeps the fingerprints, but only this file
// remembers which lecturer account each fingerprint belongs to.
// Without it an admin fingerprint would stop working after a reboot.
// ============================================================

static void saveLecturers() {

    if (!sd_isReady() && !sd_remount()) {

        Serial.println("[LECTURER] SD card unavailable. Mapping is kept in RAM only.");
        return;
    }

    DynamicJsonDocument doc(16384);

    JsonArray list = doc.createNestedArray("lecturers");

    for (int i = 0; i < lecturerCount; i++) {

        JsonObject item = list.createNestedObject();

        item["uid"] = lecturers[i].lecturerUid;
        item["fp"] = lecturers[i].fingerprintId;
    }

    const char* tempPath = "/lect.tmp";

    File file = sd_open(tempPath, FILE_WRITE);

    if (!file) {
        return;
    }

    size_t written = serializeJson(doc, file);

    file.close();

    if (written == 0) {
        sd_deleteFile(tempPath);
        return;
    }

    if (sd_rename(tempPath, LECTURERS_FILE)) {
        Serial.println("[LECTURER] Saved to SD card");
    }
}

static void loadLecturers() {

    if (!sd_isReady()) {
        return;
    }

    const char* path = LECTURERS_FILE;

    if (!sd_fileExists(path) && sd_fileExists("/lect.tmp")) {
        path = "/lect.tmp";
    }

    if (!sd_fileExists(path)) {
        return;
    }

    File file = sd_open(path, FILE_READ);

    if (!file) {
        return;
    }

    DynamicJsonDocument doc(16384);

    DeserializationError error = deserializeJson(doc, file);

    file.close();

    if (error) {

        Serial.println("[LECTURER] Saved lecturer file could not be read");
        return;
    }

    for (JsonObject item : doc["lecturers"].as<JsonArray>()) {

        if (lecturerCount >= MAX_LECTURERS) break;

        String uid = item["uid"].as<String>();
        String fp = item["fp"].as<String>();

        if (uid.length() == 0 || fp.length() == 0) continue;

        lecturers[lecturerCount].lecturerUid = uid;
        lecturers[lecturerCount].fingerprintId = fp;

        lecturerCount++;
    }

    Serial.printf("[LECTURER] Loaded %d lecturer fingerprint(s) from the SD card\n", lecturerCount);
}

// ============================================================
// INITIALIZATION
// ============================================================

void lecturer_manager_init() {

    lecturerCount = 0;

    for (int i = 0; i < MAX_LECTURERS; i++) {
        lecturers[i].lecturerUid = "";
        lecturers[i].fingerprintId = "";
    }

    loadLecturers();

    Serial.println(
        "[LECTURER] Lecturer manager initialized"
    );
}

// ============================================================
// FIND LECTURER BY UID
// ============================================================

static int findLecturerByUid(
    const String& lecturerUid
) {

    for (int i = 0; i < lecturerCount; i++) {

        if (
            lecturers[i].lecturerUid ==
            lecturerUid
        ) {
            return i;
        }
    }

    return -1;
}

// ============================================================
// FIND LECTURER BY FINGERPRINT ID
// ============================================================

static int findLecturerByFingerprintId(
    const String& fingerprintId
) {

    for (int i = 0; i < lecturerCount; i++) {

        if (
            lecturers[i].fingerprintId ==
            fingerprintId
        ) {
            return i;
        }
    }

    return -1;
}

// ============================================================
// CHECK LECTURER EXISTS
// ============================================================

bool lecturer_exists(
    const String& lecturerUid
) {

    if (lecturerUid.length() == 0) {
        return false;
    }

    return findLecturerByUid(
        lecturerUid
    ) >= 0;
}

// ============================================================
// CHECK FINGERPRINT ID IN USE
// ============================================================

bool lecturer_fingerprintIdInUse(
    const String& fingerprintId
) {

    if (fingerprintId.length() == 0) {
        return false;
    }

    return findLecturerByFingerprintId(
        fingerprintId
    ) >= 0;
}

// ============================================================
// GET LECTURER BY FINGERPRINT
// ============================================================

String lecturer_getByFingerprint(
    const String& fingerprintId
) {

    int index =
        findLecturerByFingerprintId(
            fingerprintId
        );

    if (index < 0) {
        return "";
    }

    return lecturers[index].lecturerUid;
}

// ============================================================
// GET FINGERPRINT ID BY LECTURER UID
// ============================================================

String lecturer_getFingerprintId(
    const String& lecturerUid
) {

    int index =
        findLecturerByUid(
            lecturerUid
        );

    if (index < 0) {
        return "";
    }

    return lecturers[index].fingerprintId;
}

// ============================================================
// BIND LECTURER TO FINGERPRINT
// ============================================================

bool lecturer_bindFingerprint(
    const String& lecturerUid,
    const String& fingerprintId
) {

    if (
        lecturerUid.length() == 0 ||
        fingerprintId.length() == 0
    ) {
        Serial.println(
            "[LECTURER] Empty UID or fingerprint ID"
        );

        return false;
    }

    int numericId =
        fingerprintId.toInt();

    if (
        numericId < 1 ||
        numericId > 127
    ) {
        Serial.println(
            "[LECTURER] Invalid fingerprint ID"
        );

        return false;
    }

    // If this lecturer already has a mapping,
    // permit the same mapping but reject a different one.
    int existingUidIndex =
        findLecturerByUid(
            lecturerUid
        );

    if (existingUidIndex >= 0) {

        if (
            lecturers[existingUidIndex].fingerprintId ==
            fingerprintId
        ) {

            Serial.println(
                "[LECTURER] Existing mapping confirmed"
            );

            return true;
        }

        Serial.println(
            "[LECTURER] Lecturer already has another fingerprint"
        );

        return false;
    }

    // Do not assign one fingerprint ID to two lecturers.
    if (
        findLecturerByFingerprintId(
            fingerprintId
        ) >= 0
    ) {

        Serial.println(
            "[LECTURER] Fingerprint ID already assigned to a lecturer"
        );

        return false;
    }

    if (lecturerCount >= MAX_LECTURERS) {

        Serial.println(
            "[LECTURER] Lecturer storage is full"
        );

        return false;
    }

    lecturers[lecturerCount].lecturerUid =
        lecturerUid;

    lecturers[lecturerCount].fingerprintId =
        fingerprintId;

    lecturerCount++;

    saveLecturers();

    Serial.println(
        "[LECTURER] Fingerprint mapping saved"
    );

    Serial.print(
        "[LECTURER] UID: "
    );

    Serial.println(
        lecturerUid
    );

    Serial.print(
        "[LECTURER] Fingerprint ID: "
    );

    Serial.println(
        fingerprintId
    );

    return true;
}

// ============================================================
// EXPORT LECTURERS AS JSON
// ============================================================

String lecturer_getAllJSON() {

    DynamicJsonDocument doc(8192);

    JsonArray lecturersArray =
        doc.createNestedArray(
            "lecturers"
        );

    for (int i = 0; i < lecturerCount; i++) {

        JsonObject item =
            lecturersArray.createNestedObject();

        item["lecturerUid"] =
            lecturers[i].lecturerUid;

        item["fingerprintId"] =
            lecturers[i].fingerprintId;
    }

    doc["count"] = lecturerCount;

    String output;

    serializeJson(
        doc,
        output
    );

    return output;
}

// ============================================================
// REMOVE A LECTURER MAPPING
// ============================================================

bool lecturer_unbindFingerprint(
    const String& fingerprintId
) {

    int index =
        findLecturerByFingerprintId(
            fingerprintId
        );

    if (index < 0) {
        return false;
    }

    for (int i = index; i < lecturerCount - 1; i++) {
        lecturers[i] = lecturers[i + 1];
    }

    lecturerCount--;

    lecturers[lecturerCount].lecturerUid = "";
    lecturers[lecturerCount].fingerprintId = "";

    saveLecturers();

    Serial.println(
        "[LECTURER] Mapping removed"
    );

    return true;
}
