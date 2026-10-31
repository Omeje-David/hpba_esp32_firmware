#include "session_store.h"
#include "config.h"
#include "sd_manager.h"
#include "esp_clock.h"
#include "attendance_manager.h"

#include <SD.h>

static String lastName = "";


// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

// Keeps letters, digits, '_' and '-'. Everything else is dropped.
static String sanitize(const String& input, unsigned int maxLength) {

    String out;

    for (unsigned int i = 0; i < input.length() && out.length() < maxLength; i++) {

        char c = input[i];

        bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';

        if (ok) out += c;
    }

    return out;
}

// Writes a quoted, escaped JSON string.
static void printJsonString(Print& out, const String& value) {

    out.print('"');

    for (unsigned int i = 0; i < value.length(); i++) {

        char c = value[i];

        if (c == '"' || c == '\\') {
            out.print('\\');
            out.print(c);
        } else if ((uint8_t)c < 0x20) {
            out.print(' ');
        } else {
            out.print(c);
        }
    }

    out.print('"');
}

static void printField(Print& out, const char* key, const String& value, bool last = false) {

    out.print('"');
    out.print(key);
    out.print("\":");
    printJsonString(out, value);
    out.print(last ? "\n" : ",\n");
}

static void printNumber(Print& out, const char* key, unsigned long value) {

    out.print('"');
    out.print(key);
    out.print("\":");
    out.print(value);
    out.print(",\n");
}

static void printBool(Print& out, const char* key, bool value) {

    out.print('"');
    out.print(key);
    out.print("\":");
    out.print(value ? "true" : "false");
    out.print(",\n");
}

static String buildPath(const String& dir, const String& name) {
    return dir + "/" + name + SESSION_FILE_EXT;
}


// ------------------------------------------------------------
// Init
// ------------------------------------------------------------

void session_store_init() {

    if (!sd_isReady()) {

        Serial.println("[STORE] SD card not available. Sessions will not be saved to SD.");
        return;
    }

    sd_mkdir(COURSES_DIR);
    sd_mkdir(EMERGENCY_DIR);

    Serial.println("[STORE] Session folders ready");
}

String session_store_lastName() {
    return lastName;
}


// ------------------------------------------------------------
// Save one finished session
// ------------------------------------------------------------

bool session_store_saveCompleted(
    const AttendanceSession& s,
    unsigned long endEpoch,
    unsigned long endMillis
) {

    lastName = "";

    if (!sd_isReady() && !sd_remount()) {

        Serial.println("[STORE] SD card unavailable. Session not saved to SD.");
        return false;
    }

    bool emergency = s.fingerprintStarted;

    // --------------------------------------------------------
    // Folder and base name
    // --------------------------------------------------------

    String dir;
    String base;

    String stamp =
        s.startEpoch > 0
            ? clock_localStamp(s.startEpoch)
            : "T" + String(s.startTime / 1000UL);

    if (emergency) {

        dir = EMERGENCY_DIR;

        base = sanitize(s.sessionId, 40);

        if (base.length() == 0) {
            base = "EMERGENCY_" + stamp;
        }

    } else {

        String code = sanitize(s.courseCode, 20);
        code.toUpperCase();

        if (code.length() == 0) code = "UNKNOWN";

        sd_mkdir(COURSES_DIR);

        dir = String(COURSES_DIR) + "/" + code;

        if (!sd_mkdir(dir.c_str())) {
            dir = COURSES_DIR;
        }

        base = code + "_" + stamp;
    }

    sd_mkdir(dir.c_str());

    // --------------------------------------------------------
    // Unique name
    // --------------------------------------------------------

    String name = base;

    for (int n = 2; n < 100 && sd_fileExists(buildPath(dir, name).c_str()); n++) {
        name = base + "_" + String(n);
    }

    String finalPath = buildPath(dir, name);
    String tempPath = dir + "/" + name + ".TMP";

    // --------------------------------------------------------
    // Open the temp file. If the long name is rejected, fall back
    // to an 8.3 name in the top folder.
    // --------------------------------------------------------

    File file = sd_open(tempPath.c_str(), FILE_WRITE);

    if (!file) {

        Serial.println("[STORE] Long file name rejected. Using a short name.");

        dir = emergency ? String(EMERGENCY_DIR) : String(COURSES_DIR);

        char shortName[10];
        snprintf(shortName, sizeof(shortName), "S%07lu", (s.startTime / 1000UL) % 10000000UL);

        name = String(shortName);

        for (int n = 1; n < 100 && sd_fileExists(buildPath(dir, name).c_str()); n++) {
            snprintf(shortName, sizeof(shortName), "S%06lu%01d",
                     (s.startTime / 1000UL) % 1000000UL, n % 10);
            name = String(shortName);
        }

        finalPath = buildPath(dir, name);
        tempPath = dir + "/" + name + ".TMP";

        file = sd_open(tempPath.c_str(), FILE_WRITE);
    }

    if (!file) {

        Serial.println("[STORE] Could not create the session file.");
        return false;
    }

    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

    bool approx = clock_isApprox();

    file.print("{\n");

    printField(file, "sessionName", base);
    printField(file, "sessionId", s.sessionId);
    printField(file, "type", emergency ? String("emergency") : s.sessionType);
    printField(file, "adminId", s.startedByLecturerUid);
    printField(file, "courseId", s.courseId);
    printField(file, "courseCode", s.courseCode);
    printField(file, "courseTitle", s.courseTitle);

    printNumber(file, "startEpoch", s.startEpoch);
    printNumber(file, "endEpoch", endEpoch);
    printNumber(file, "startMillis", s.startTime);
    printNumber(file, "endMillis", endMillis);

    printField(file, "bootId", clock_bootId());
    printBool(file, "clockApprox", approx);

    // --------------------------------------------------------
    // Records
    // --------------------------------------------------------

    file.print("\"records\":[\n");

    int count = attendance_writeSessionRecords(s.sessionId, file);

    file.print("\n],\n");

    file.print("\"count\":");
    file.print(count);
    file.print("\n}\n");

    file.close();

    // --------------------------------------------------------
    // Publish (temp file becomes the real file)
    // --------------------------------------------------------

    if (!sd_rename(tempPath.c_str(), finalPath.c_str())) {

        Serial.println("[STORE] Could not finalize the session file.");
        sd_deleteFile(tempPath.c_str());

        return false;
    }

    lastName = name;

    Serial.print("[STORE] Saved ");
    Serial.print(finalPath);
    Serial.print(" (");
    Serial.print(count);
    Serial.println(" records)");

    return true;
}
