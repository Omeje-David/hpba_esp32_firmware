#include "attendance_manager.h"
#include "session_manager.h"
#include "student_manager.h"

#include <ArduinoJson.h>

//
// Attendance lives in RAM for the current power-up and is retrieved by
// the Flutter app through /api/attendance. Every finished session is
// also written to the SD card by session_store.cpp.
//

static const size_t ATTENDANCE_DOC_SIZE = 49152;

static DynamicJsonDocument attendanceDoc(ATTENDANCE_DOC_SIZE);
static bool attendanceReady = false;

static void resetDoc() {

    attendanceDoc.clear();
    attendanceDoc.createNestedArray("records");
}

bool attendance_manager_init() {

    resetDoc();

    attendanceReady = true;

    Serial.println("[ATTENDANCE] In-memory attendance manager ready");

    return true;
}

void attendance_compactIfNeeded() {

    if (!attendanceReady) return;

    size_t used = attendanceDoc.memoryUsage();

    if (used < (ATTENDANCE_DOC_SIZE * 6) / 10) return;

    Serial.printf(
        "[ATTENDANCE] Buffer %u/%u bytes. Clearing old sessions "
        "(they are saved on the SD card).\n",
        (unsigned)used,
        (unsigned)ATTENDANCE_DOC_SIZE
    );

    resetDoc();
}

bool attendance_alreadyMarked(
    const String& studentId,
    const String& sessionId
) {

    if (!attendanceReady) return false;

    JsonArray records = attendanceDoc["records"].as<JsonArray>();

    for (JsonObject record : records) {

        if (
            record["studentId"].as<String>() == studentId &&
            record["sessionId"].as<String>() == sessionId
        ) {
            return true;
        }
    }

    return false;
}

bool attendance_mark(
    const String& studentId,
    const String& sessionId,
    AttendanceMethod method,
    int rssi
) {

    if (!attendanceReady) return false;

    if (studentId.length() == 0 || sessionId.length() == 0) {
        return false;
    }

    if (attendance_alreadyMarked(studentId, sessionId)) {

        Serial.println("[ATTENDANCE] Already marked");
        return false;
    }

    JsonArray records = attendanceDoc["records"].as<JsonArray>();

    JsonObject record = records.createNestedObject();

    if (record.isNull() || attendanceDoc.overflowed()) {

        Serial.println("[ATTENDANCE] ERROR: attendance buffer is full");
        return false;
    }

    record["studentId"] = studentId;
    record["sessionId"] = sessionId;

    unsigned long epochNow = session_getEpochNow();

    record["timestamp"] = epochNow > 0 ? epochNow : millis();

    switch (method) {

        case ATTENDANCE_DEVICE:
            record["method"] = "device";
            record["rssi"] = String(rssi);
            break;

        case ATTENDANCE_FINGERPRINT:
            record["method"] = "fingerprint";
            record["rssi"] = "biometric";
            break;

        case ATTENDANCE_BOTH:
            record["method"] = "both";
            record["rssi"] = String(rssi);
            break;
    }

    record["synced"] = false;

    if (attendanceDoc.overflowed()) {

        Serial.println("[ATTENDANCE] ERROR: attendance buffer overflowed");
        return false;
    }

    Serial.print("[ATTENDANCE] Marked: ");
    Serial.println(studentId);

    return true;
}

String attendance_getSessionJSON(const String& sessionId) {

    DynamicJsonDocument response(16384);

    JsonArray result = response.createNestedArray("attendance");

    if (attendanceReady) {

        JsonArray records = attendanceDoc["records"].as<JsonArray>();

        for (JsonObject record : records) {

            if (record["sessionId"].as<String>() != sessionId) continue;

            JsonObject item = result.createNestedObject();

            item["studentId"] = record["studentId"];
            item["sessionId"] = record["sessionId"];
            item["timestamp"] = record["timestamp"];
            item["rssi"] = record["rssi"];
            item["method"] = record["method"];
            item["synced"] = record["synced"];
        }
    }

    String output;
    serializeJson(response, output);

    return output;
}

String attendance_getAllJSON(const String& sessionId) {

    DynamicJsonDocument response(16384);

    JsonArray result = response.createNestedArray("attendance");

    if (attendanceReady) {

        JsonArray records = attendanceDoc["records"].as<JsonArray>();

        for (JsonObject record : records) {

            String storedSessionId = record["sessionId"].as<String>();

            if (sessionId.length() > 0 && storedSessionId != sessionId) {
                continue;
            }

            JsonObject item = result.createNestedObject();

            item["studentId"] = record["studentId"];
            item["sessionId"] = record["sessionId"];
            item["timestamp"] = record["timestamp"];
            item["rssi"] = record["rssi"];
            item["method"] = record["method"];
            item["synced"] = record["synced"];
        }
    }

    response["count"] = result.size();

    String output;
    serializeJson(response, output);

    return output;
}

int attendance_writeSessionRecords(
    const String& sessionId,
    Print& out
) {

    if (!attendanceReady) return 0;

    int written = 0;

    JsonArray records = attendanceDoc["records"].as<JsonArray>();

    for (JsonObject record : records) {

        if (record["sessionId"].as<String>() != sessionId) continue;

        String studentId = record["studentId"].as<String>();

        StaticJsonDocument<512> item;

        item["studentId"] = studentId;

        Student student;

        if (student_get(studentId, student) && student.name.length() > 0) {
            item["studentName"] = student.name;
        }

        item["timestamp"] = record["timestamp"];
        item["method"] = record["method"];
        item["rssi"] = record["rssi"];

        if (written > 0) {
            out.print(",\n");
        }

        serializeJson(item, out);

        written++;
    }

    return written;
}
