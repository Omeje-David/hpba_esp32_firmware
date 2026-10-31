#include "firebase_sync.h"
#include "config.h"
#include "esp_clock.h"
#include "sd_manager.h"
#include "oled_manager.h"
#include "wifi_module.h"
#include "captive_portal.h"
#include "api_server.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>

static String idToken = "";


// ============================================================
// SMALL HELPERS
// ============================================================

static bool firebaseConfigured() {

    return
        strcmp(FIREBASE_PROJECT_ID, "YOUR_FIREBASE_PROJECT_ID") != 0 &&
        strcmp(FIREBASE_API_KEY, "YOUR_FIREBASE_WEB_API_KEY") != 0 &&
        strlen(FIREBASE_PROJECT_ID) > 0 &&
        strlen(FIREBASE_API_KEY) > 0;
}

static String lastComponent(const String& path) {

    int i = path.lastIndexOf('/');

    return i >= 0 ? path.substring(i + 1) : path;
}

static String jsonEscape(const String& in) {

    String out;
    out.reserve(in.length() + 8);

    for (unsigned int i = 0; i < in.length(); i++) {

        char c = in[i];

        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((uint8_t)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }

    return out;
}

static String safeId(const String& in) {

    String out;

    for (unsigned int i = 0; i < in.length(); i++) {

        char c = in[i];

        bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';

        if (ok) out += c;
    }

    return out;
}

// Firestore REST typed values.
static String sv(const String& v) {
    return "{\"stringValue\":\"" + jsonEscape(v) + "\"}";
}

static String iv(long v) {
    return "{\"integerValue\":\"" + String(v) + "\"}";
}

static String bv(bool v) {
    return String("{\"booleanValue\":") + (v ? "true" : "false") + "}";
}

static String tv(unsigned long epoch) {
    return "{\"timestampValue\":\"" + clock_isoUtc(epoch) + "\"}";
}


// ============================================================
// SD QUEUE
// ============================================================

// One file name per line: every *.JSN with no matching .OK marker.
static String pendingFiles(int& count) {

    String out;
    count = 0;

    if (!sd_isReady()) return out;

    File dir = SD.open(EMERGENCY_DIR);

    if (!dir) return out;

    if (!dir.isDirectory()) {
        dir.close();
        return out;
    }

    File f = dir.openNextFile();

    while (f) {

        String name = lastComponent(String(f.name()));
        bool isDir = f.isDirectory();

        f.close();

        String upper = name;
        upper.toUpperCase();

        if (!isDir && upper.endsWith(SESSION_FILE_EXT)) {

            String base = name.substring(0, name.length() - strlen(SESSION_FILE_EXT));
            String marker = String(EMERGENCY_DIR) + "/" + base + SYNCED_MARKER_EXT;

            if (!SD.exists(marker.c_str())) {
                out += name + "\n";
                count++;
            }
        }

        f = dir.openNextFile();
    }

    dir.close();

    return out;
}

int cloud_pendingCount() {

    int count = 0;
    pendingFiles(count);

    return count;
}


// ============================================================
// NETWORK
// ============================================================

static bool joinHotspot() {

    // The student access point is switched off while we are away.
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(300);

    WiFi.mode(WIFI_STA);

    for (int attempt = 1; attempt <= HOTSPOT_ATTEMPTS; attempt++) {

        WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);

        unsigned long start = millis();

        while (WiFi.status() != WL_CONNECTED) {

            unsigned long waited = millis() - start;

            if (waited > HOTSPOT_CONNECT_TIMEOUT_MS) break;

            oled_show(
                "Do not power off",
                "Joining hotspot...",
                String(HOTSPOT_SSID).substring(0, 21),
                "Try " + String(attempt) + "/" + String(HOTSPOT_ATTEMPTS) +
                    "  " + String(waited / 1000UL) + "s"
            );

            delay(500);
        }

        if (WiFi.status() == WL_CONNECTED) {

            Serial.print("[CLOUD] Joined hotspot, IP ");
            Serial.println(WiFi.localIP());

            return true;
        }

        WiFi.disconnect();
        delay(500);
    }

    return false;
}

static void restoreAccessPoint() {

    oled_show("Do not power off", "Restarting student", "Wi-Fi...", "");

    WiFi.disconnect(true);
    delay(300);

    wifi_init();
    captive_portal_init();
    api_server_restart();

    Serial.println("[CLOUD] Student access point restored");
}


// ============================================================
// FIREBASE
// ============================================================

static bool signIn(WiFiClientSecure& client) {

    HTTPClient http;

    http.setReuse(false);
    http.setTimeout(20000);

    String url =
        String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
        FIREBASE_API_KEY;

    if (!http.begin(client, url)) {
        return false;
    }

    // The server's Date header gives the ESP32 the correct time.
    const char* headerKeys[] = {"Date"};
    http.collectHeaders(headerKeys, 1);

    http.addHeader("Content-Type", "application/json");

    String body =
        String("{\"email\":\"") + jsonEscape(FIREBASE_ESP_EMAIL) +
        "\",\"password\":\"" + jsonEscape(FIREBASE_ESP_PASSWORD) +
        "\",\"returnSecureToken\":true}";

    int code = http.POST(body);

    if (code != 200) {

        Serial.print("[CLOUD] Sign-in failed, HTTP ");
        Serial.println(code);

        if (code > 0) {
            Serial.println(http.getString());
        }

        http.end();
        client.stop();

        return false;
    }

    String dateHeader = http.header("Date");

    if (dateHeader.length() > 0) {
        clock_setFromHttpDate(dateHeader);
    }

    DynamicJsonDocument res(4096);

    DeserializationError err = deserializeJson(res, http.getString());

    http.end();
    client.stop();

    if (err) return false;

    idToken = res["idToken"].as<String>();

    return idToken.length() > 20;
}

// Result of one upload.
enum UploadResult {
    UPLOAD_FAILED = 0,
    UPLOAD_DONE,
    UPLOAD_SKIPPED_EMPTY
};

static UploadResult uploadSession(
    WiFiClientSecure& client,
    const String& fileName
) {

    String path = String(EMERGENCY_DIR) + "/" + fileName;

    String body;
    String docId;

    // --------------------------------------------------------
    // Read the file and build the Firestore request. The parsed
    // JSON and the file text are released before the network call
    // so the TLS connection has enough memory.
    // --------------------------------------------------------

    {
        String text = sd_readFile(path.c_str());

        if (text.length() == 0) {
            Serial.println("[CLOUD] Empty or unreadable file: " + path);
            return UPLOAD_FAILED;
        }

        size_t capacity = text.length() * 2 + 2048;
        if (capacity > 60000) capacity = 60000;

        DynamicJsonDocument in(capacity);

        if (deserializeJson(in, text)) {

            Serial.println("[CLOUD] Invalid JSON in " + path);
            return UPLOAD_FAILED;
        }

        text = "";

        String adminId = in["adminId"].as<String>();

        if (adminId.length() == 0) {

            Serial.println("[CLOUD] Session has no admin id. Cannot upload.");
            return UPLOAD_FAILED;
        }

        JsonArray records = in["records"].as<JsonArray>();

        if (records.isNull() || records.size() == 0) {
            return UPLOAD_SKIPPED_EMPTY;
        }

        String sessionName = in["sessionName"].as<String>();
        String sessionId = in["sessionId"].as<String>();

        unsigned long startEpoch = in["startEpoch"] | 0UL;
        unsigned long endEpoch = in["endEpoch"] | 0UL;
        unsigned long startMs = in["startMillis"] | 0UL;
        unsigned long endMs = in["endMillis"] | 0UL;

        bool approx = in["clockApprox"] | false;
        String bootId = in["bootId"].as<String>();

        // ----------------------------------------------------
        // Time correction. If the session was recorded with an
        // approximate clock but the ESP32 has not rebooted since,
        // the real time is now known and the session is corrected.
        // ----------------------------------------------------

        long delta = 0;
        bool corrected = false;

        if (
            (approx || startEpoch == 0) &&
            bootId == clock_bootId() &&
            clock_isSet() &&
            !clock_isApprox()
        ) {

            unsigned long trueStart = clock_epochAtMillis(startMs);
            unsigned long trueEnd = clock_epochAtMillis(endMs);

            if (trueStart > 0) {

                delta = startEpoch > 0 ? (long)trueStart - (long)startEpoch : 0;

                startEpoch = trueStart;
                endEpoch = trueEnd;
                corrected = true;

                if (sessionName.startsWith("EMERGENCY_")) {
                    sessionName = "EMERGENCY_" + clock_localStamp(trueStart);
                }

                Serial.println("[CLOUD] Session time corrected from server time");
            }
        }

        bool stillApprox = approx && !corrected;
        bool clockUnknown = startEpoch == 0;

        // The SD file name is unique, so the document id is too.
        String baseName = fileName.substring(0, fileName.length() - strlen(SESSION_FILE_EXT));

        docId = safeId(adminId) + "_" + safeId(baseName);

        // ----------------------------------------------------
        // Records
        // ----------------------------------------------------

        String ids;
        String recs;

        int count = 0;

        for (JsonObject r : records) {

            String studentId = r["studentId"].as<String>();
            String studentName = r["studentName"].as<String>();
            String method = r["method"].as<String>();
            String rssi = r["rssi"].as<String>();

            unsigned long ts = r["timestamp"] | 0UL;

            if (ts > 0 && ts < 1700000000UL) {

                // Stored as millis() because no clock existed.
                unsigned long fixed = corrected ? clock_epochAtMillis(ts) : 0;
                ts = fixed;

            } else if (ts > 0 && corrected) {

                ts = (unsigned long)((long)ts + delta);
            }

            if (count > 0) {
                ids += ",";
                recs += ",";
            }

            ids += sv(studentId);

            recs += "{\"mapValue\":{\"fields\":{";
            recs += "\"studentId\":" + sv(studentId) + ",";

            if (studentName.length() > 0) {
                recs += "\"studentName\":" + sv(studentName) + ",";
            }

            recs += "\"method\":" + sv(method) + ",";
            recs += "\"rssi\":" + sv(rssi) + ",";
            recs += "\"timestamp\":" + iv((long)ts);
            recs += "}}}";

            count++;
        }

        // ----------------------------------------------------
        // Document
        // ----------------------------------------------------

        body.reserve(ids.length() + recs.length() + 900);

        body += "{\"fields\":{";

        body += "\"adminId\":" + sv(adminId) + ",";
        body += "\"sessionName\":" + sv(sessionName) + ",";
        body += "\"sessionId\":" + sv(sessionId) + ",";
        body += "\"type\":" + sv("unclaimed") + ",";
        body += "\"source\":" + sv("esp32") + ",";
        body += "\"roomLabel\":" + sv("ESP32 attendance device") + ",";
        body += "\"clockUnknown\":" + bv(clockUnknown) + ",";
        body += "\"clockApprox\":" + bv(stillApprox) + ",";
        body += "\"scannedCount\":" + iv(count) + ",";

        if (startEpoch > 0) {
            body += "\"startedAt\":" + tv(startEpoch) + ",";
            body += "\"timestamp\":" + tv(endEpoch > 0 ? endEpoch : startEpoch) + ",";
        }

        if (endEpoch > 0) {
            body += "\"endedAt\":" + tv(endEpoch) + ",";
        }

        body += "\"studentIds\":{\"arrayValue\":{\"values\":[" + ids + "]}},";
        body += "\"records\":{\"arrayValue\":{\"values\":[" + recs + "]}}";

        body += "}}";
    }

    // --------------------------------------------------------
    // Send. "exists=false" makes this create-only, so a session that
    // was already uploaded (and perhaps already assigned by the admin)
    // can never be overwritten by a retry.
    // --------------------------------------------------------

    String url =
        String("https://firestore.googleapis.com/v1/projects/") +
        FIREBASE_PROJECT_ID +
        "/databases/(default)/documents/hardware_logs/" +
        docId +
        "?currentDocument.exists=false";

    HTTPClient http;

    http.setReuse(false);
    http.setTimeout(25000);

    if (!http.begin(client, url)) {
        return UPLOAD_FAILED;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + idToken);

    int code = http.PATCH(body);

    String reply = code > 0 ? http.getString() : String("");

    http.end();
    client.stop();

    if (code == 200) {

        Serial.println("[CLOUD] Uploaded " + docId);
        return UPLOAD_DONE;
    }

    // Already exists = an earlier attempt succeeded.
    if (
        code == 409 ||
        (code == 400 && (
            reply.indexOf("ALREADY_EXISTS") >= 0 ||
            reply.indexOf("FAILED_PRECONDITION") >= 0 ||
            reply.indexOf("already exists") >= 0))
    ) {

        Serial.println("[CLOUD] Already uploaded earlier: " + docId);
        return UPLOAD_DONE;
    }

    Serial.print("[CLOUD] Upload failed, HTTP ");
    Serial.println(code);
    Serial.println(reply);

    return UPLOAD_FAILED;
}


// ============================================================
// PUBLIC ENTRY POINT
// ============================================================

bool cloud_syncEmergencyQueue() {

    if (!sd_isReady() && !sd_remount()) {

        oled_show("SD card missing", "Cannot sync", "", "");
        delay(2000);

        return false;
    }

    int total = 0;
    String pending = pendingFiles(total);

    if (total == 0) {
        return true;
    }

    if (!firebaseConfigured()) {

        oled_show("Saved to SD card", "Firebase not set up", "See config.h", "");
        delay(3000);

        return false;
    }

    Serial.printf("[CLOUD] %d session(s) waiting to upload\n", total);

    if (!joinHotspot()) {

        oled_show(
            "Hotspot not found",
            "Files kept on SD",
            "Will retry after",
            "the next session"
        );
        delay(3000);

        restoreAccessPoint();

        return false;
    }

    oled_show("Do not power off", "Signing in...", "", "");

    WiFiClientSecure client;
    client.setInsecure();

    if (!signIn(client)) {

        oled_show(
            "Sign-in failed",
            "Check ESP account",
            "Files kept on SD",
            ""
        );
        delay(3000);

        restoreAccessPoint();

        return false;
    }

    int done = 0;
    int index = 0;
    int start = 0;

    while (start < (int)pending.length()) {

        int nl = pending.indexOf('\n', start);

        if (nl < 0) break;

        String fileName = pending.substring(start, nl);
        start = nl + 1;
        index++;

        oled_show(
            "Do not power off",
            "Uploading " + String(index) + " of " + String(total),
            fileName.substring(0, 21),
            ""
        );

        UploadResult result = uploadSession(client, fileName);

        if (result != UPLOAD_FAILED) {

            String base = fileName.substring(0, fileName.length() - strlen(SESSION_FILE_EXT));
            String marker = String(EMERGENCY_DIR) + "/" + base + SYNCED_MARKER_EXT;

            sd_writeFile(marker.c_str(), "1");

            done++;
        }
    }

    oled_show(
        "Sync complete",
        String(done) + " of " + String(total) + " uploaded",
        done == total ? "Open app to assign" : "Rest kept on SD",
        ""
    );
    delay(2500);

    restoreAccessPoint();

    return done == total;
}
