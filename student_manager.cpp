#include "student_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>

#include "config.h"
#include "sd_manager.h"

// ============================================================
// IN-MEMORY STUDENT DATABASE
// ============================================================

static DynamicJsonDocument studentsDoc(32768);
static bool studentsReady = false;


// ============================================================
// INITIALIZATION
// ============================================================

static void loadStudentsFromSD() {

    if (!sd_isReady()) {
        return;
    }

    // If power was lost between delete and rename, the temp file is the latest copy.
    const char* path = STUDENTS_FILE;

    if (!sd_fileExists(path) && sd_fileExists("/students.tmp")) {
        path = "/students.tmp";
    }

    if (!sd_fileExists(path)) {
        return;
    }

    File file = sd_open(path, FILE_READ);

    if (!file) {
        return;
    }

    DeserializationError error = deserializeJson(studentsDoc, file);

    file.close();

    JsonArray list = studentsDoc["students"].as<JsonArray>();

    if (error || list.isNull()) {

        Serial.println("[STUDENT] Saved student file could not be read. Starting empty.");

        studentsDoc.clear();
        studentsDoc.createNestedArray("students");

        return;
    }

    Serial.printf("[STUDENT] Loaded %u students from the SD card\n", (unsigned int)list.size());
}


bool student_manager_init() {

    studentsDoc.clear();

    // Correctly create the students array.
    studentsDoc.createNestedArray("students");

    if (studentsDoc.overflowed()) {
        studentsReady = false;

        Serial.println(
            "[STUDENT] ERROR: Could not initialize student database"
        );

        return false;
    }

    // Restore students and fingerprint mappings saved on the SD card.
    loadStudentsFromSD();

    studentsReady = true;

    Serial.println(
        "[STUDENT] Student database ready"
    );

    Serial.println(
        "[STUDENT] Student array initialized"
    );

    Serial.println(
        "[STUDENT] Changes are saved to the SD card when it is available"
    );

    return true;
}


// ============================================================
// SAVE
// ============================================================

static bool studentsDirty = false;
static unsigned long lastStudentSave = 0;

static bool saveStudents() {

    if (!studentsReady) {
        return false;
    }

    // The SD card is written by student_manager_flush() shortly after,
    // so a burst of sign-ins causes one write instead of many.
    studentsDirty = true;

    return !studentsDoc.overflowed();
}

void student_manager_flush(bool force) {

    if (!studentsReady || !studentsDirty) {
        return;
    }

    unsigned long now = millis();

    if (!force && now - lastStudentSave < 3000UL) {
        return;
    }

    lastStudentSave = now;

    if (!sd_isReady() && !sd_remount()) {
        return;
    }

    const char* tempPath = "/students.tmp";

    File file = sd_open(tempPath, FILE_WRITE);

    if (!file) {
        return;
    }

    size_t written = serializeJson(studentsDoc, file);

    file.close();

    if (written == 0) {
        sd_deleteFile(tempPath);
        return;
    }

    if (sd_rename(tempPath, STUDENTS_FILE)) {

        studentsDirty = false;

        Serial.println("[STUDENT] Saved to SD card");
    }
}


// ============================================================
// ADD STUDENT
// ============================================================

bool student_add(
    const String& id,
    const String& name,
    const String& matricNumber
) {

    if (!studentsReady) {
        Serial.println(
            "[STUDENT] ERROR: Database is not ready"
        );

        return false;
    }

    String cleanId = id;
    String cleanName = name;
    String cleanMatric = matricNumber;

    cleanId.trim();
    cleanName.trim();
    cleanMatric.trim();

    if (
        cleanId.length() == 0 ||
        cleanMatric.length() == 0
    ) {
        Serial.println(
            "[STUDENT] ERROR: Student ID or registration number is empty"
        );

        return false;
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        Serial.println(
            "[STUDENT] ERROR: Students array is missing"
        );

        return false;
    }

    // --------------------------------------------------------
    // Check existing records
    // --------------------------------------------------------

    for (JsonObject student : students) {

        String existingId =
            student["id"].as<String>();

        String existingMatric =
            student["matricNumber"].as<String>();

        existingId.trim();
        existingMatric.trim();

        // Same internal student ID.
        if (existingId == cleanId) {

            if (cleanName.length() > 0) {
                student["name"] = cleanName;
            }

            student["matricNumber"] = cleanMatric;

            bool saved = saveStudents();

            Serial.printf(
                "[STUDENT] Updated existing student: %s\n",
                cleanId.c_str()
            );

            return saved;
        }

        // Same registration number.
        if (
            existingMatric.equalsIgnoreCase(cleanMatric)
        ) {

            // Preserve the existing internal ID and fingerprint.
            if (cleanName.length() > 0) {
                student["name"] = cleanName;
            }

            bool saved = saveStudents();

            Serial.printf(
                "[STUDENT] Registration number already exists: %s\n",
                cleanMatric.c_str()
            );

            return saved;
        }
    }

    // --------------------------------------------------------
    // Create new student
    // --------------------------------------------------------

    JsonObject student =
        students.createNestedObject();

    if (student.isNull()) {
        Serial.println(
            "[STUDENT] ERROR: Could not create student record"
        );

        return false;
    }

    student["id"] = cleanId;
    student["name"] = cleanName;
    student["matricNumber"] = cleanMatric;
    student["fingerprintId"] = "";
    student["deviceId"] = "";
    student["registered"] = false;

    bool saved = saveStudents();

    if (saved) {

        Serial.printf(
            "[STUDENT] Student created: ID=%s, matric=%s\n",
            cleanId.c_str(),
            cleanMatric.c_str()
        );

    } else {

        Serial.println(
            "[STUDENT] ERROR: Student database capacity exceeded"
        );
    }

    return saved;
}


// ============================================================
// EXISTS
// ============================================================

bool student_exists(
    const String& id
) {

    Student student;

    return student_get(id, student);
}


// ============================================================
// GET BY INTERNAL ID
// ============================================================

bool student_get(
    const String& studentId,
    Student& student
) {

    if (!studentsReady) {
        return false;
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        return false;
    }

    for (JsonObject obj : students) {

        String storedId =
            obj["id"].as<String>();

        if (storedId == studentId) {

            student.id =
                obj["id"].as<String>();

            student.name =
                obj["name"].as<String>();

            student.matricNumber =
                obj["matricNumber"].as<String>();

            student.fingerprintId =
                obj["fingerprintId"].as<String>();

            student.deviceId =
                obj["deviceId"].as<String>();

            student.registered =
                obj["registered"] | false;

            return true;
        }
    }

    return false;
}


// ============================================================
// BIND FINGERPRINT
// ============================================================

bool student_bindFingerprint(
    const String& studentId,
    const String& fingerprintId
) {

    if (!studentsReady) {
        Serial.println(
            "[STUDENT] ERROR: Database is not ready"
        );

        return false;
    }

    String cleanStudentId = studentId;
    String cleanFingerprintId = fingerprintId;

    cleanStudentId.trim();
    cleanFingerprintId.trim();

    if (
        cleanStudentId.length() == 0 ||
        cleanFingerprintId.length() == 0
    ) {
        Serial.println(
            "[STUDENT] ERROR: Empty student ID or fingerprint ID"
        );

        return false;
    }

    int fpId = cleanFingerprintId.toInt();

    if (
        fpId < 1 ||
        fpId > 127
    ) {
        Serial.printf(
            "[STUDENT] ERROR: Invalid fingerprint ID: %s\n",
            cleanFingerprintId.c_str()
        );

        return false;
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        Serial.println(
            "[STUDENT] ERROR: Students array is missing"
        );

        return false;
    }

    // Find the student record first.
    JsonObject targetStudent;

    for (JsonObject student : students) {

        String storedStudentId =
            student["id"].as<String>();

        if (storedStudentId == cleanStudentId) {
            targetStudent = student;
            break;
        }
    }

    if (targetStudent.isNull()) {

        Serial.printf(
            "[STUDENT] ERROR: Student %s not found during binding\n",
            cleanStudentId.c_str()
        );

        return false;
    }

    // Prevent assigning the same fingerprint ID to two students.
    for (JsonObject student : students) {

        String otherStudentId =
            student["id"].as<String>();

        String otherFingerprintId =
            student["fingerprintId"].as<String>();

        otherFingerprintId.trim();

        if (
            otherStudentId != cleanStudentId &&
            otherFingerprintId.length() > 0 &&
            otherFingerprintId.toInt() == fpId
        ) {

            Serial.printf(
                "[STUDENT] ERROR: Fingerprint %d is already assigned to student %s\n",
                fpId,
                otherStudentId.c_str()
            );

            return false;
        }
    }

    // Save the mapping.
    targetStudent["fingerprintId"] =
        String(fpId);

    targetStudent["registered"] = true;

    if (!saveStudents()) {

        Serial.println(
            "[STUDENT] ERROR: Could not save fingerprint mapping"
        );

        return false;
    }

    Serial.println(
        "[STUDENT] Fingerprint mapping saved successfully"
    );

    Serial.printf(
        "[STUDENT] Student ID: %s\n",
        cleanStudentId.c_str()
    );

    Serial.printf(
        "[STUDENT] Fingerprint ID: %d\n",
        fpId
    );

    return true;
}


// ============================================================
// BIND DEVICE
// ============================================================

bool student_bindDevice(
    const String& studentId,
    const String& deviceId
) {

    if (!studentsReady) {
        return false;
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        return false;
    }

    for (JsonObject student : students) {

        if (
            student["id"].as<String>() == studentId
        ) {

            if (student["deviceId"].as<String>() == deviceId) {
                return true;
            }

            student["deviceId"] = deviceId;

            return saveStudents();
        }
    }

    return false;
}


// ============================================================
// GET BY FINGERPRINT
// ============================================================

String student_getByFingerprint(
    const String& fingerprintId
) {

    if (!studentsReady) {

        Serial.println(
            "[STUDENT] ERROR: Student manager is not ready"
        );

        return "";
    }

    String requested = fingerprintId;
    requested.trim();

    if (requested.length() == 0) {

        Serial.println(
            "[STUDENT] ERROR: Empty fingerprint ID"
        );

        return "";
    }

    int requestedId = requested.toInt();

    if (
        requestedId < 1 ||
        requestedId > 127
    ) {

        Serial.printf(
            "[STUDENT] ERROR: Invalid fingerprint ID: %s\n",
            requested.c_str()
        );

        return "";
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {

        Serial.println(
            "[STUDENT] ERROR: Students array is missing"
        );

        return "";
    }

    Serial.printf(
        "[STUDENT] Looking up fingerprint ID: %d\n",
        requestedId
    );

    for (JsonObject student : students) {

        String storedFingerprintId =
            student["fingerprintId"].as<String>();

        storedFingerprintId.trim();

        if (storedFingerprintId.length() == 0) {
            continue;
        }

        int storedId =
            storedFingerprintId.toInt();

        Serial.printf(
            "[STUDENT] Checking student=%s, fingerprint=%s\n",
            student["id"].as<String>().c_str(),
            storedFingerprintId.c_str()
        );

        if (storedId == requestedId) {

            String studentId =
                student["id"].as<String>();

            Serial.printf(
                "[STUDENT] MATCH: fingerprint %d -> student %s\n",
                requestedId,
                studentId.c_str()
            );

            return studentId;
        }
    }

    Serial.printf(
        "[STUDENT] ERROR: No student mapped to fingerprint ID %d\n",
        requestedId
    );

    return "";
}


// ============================================================
// GET BY DEVICE
// ============================================================

String student_getByDevice(
    const String& deviceId
) {

    if (!studentsReady) {
        return "";
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        return "";
    }

    for (JsonObject student : students) {

        if (
            student["deviceId"].as<String>() == deviceId
        ) {

            return student["id"].as<String>();
        }
    }

    return "";
}


// ============================================================
// GET BY REGISTRATION NUMBER
// ============================================================

String student_getByMatric(
    const String& matricNumber
) {

    if (!studentsReady) {
        return "";
    }

    String requested = matricNumber;
    requested.trim();

    if (requested.length() == 0) {
        return "";
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        return "";
    }

    for (JsonObject student : students) {

        String candidate =
            student["matricNumber"].as<String>();

        candidate.trim();

        if (
            candidate.equalsIgnoreCase(requested)
        ) {

            return student["id"].as<String>();
        }
    }

    return "";
}


// ============================================================
// ENSURE STUDENT EXISTS BY MATRIC
// ============================================================

String student_ensureByMatric(
    const String& matricNumber
) {

    if (!studentsReady) {
        return "";
    }

    String matric = matricNumber;
    matric.trim();

    if (matric.length() == 0) {
        return "";
    }

    // Check for an existing student.
    String existingId =
        student_getByMatric(matric);

    if (existingId.length() > 0) {

        Serial.printf(
            "[STUDENT] Existing student found: %s\n",
            existingId.c_str()
        );

        return existingId;
    }

    // Use registration number as the internal ID.
    bool added = student_add(
        matric,
        "",
        matric
    );

    if (!added) {

        Serial.println(
            "[STUDENT] ERROR: Could not create student placeholder"
        );

        return "";
    }

    // Verify that the record now exists.
    String createdId =
        student_getByMatric(matric);

    if (createdId.length() == 0) {

        Serial.println(
            "[STUDENT] ERROR: Created student could not be retrieved"
        );

        return "";
    }

    Serial.printf(
        "[STUDENT] Created local placeholder: %s\n",
        createdId.c_str()
    );

    return createdId;
}


// ============================================================
// NEXT FINGERPRINT ID
// ============================================================

int student_nextFingerprintId() {

    if (!studentsReady) {
        return -1;
    }

    bool used[128] = { false };

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) {
        return -1;
    }

    for (JsonObject student : students) {

        String fpId =
            student["fingerprintId"].as<String>();

        fpId.trim();

        if (fpId.length() == 0) {
            continue;
        }

        int idNum = fpId.toInt();

        if (
            idNum >= 1 &&
            idNum <= 127
        ) {

            used[idNum] = true;
        }
    }

    for (int i = 1; i <= 127; i++) {

        if (!used[i]) {
            return i;
        }
    }

    Serial.println(
        "[STUDENT] ERROR: No free fingerprint IDs"
    );

    return -1;
}


// ============================================================
// GET ALL
// ============================================================

String student_getAllJSON() {

    if (!studentsReady) {
        return "{\"students\":[]}";
    }

    String output;

    serializeJson(
        studentsDoc,
        output
    );

    return output;
}


// ============================================================
// UNBIND A FINGERPRINT ID FROM STUDENT RECORDS
// ============================================================

int student_unbindFingerprint(
    const String& fingerprintId
) {

    if (!studentsReady) return 0;

    int target = fingerprintId.toInt();

    if (target < 1) return 0;

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    if (students.isNull()) return 0;

    int cleared = 0;

    for (JsonObject student : students) {

        String stored = student["fingerprintId"] | "";

        stored.trim();

        if (
            stored.length() > 0 &&
            stored.toInt() == target
        ) {
            student["fingerprintId"] = "";
            cleared++;
        }
    }

    if (cleared > 0) {
        saveStudents();
        student_manager_flush(true);
    }

    return cleared;
}
