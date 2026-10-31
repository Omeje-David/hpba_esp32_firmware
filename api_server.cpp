 
#include "api_server.h"
#include "config.h"

#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "wifi_module.h"
#include "sd_manager.h"
#include "student_manager.h"
#include "device_manager.h"
#include "session_manager.h"
#include "attendance_manager.h"
#include "fingerprint_manager.h"
#include "oled_manager.h"
#include "lecturer_manager.h"
#include "device_guard.h"

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

static void handleEnrollStart();
static void handleEnrollStatus();
static void handlePortalSignin();
static void handleLecturerEnrollStart();
static void startLecturerEnrollment(const String& lecturerUid);
static void finalizeEnrollmentIfNeeded();
static void handleLecturersList();
static void handleLecturerBind();
static void handleFingerprintLookup();
static void handleFingerprintRelease();
static int findAvailableFingerprintId();

static void handleGetAttendance();
static void handleMarkAttendance();
static void handleSessionHistory();
static void handleAllAttendance();

static void redirectToPortal();
static void handleGenerate204();
static void handleAppleCaptive();
static void handleNcsi();
static void handleNotFound();

static void sendJSON(
    int statusCode,
    const String& json
);

static void addCORSHeaders();
static bool checkApiKey();
static bool isFromPortalNetwork();

// ============================================================
// SERVER
// ============================================================

static WebServer server(HTTP_PORT);

// ============================================================
// FINGERPRINT ENROLLMENT STATE
// ============================================================

static String enrollingStudentId = "";
static String enrollingRegistrationNumber = "";
static String enrollingLecturerUid = "";

static int enrollingFingerprintId = -1;

static bool enrollingLecturer = false;
static bool enrollFinalized = true;
static bool enrollBindingSuccess = false;

// ============================================================
// API KEY
// ============================================================

static bool checkApiKey() {

    if (
        !server.hasHeader("X-API-KEY") ||
        server.header("X-API-KEY") != String(API_KEY)
    ) {

        sendJSON(
            401,
            "{\"error\":\"Unauthorized\"}"
        );

        return false;
    }

    return true;
}

// ============================================================
// PORTAL NETWORK CHECK
// ============================================================

static bool isFromPortalNetwork() {

    IPAddress clientIp =
        server.client().remoteIP();

    IPAddress apIp =
        wifi_getAPIP();

    return
        clientIp[0] == apIp[0] &&
        clientIp[1] == apIp[1] &&
        clientIp[2] == apIp[2];
}

// ============================================================
// CORS
// ============================================================

static void addCORSHeaders() {

    server.sendHeader(
        "Access-Control-Allow-Origin",
        "*"
    );

    server.sendHeader(
        "Access-Control-Allow-Methods",
        "GET,POST,PUT,DELETE,OPTIONS"
    );

    server.sendHeader(
        "Access-Control-Allow-Headers",
        "Content-Type, X-API-KEY"
    );
}

// ============================================================
// JSON RESPONSE
// ============================================================

static void sendJSON(
    int statusCode,
    const String& json
) {

    addCORSHeaders();

    server.send(
        statusCode,
        "application/json",
        json
    );
}

// ============================================================
// STUDENT CAPTIVE PORTAL
// ============================================================

static void handleRoot() {

    String html = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<meta name="theme-color"
      content="#1769aa">

<title>HPBA Attendance</title>

<style>

* {
    box-sizing: border-box;
}

body {
    font-family: Arial, sans-serif;
    background: #eef3f8;
    margin: 0;
    padding: 18px;
    color: #17202a;
}

.container {
    max-width: 520px;
    margin: 20px auto;
    background: white;
    padding: 24px;
    border-radius: 16px;
    box-shadow: 0 5px 20px rgba(0,0,0,.08);
}

h1 {
    margin: 0 0 6px;
}

.subtitle {
    color: #667085;
    margin-bottom: 20px;
}

.info {
    padding: 14px;
    background: #f2f4f7;
    border-radius: 10px;
    margin: 12px 0;
    line-height: 1.5;
}

input {
    width: 100%;
    padding: 13px;
    font-size: 17px;
    border: 1px solid #cbd5e1;
    border-radius: 9px;
    margin-top: 8px;
    text-transform: uppercase;
}

button {
    width: 100%;
    padding: 14px;
    margin-top: 12px;
    border: none;
    border-radius: 9px;
    font-size: 16px;
    font-weight: bold;
    background: #1769aa;
    color: white;
}

button:disabled {
    background: #9ca3af;
}

.success {
    color: #087443;
    background: #ecfdf3;
    padding: 12px;
    border-radius: 9px;
    margin-top: 14px;
}

.error {
    color: #b42318;
    background: #fef3f2;
    padding: 12px;
    border-radius: 9px;
    margin-top: 14px;
}

.notice {
    color: #175cd3;
    background: #eff8ff;
    padding: 12px;
    border-radius: 9px;
    margin-top: 10px;
    line-height: 1.5;
}

.warn {
    color: #93370d;
    background: #fffaeb;
    padding: 12px;
    border-radius: 9px;
    margin-top: 10px;
    line-height: 1.5;
}

.small {
    color: #667085;
    font-size: 13px;
    margin-top: 14px;
}

</style>

</head  >

<body>

<div class="container">

<h1>HPBA Attendance</h1>

<div class="subtitle">
    Hybrid Proximity & Biometric Attendance
</div>

<div id="session" class="info">
    Checking attendance session...
</div>

<div id="formArea">

<label for="matric">
    Registration / Matric Number
</label>

<input
    id="matric"
    type="text"
    placeholder="e.g. 2021/245220"
    autocomplete="off"
    autocapitalize="characters"
>

<button
    id="signInBtn"
    onclick="signIn()"
    disabled
>
    Mark Attendance
</button>

</div>

<div id="result"></div>

<div class="small">
    Keep this page open until your attendance confirmation appears.
</div>

</div>

<script>

let deviceId =
    localStorage.getItem("hpba_device_id");

if (!deviceId) {

    deviceId =
        "HPBA-" +
        Date.now().toString(36) +
        "-" +
        Math.random()
            .toString(36)
            .substring(2, 10);

    try {

        localStorage.setItem(
            "hpba_device_id",
            deviceId
        );

    } catch (e) {
        // Captive portal browsers may restrict storage.
    }
}

function showResult(message, success) {

    const result =
        document.getElementById("result");

    result.className =
        success
            ? "success"
            : "error";

    result.innerText =
        message;
}

let countdownTimer = null;

function addBox(parent, className, text) {

    const box =
        document.createElement("div");

    box.className = className;

    box.innerText = text;

    parent.appendChild(box);

    return box;
}

function showSuccess(data) {

    const result =
        document.getElementById("result");

    result.className = "";
    result.innerHTML = "";

    if (countdownTimer) {

        clearInterval(countdownTimer);
        countdownTimer = null;
    }

    addBox(
        result,
        "success",
        data.message ||
        "Attendance marked successfully."
    );

    // Ask the student to leave so other phones can connect and sign.
    if (data.leaveNetwork) {

        addBox(
            result,
            "notice",
            "Please leave this Wi-Fi network now so other " +
            "students can sign in."
        );

        if (data.kickSeconds > 0) {

            let left = data.kickSeconds;

            const countdown =
                addBox(result, "notice", "");

            const draw = function() {

                countdown.innerText =
                    left > 0
                        ? "This phone will be disconnected " +
                          "automatically in " + left +
                          " seconds."
                        : "Disconnecting this phone now. " +
                          "You can close this page.";
            };

            draw();

            countdownTimer =
                setInterval(
                    function() {

                        left--;

                        if (left <= 0) {

                            left = 0;

                            clearInterval(countdownTimer);

                            countdownTimer = null;
                        }

                        draw();
                    },
                    1000
                );
        }
    }

    // Random (private) Wi-Fi address: attendance still counted, but warn.
    if (data.randomMac) {

        addBox(
            result,
            "warn",
            "Your phone is using a random (private) Wi-Fi address " +
            "for this network. Please turn this off for this " +
            "network so you do not have problems with your " +
            "attendance later:\n\n" +
            "Android: Wi-Fi settings > tap this network > " +
            "Privacy (or MAC address type) > Use device MAC.\n\n" +
            "iPhone: Wi-Fi settings > tap the (i) next to this " +
            "network > turn off Private Wi-Fi Address."
        );
    }
}

async function loadSession() {

    const sessionBox =
        document.getElementById("session");

    const button =
        document.getElementById("signInBtn");

    try {

        const response =
            await fetch(
                "/api/session",
                {
                    cache: "no-store"
                }
            );

        if (!response.ok) {
            throw new Error(
                "Session request failed"
            );
        }

        const data =
            await response.json();

        if (!data.active) {

            sessionBox.innerHTML =
                "<strong>No active attendance session.</strong><br>" +
                "Please wait for your lecturer to start the class.";

            button.disabled = true;

            return;
        }

        if (data.fingerprintOnly) {

            sessionBox.innerHTML =
                "<strong>Fingerprint-only session</strong><br>" +
                (data.courseCode || "") +
                (data.courseTitle
                    ? " - " + data.courseTitle
                    : "") +
                "<br>Use the fingerprint sensor.";

            button.disabled = true;

            return;
        }

        sessionBox.innerHTML =
            "<strong>Active Session</strong><br>" +
            (data.courseCode || "Attendance") +
            (data.courseTitle
                ? " - " + data.courseTitle
                : "");

        button.disabled = false;

    } catch (error) {

        sessionBox.innerHTML =
            "<strong>Unable to contact HPBA.</strong><br>" +
            "Stay connected to the attendance Wi-Fi and try again.";

        button.disabled = true;
    }
}

async function signIn() {

    const matric =
        document.getElementById("matric")
            .value
            .trim();

    const button =
        document.getElementById("signInBtn");

    if (!matric) {

        showResult(
            "Enter your registration number first.",
            false
        );

        return;
    }

    button.disabled = true;

    showResult(
        "Submitting attendance...",
        true
    );

    try {

        const response =
            await fetch(
                "/api/portal/signin",
                {
                    method: "POST",

                    headers: {
                        "Content-Type":
                            "application/json"
                    },

                    body: JSON.stringify({
                        matricNumber: matric,
                        deviceId: deviceId
                    })
                }
            );

        const data =
            await response.json();

        if (data.success) {

            showSuccess(data);

        } else {

            showResult(
                data.message ||
                "Attendance could not be marked.",
                false
            );

            button.disabled = false;
        }

    } catch (error) {

        showResult(
            "Network error. Stay connected to HPBA and try again.",
            false
        );

        button.disabled = false;
    }
}

document
    .getElementById("matric")
    .addEventListener(
        "keydown",
        function(event) {

            if (event.key === "Enter") {
                signIn();
            }

        }
    );

loadSession();

setInterval(
    loadSession,
    5000
);

</script>

</body>
</html>
)rawliteral";

    server.send(
        200,
        "text/html",
        html
    );
}

// ============================================================
// STATUS
// ============================================================

static void handleStatus() {

    DynamicJsonDocument doc(4096);

    doc["status"] =
        "online";

    doc["device"] =
        "HPBA-ESP32";

    doc["wifi"]["connected"] =
        wifi_isConnected();

    doc["wifi"]["ssid"] =
        wifi_getSSID();

    doc["wifi"]["ip"] =
        wifi_getIP();

    doc["wifi"]["rssi"] =
        wifi_getRSSI();

    doc["ap"]["ssid"] =
        AP_SSID;

    doc["ap"]["ip"] =
        wifi_getAPIP();

    doc["sd"]["ready"] =
        sd_isReady();

    doc["session"]["active"] =
        session_isActive();

    doc["session"]["fingerprintOnly"] =
        session_isFingerprintOnly();

    String output;

    serializeJson(
        doc,
        output
    );

    sendJSON(
        200,
        output
    );
}

static void handleMarkAttendance() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {
        sendJSON(
            400,
            "{\"error\":\"Request body required\"}"
        );
        return;
    }

    DynamicJsonDocument doc(4096);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"error\":\"Invalid JSON body\"}"
        );

        return;
    }

    String studentId =
        doc["studentId"] | "";

    String sessionId =
        doc["sessionId"] | "";

    String method =
        doc["method"] | "device";

    int rssi =
        doc["rssi"] | 0;

    studentId.trim();
    sessionId.trim();
    method.trim();

    if (
        studentId.length() == 0 ||
        sessionId.length() == 0
    ) {

        sendJSON(
            400,
            "{\"error\":\"studentId and sessionId are required\"}"
        );

        return;
    }

    if (!session_isActive()) {

        sendJSON(
            409,
            "{\"error\":\"No active attendance session\"}"
        );

        return;
    }

    AttendanceSession current =
        session_get();

    if (current.sessionId != sessionId) {

        sendJSON(
            409,
            "{\"error\":\"Session ID does not match the active session\"}"
        );

        return;
    }

    AttendanceMethod attendanceMethod =
        ATTENDANCE_DEVICE;

    if (method.equalsIgnoreCase("fingerprint")) {

        attendanceMethod =
            ATTENDANCE_FINGERPRINT;

    } else if (method.equalsIgnoreCase("both")) {

        attendanceMethod =
            ATTENDANCE_BOTH;
    }

    if (
        attendance_alreadyMarked(
            studentId,
            sessionId
        )
    ) {

        DynamicJsonDocument response(2048);

        response["success"] = true;
        response["alreadyMarked"] = true;
        response["message"] =
            "Attendance already marked";
        response["studentId"] = studentId;
        response["sessionId"] = sessionId;

        String output;

        serializeJson(
            response,
            output
        );

        sendJSON(
            200,
            output
        );

        return;
    }

    bool marked =
        attendance_mark(
            studentId,
            sessionId,
            attendanceMethod,
            rssi
        );

    if (!marked) {

        sendJSON(
            500,
            "{\"error\":\"Failed to mark attendance\"}"
        );

        return;
    }

    DynamicJsonDocument response(2048);

    response["success"] = true;
    response["alreadyMarked"] = false;
    response["message"] =
        "Attendance marked successfully";
    response["studentId"] = studentId;
    response["sessionId"] = sessionId;
    response["method"] = method;

    String output;

    serializeJson(
        response,
        output
    );

    sendJSON(
        200,
        output
    );
}

// ============================================================
// SESSION
// ============================================================
static void redirectToPortal() {

    String url = "http://";

    if (WiFi.getMode() == WIFI_AP) {
        url += WiFi.softAPIP().toString();
    } else {
        url += WiFi.localIP().toString();
    }

    url += "/";

    server.sendHeader("Location", url, true);

    server.send(
        302,
        "text/plain",
        "Redirecting to HPBA student portal..."
    );
}

static void handleGetSession() {

    sendJSON(
        200,
        session_getJSON()
    );
}

static void handleStartSession() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"error\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(4096);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"error\":\"Invalid JSON\"}"
        );

        return;
    }

    String sessionId =
        doc["sessionId"] | "";

    String courseId =
        doc["courseId"] | "";

    String courseCode =
        doc["courseCode"] | "";

    String courseTitle =
        doc["courseTitle"] | "";

    String sessionType = 
        doc["sessionType"] | "class";

    unsigned long duration =
        doc["duration"] | 0;

    unsigned long startEpoch =
        doc["startEpoch"] | 0;

    bool fingerprintOnly =
        doc["fingerprintOnly"] | false;

    if (
        sessionId.length() == 0 ||
        courseId.length() == 0 ||
        courseCode.length() == 0
    ) {

        sendJSON(
            400,
            "{\"error\":\"Missing session information\"}"
        );

        return;
    }

    if (!session_start(
        sessionId,
        courseId,
        courseCode,
        courseTitle,
        duration,
        startEpoch,
        fingerprintOnly,
        sessionType
    )) {

        sendJSON(
            409,
            "{\"error\":\"Session already active\"}"
        );

        return;
    }

    if (fingerprintOnly) {

        oled_show(
            "Fingerprint Mode",
            courseCode,
            courseTitle,
            "Use sensor"
        );

    } else {

        oled_show(
            "Attendance Active",
            courseCode,
            courseTitle,
            "Students may sign in"
        );
    }

    sendJSON(
        200,
        session_getJSON()
    );
}

static void handleEndSession() {

    if (!checkApiKey()) return;

    if (!session_end()) {

        sendJSON(
            409,
            "{\"error\":\"No active session\"}"
        );

        return;
    }

    oled_show(
        "HPBA Attendance",
        "Session Ended",
        "",
        ""
    );

    sendJSON(
        200,
        "{\"success\":true}"
    );
}

// ============================================================
// STUDENTS
// ============================================================

static void handleGetStudents() {

    if (!checkApiKey()) return;

    sendJSON(
        200,
        student_getAllJSON()
    );
}

static void handleAddStudent() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"error\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(4096);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"error\":\"Invalid JSON\"}"
        );

        return;
    }

    String id =
        doc["id"] | "";

    String name =
        doc["name"] | "";

    String matricNumber =
        doc["matricNumber"] | "";

    if (
        id.length() == 0 ||
        name.length() == 0 ||
        matricNumber.length() == 0
    ) {

        sendJSON(
            400,
            "{\"error\":\"Missing student information\"}"
        );

        return;
    }

    if (
        !student_add(
            id,
            name,
            matricNumber
        )
    ) {

        sendJSON(
            409,
            "{\"error\":\"Student already exists or matric number is already registered\"}"
        );

        return;
    }

    sendJSON(
        201,
        "{\"success\":true}"
    );
}

// ============================================================
// DEVICES
// ============================================================

static void handleRegisterDevice() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"error\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(4096);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"error\":\"Invalid JSON\"}"
        );

        return;
    }

    String deviceId =
        doc["deviceId"] | "";

    String studentId =
        doc["studentId"] | "";

    if (
        deviceId.length() == 0 ||
        studentId.length() == 0
    ) {

        sendJSON(
            400,
            "{\"error\":\"deviceId and studentId required\"}"
        );

        return;
    }

    if (!student_exists(studentId)) {

        sendJSON(
            404,
            "{\"error\":\"Student not found\"}"
        );

        return;
    }

    if (
        !device_register(
            deviceId,
            studentId
        )
    ) {

        sendJSON(
            500,
            "{\"error\":\"Device registration failed\"}"
        );

        return;
    }

    sendJSON(
        200,
        "{\"success\":true}"
    );
}

static void handleGetDevices() {

    if (!checkApiKey()) return;

    sendJSON(
        200,
        device_getAllJSON()
    );
}

// ============================================================
// ATTENDANCE
// ============================================================

static void handleGetAttendance() {

    if (!checkApiKey()) {
        return;
    }

    if (!server.hasArg("sessionId")) {

        sendJSON(
            400,
            "{\"error\":\"sessionId required\"}"
        );

        return;
    }

    String requestedSessionId =
        server.arg("sessionId");

    requestedSessionId.trim();

    if (requestedSessionId.length() == 0) {

        sendJSON(
            400,
            "{\"error\":\"sessionId cannot be empty\"}"
        );

        return;
    }

    String attendanceJson =
        attendance_getSessionJSON(
            requestedSessionId
        );

    // ---------------------------------------------------------
    // Add diagnostic information to the response.
    // ---------------------------------------------------------

    DynamicJsonDocument sourceDoc(16384);

    DeserializationError error =
        deserializeJson(
            sourceDoc,
            attendanceJson
        );

    if (error) {

        Serial.println(
            "[ATTENDANCE API] Failed to parse attendance JSON"
        );

        sendJSON(
            500,
            "{\"error\":\"Invalid attendance data\"}"
        );

        return;
    }

    JsonArray records =
        sourceDoc["attendance"].as<JsonArray>();

    DynamicJsonDocument response(16384);

    response["sessionId"] =
        requestedSessionId;

    response["count"] =
        records.size();

    JsonArray output =
        response.createNestedArray(
            "attendance"
        );

    for (JsonObject record : records) {

        JsonObject item =
            output.createNestedObject();

        item["studentId"] =
            record["studentId"];

        item["sessionId"] =
            record["sessionId"];

        item["timestamp"] =
            record["timestamp"];

        item["rssi"] =
            record["rssi"];

        item["method"] =
            record["method"];

        item["synced"] =
            record["synced"];
    }

    String outputJson;

    serializeJson(
        response,
        outputJson
    );

    Serial.println();
    Serial.println(
        "[ATTENDANCE API] GET /api/attendance"
    );

    Serial.print(
        "[ATTENDANCE API] Requested session: "
    );

    Serial.println(
        requestedSessionId
    );

    Serial.print(
        "[ATTENDANCE API] Records returned: "
    );

    Serial.println(
        records.size()
    );

    Serial.print(
        "[ATTENDANCE API] Response: "
    );

    Serial.println(
        outputJson
    );

    sendJSON(
        200,
        outputJson
    );
}

// ============================================================
// STUDENT PORTAL SIGN-IN
// ============================================================

static void handlePortalSignin() {

    // ---------------------------------------------------------
    // Portal does NOT require API key.
    // ---------------------------------------------------------

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Request body missing\"}"
        );

        return;
    }

    DynamicJsonDocument doc(2048);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Invalid JSON\"}"
        );

        return;
    }

    String matricNumber =
        doc["matricNumber"] |
        doc["reg_number"] |
        "";

    String deviceId =
        doc["deviceId"] |
        "";

    matricNumber.trim();
    deviceId.trim();

    if (matricNumber.length() == 0) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Registration number is required\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Confirm active session
    // ---------------------------------------------------------

    if (!session_isActive()) {

        sendJSON(
            200,
            "{\"success\":false,\"message\":\"There is no active attendance session.\"}"
        );

        return;
    }

    // IMPORTANT:
    // The actual session getter is session_get(),
    // not session_getCurrent().

    AttendanceSession current =
        session_get();

    if (current.fingerprintOnly) {

        sendJSON(
            200,
            "{\"success\":false,\"message\":\"This session requires fingerprint attendance.\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Registration number is the attendance identity.
    // ---------------------------------------------------------

    String attendanceStudentId =
        matricNumber;

    // ---------------------------------------------------------
    // Device guard: one phone, one signature per session.
    //
    // The phone is identified by its Wi-Fi MAC address (read from the
    // access point, so the browser cannot change it) and by the browser
    // token. If either was already used in this session the sign-in is
    // refused, even if the phone disconnected and reconnected.
    // ---------------------------------------------------------

    ClientInfo clientInfo =
        device_guard_identify(
            server.client().remoteIP()
        );

    String signedStudent = "";

    if (
        device_guard_isDuplicate(
            clientInfo,
            deviceId,
            signedStudent
        )
    ) {

        Serial.print("[GUARD] Blocked second sign-in from device ");
        Serial.print(device_guard_macString(clientInfo));
        Serial.print(" (already signed for ");
        Serial.print(signedStudent);
        Serial.print("), attempted: ");
        Serial.println(matricNumber);

        oled_show(
            "Blocked",
            "Device already",
            "signed this session",
            matricNumber
        );

        sendJSON(
            200,
            "{\"success\":false,\"deviceBlocked\":true,\"message\":\"This device has already signed for this session.\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Prevent duplicate attendance
    // ---------------------------------------------------------

    if (
        attendance_alreadyMarked(
            attendanceStudentId,
            current.sessionId
        )
    ) {

        sendJSON(
            200,
            "{\"success\":true,\"alreadyMarked\":true,\"leaveNetwork\":true,\"message\":\"You are already signed in.\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Get RSSI
    // ---------------------------------------------------------

    // The signal strength the access point measures for THIS phone.
    int rssi =
        clientInfo.found
            ? clientInfo.rssi
            : 0;

    // ---------------------------------------------------------
    // Device registration is optional.
    // Attendance does NOT depend on it.
    // ---------------------------------------------------------

    if (deviceId.length() > 0) {

        String existingStudentId =
            student_getByMatric(
                matricNumber
            );

        if (existingStudentId.length() > 0) {

            device_register(
                deviceId,
                existingStudentId
            );

            device_updateRSSI(
                deviceId,
                rssi
            );
        }
    }

    // ---------------------------------------------------------
    // Mark attendance
    // ---------------------------------------------------------

    bool marked =
        attendance_mark(
            attendanceStudentId,
            current.sessionId,
            ATTENDANCE_DEVICE,
            rssi
        );

    if (!marked) {

        sendJSON(
            500,
            "{\"success\":false,\"message\":\"Attendance could not be recorded.\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Remember this phone so it cannot sign again in this session.
    // ---------------------------------------------------------

    device_guard_record(
        clientInfo,
        deviceId,
        attendanceStudentId
    );

    // How long until this phone is disconnected (0 = it will not be,
    // so the page only asks the student to leave).
    int kickSeconds =
        (device_guard_kickSupported() && clientInfo.found)
            ? (SIGNED_CLIENT_KICK_DELAY_MS / 1000)
            : 0;

    // A random MAC does not stop the sign-in, the student is just told.
    bool randomMac =
        device_guard_isRandomMac(clientInfo);

    if (randomMac) {

        Serial.print("[GUARD] ");
        Serial.print(device_guard_macString(clientInfo));
        Serial.println(" is a random (private) MAC address");
    }

    // ---------------------------------------------------------
    // OLED confirmation
    // ---------------------------------------------------------

    oled_show(
        "Attendance",
        "Signed In!",
        matricNumber,
        current.courseCode
    );

    // ---------------------------------------------------------
    // Response
    // ---------------------------------------------------------

    String response = "{";

    response +=
        "\"success\":true,";

    response +=
        "\"alreadyMarked\":false,";

    response +=
        "\"message\":\"You are signed in successfully.\",";

    response +=
        "\"leaveNetwork\":true,";

    response += "\"kickSeconds\":";
    response += String(kickSeconds);
    response += ",";

    response += "\"randomMac\":";
    response += randomMac ? "true" : "false";
    response += ",";

    response +=
        "\"studentId\":\"";

    response +=
        matricNumber;

    response +=
        "\",";

    response +=
        "\"sessionId\":\"";

    response +=
        current.sessionId;

    response +=
        "\"";

    response += "}";

    sendJSON(
        200,
        response
    );
}

static int findAvailableFingerprintId() {

    // Retrieve all current student records.
    String studentsJson =
        student_getAllJSON();

    DynamicJsonDocument studentsDoc(16384);

    DeserializationError error =
        deserializeJson(
            studentsDoc,
            studentsJson
        );

    if (error) {

        Serial.println(
            "[ENROLL] Could not read student mappings"
        );

        return -1;
    }

    JsonArray students =
        studentsDoc["students"].as<JsonArray>();

    // R307 fingerprint IDs range from 1 to 127.
    for (int id = 1; id <= 127; id++) {

        bool studentUsesId = false;

        String candidate =
            String(id);

        // Check student mappings.
        for (JsonObject student : students) {

            String existingId =
                student["fingerprintId"] | "";

            existingId.trim();

            if (existingId == candidate) {

                studentUsesId = true;

                break;
            }
        }

        if (studentUsesId) {
            continue;
        }

        // Check lecturer mappings.
        if (
            lecturer_fingerprintIdInUse(
                candidate
            )
        ) {
            continue;
        }

        // Skip a slot that still holds a template on the sensor even
        // though no student or lecturer is linked to it (for example an
        // enrollment whose mapping was never saved). Enrolling there
        // would silently overwrite that person's fingerprint.
        if (fingerprint_idExists((uint16_t)id)) {
            continue;
        }

        return id;
    }

    return -1;
}

//h ============================================================
// FINGERPRINT ENROLLMENT START
// ============================================================

static void handleEnrollStart() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(512);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"Invalid JSON\"}"
        );

        return;
    }

    String regNumber =
        doc["reg_number"] | "";

    regNumber.trim();

    // ---------------------------------------------------------
    // A lecturer / admin must never be stored as a student.
    //
    // If the app marks the request as a lecturer (any of these
    // fields), hand it to the lecturer flow so the fingerprint is
    // linked to the user's Firebase UID instead of a student record.
    // ---------------------------------------------------------

    String role =
        doc["role"] | "";

    role.trim();
    role.toLowerCase();

    String lecturerUidHint =
        doc["lecturerUid"] |
        doc["lecturer_uid"] |
        doc["adminId"] |
        "";

    lecturerUidHint.trim();

    bool lecturerFlag =
        (doc["isLecturer"] | false) ||
        (doc["isAdmin"] | false) ||
        role == "lecturer" ||
        role == "admin";

    if (lecturerUidHint.length() > 0 || lecturerFlag) {

        String uid = lecturerUidHint;

        if (uid.length() == 0) {

            uid = doc["uid"] | doc["userId"] | "";

            uid.trim();
        }

        if (uid.length() == 0) {
            uid = regNumber;
        }

        if (uid.length() == 0) {

            sendJSON(
                400,
                "{\"status\":\"FAILED\",\"message\":\"lecturerUid required\"}"
            );

            return;
        }

        Serial.println(
            "[ENROLL] Request marked as lecturer. Using lecturer enrollment."
        );

        startLecturerEnrollment(uid);

        return;
    }

    if (regNumber.length() == 0) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"reg_number required\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Prevent simultaneous enrollment
    // ---------------------------------------------------------

    if (fingerprint_isEnrolling()) {

        sendJSON(
            409,
            "{\"status\":\"FAILED\",\"message\":\"Another enrollment is already in progress\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Ensure local student record exists
    // ---------------------------------------------------------

    String studentId =
        student_ensureByMatric(
            regNumber
        );

    if (studentId.length() == 0) {

        sendJSON(
            500,
            "{\"status\":\"FAILED\",\"message\":\"Could not create local student record\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // Find free fingerprint slot
    // ---------------------------------------------------------

    int fpId =
        findAvailableFingerprintId();

    if (fpId == -1) {

        sendJSON(
            409,
            "{\"status\":\"FAILED\",\"message\":\"No free fingerprint slots left\"}"
        );

        return;
    }

    // ---------------------------------------------------------
    // IMPORTANT:
    //
    // Do NOT call student_get() here.
    //
    // student_ensureByMatric() already established the local
    // student identity. Calling student_get() again was causing
    // the "Student record could not be loaded" failure.
    // ---------------------------------------------------------

    enrollingStudentId =
    studentId;

    enrollingRegistrationNumber =
    regNumber;

    enrollingFingerprintId =
    fpId;

    enrollingLecturer = false;  
    enrollingLecturerUid = "";

    enrollFinalized =
    false;

    enrollBindingSuccess =
    false;

    // ---------------------------------------------------------
    // OLED
    // ---------------------------------------------------------                           

    oled_show(
        "Fingerprint",
        "Enrollment",
        "ID " + String(fpId),
        "Place finger"
    );

    // ---------------------------------------------------------
    // Start non-blocking enrollment
    // ---------------------------------------------------------

    fingerprint_startEnroll(
        (uint16_t)fpId
    );

    Serial.println();
    Serial.println(
        "[ENROLL] Enrollment started"
    );

    Serial.print(
        "[ENROLL] Registration: "
    );

    Serial.println(
        regNumber
    );

    Serial.print(
        "[ENROLL] Student ID: "
    );

    Serial.println(
        studentId
    );

    Serial.print(
        "[ENROLL] Fingerprint ID: "
    );

    Serial.println(
        fpId
    );

    // ---------------------------------------------------------
    // Response
    // ---------------------------------------------------------

    DynamicJsonDocument response(512);

    response["status"] =
        "WAITING";

    response["message"] =
        "Place finger on sensor";

    response["reg_number"] =
        regNumber;

    response["fingerprintId"] =
        fpId;

    String output;

    serializeJson(
        response,
        output
    );

    sendJSON(
        202,
        output
    );
    }

static void handleLecturerEnrollStart() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(1024);

    DeserializationError error =
        deserializeJson(
            doc,
            server.arg("plain")
        );

    if (error) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"Invalid JSON\"}"
        );

        return;
    }

    String lecturerUid =
        doc["lecturerUid"] | "";

    lecturerUid.trim();

    if (lecturerUid.length() == 0) {

        sendJSON(
            400,
            "{\"status\":\"FAILED\",\"message\":\"lecturerUid required\"}"
        );

        return;
    }

    startLecturerEnrollment(lecturerUid);
}

// ============================================================
// START LECTURER ENROLLMENT (shared by both endpoints)
// ============================================================

static void startLecturerEnrollment(const String& lecturerUid) {

    if (fingerprint_isEnrolling()) {

        sendJSON(
            409,
            "{\"status\":\"FAILED\",\"message\":\"Another enrollment is already in progress\"}"
        );

        return;
    }

    // Do not enrol a lecturer who already has a mapping.
    if (lecturer_exists(lecturerUid)) {

        sendJSON(
            409,
            "{\"status\":\"FAILED\",\"message\":\"This lecturer already has an enrolled fingerprint\"}"
        );

        return;
    }

    int fpId =
        findAvailableFingerprintId();

    if (fpId == -1) {

        sendJSON(
            409,
            "{\"status\":\"FAILED\",\"message\":\"No free fingerprint IDs available\"}"
        );

        return;
    }

    // Prepare lecturer enrollment state.
    enrollingStudentId = "";

    enrollingRegistrationNumber = "";

    enrollingLecturerUid =
        lecturerUid;

    enrollingFingerprintId =
        fpId;

    enrollingLecturer =
        true;

    enrollFinalized =
        false;

    enrollBindingSuccess =
        false;

    oled_show(
        "Fingerprint",
        "Lecturer Setup",
        "ID " + String(fpId),
        "Place finger"
    );

    fingerprint_startEnroll(
        (uint16_t)fpId
    );

    Serial.println(
        "[ENROLL] Lecturer enrollment started"
    );

    Serial.print(
        "[ENROLL] Lecturer UID: "
    );

    Serial.println(
        lecturerUid
    );

    Serial.print(
        "[ENROLL] Fingerprint ID: "
    );

    Serial.println(
        fpId
    );

    DynamicJsonDocument response(512);

    response["status"] =
        "WAITING";

    response["message"] =
        "Place lecturer finger on sensor";

    response["lecturerUid"] =
        lecturerUid;

    response["fingerprintId"] =
        fpId;

    String output;

    serializeJson(
        response,
        output
    );

    sendJSON(
        202,
        output
    );
}

// ============================================================
// FINALIZE A FINISHED ENROLLMENT
//
// The sensor stores the fingerprint by itself, but the link
// (student or lecturer) is only saved here. This runs from the main
// loop, so the link no longer depends on the app polling
// /api/enroll/status at exactly the right moment.
// ============================================================

static void finalizeEnrollmentIfNeeded() {

    if (enrollFinalized) {
        return;
    }

    FingerprintStatus status =
        fingerprint_getStatus();

    // ---------------------------------------------------------
    // FINALIZE SUCCESS ONCE
    // ---------------------------------------------------------

    if (
        status == FP_SUCCESS &&
        !enrollFinalized
    ) {

        bool bound = false;

        String fingerprintId =
            String(enrollingFingerprintId);

        if (enrollingLecturer) {

            Serial.println(
                "[ENROLL] Finalizing lecturer enrollment"
            );

            Serial.print(
                "[ENROLL] Lecturer UID: "
            );

            Serial.println(
                enrollingLecturerUid
            );

            Serial.print(
                "[ENROLL] Fingerprint ID: "
            );

            Serial.println(
                enrollingFingerprintId
            );

            bound =
                lecturer_bindFingerprint(
                    enrollingLecturerUid,
                    fingerprintId
                );

        } else {

            Serial.println(
                "[ENROLL] Finalizing student enrollment"
            );

            Serial.print(
                "[ENROLL] Student ID: "
            );

            Serial.println(
                enrollingStudentId
            );

            Serial.print(
                "[ENROLL] Registration: "
            );

            Serial.println(
                enrollingRegistrationNumber
            );

            bound =
                student_bindFingerprint(
                    enrollingStudentId,
                    fingerprintId
                );
        }

        enrollBindingSuccess =
            bound;

        enrollFinalized =
            true;

        if (bound) {

            Serial.println(
                "[ENROLL] Fingerprint binding SUCCESS"
            );

            if (enrollingLecturer) {

                oled_show(
                    "Lecturer",
                    "Enrolled!",
                    "Fingerprint ID",
                    fingerprintId
                );

            } else {

                oled_show(
                    "Student",
                    "Enrolled!",
                    enrollingRegistrationNumber,
                    "Slot " + fingerprintId
                );
            }

        } else {

            Serial.println(
                "[ENROLL] Fingerprint binding FAILED"
            );

            oled_show(
                "Fingerprint",
                "Sensor saved",
                "Mapping failed",
                ""
            );
        }
    }

    // ---------------------------------------------------------
    // FAILED / TIMEOUT
    // ---------------------------------------------------------

    if (
        status == FP_FAILED ||
        status == FP_TIMEOUT
    ) {

        enrollFinalized =
            true;

        enrollBindingSuccess =
            false;
    }
}

// ============================================================
// FINGERPRINT ENROLLMENT STATUS
// ============================================================

static void handleEnrollStatus() {

    if (!checkApiKey()) return;

    FingerprintStatus status =
        fingerprint_getStatus();

    String statusText =
        fingerprint_statusString(status);

    // ---------------------------------------------------------
    // OLED STATUS
    // ---------------------------------------------------------

    switch (status) {

        case FP_WAITING:

            oled_show(
                "Fingerprint",
                "Enrollment",
                "Place finger",
                "ID " + String(enrollingFingerprintId)
            );

            break;

        case FP_REMOVE_FINGER:

            oled_show(
                "Fingerprint",
                "Good!",
                "Remove finger",
                "Wait..."
            );

            break;

        case FP_PLACE_AGAIN:

            oled_show(
                "Fingerprint",
                "Place same",
                "finger again",
                ""
            );

            break;

        case FP_SUCCESS:

            if (!enrollFinalized) {

                oled_show(
                    "Fingerprint",
                    "Sensor saved",
                    enrollingLecturer
                        ? "Lecturer"
                        : enrollingRegistrationNumber,
                    "Binding..."
                );
            }

            break;

        case FP_FAILED:

            oled_show(
                "Fingerprint",
                "Enrollment",
                "FAILED",
                "Try again"
            );

            break;

        case FP_TIMEOUT:

            oled_show(
                "Fingerprint",
                "Enrollment",
                "TIMEOUT",
                "Try again"
            );

            break;

        default:
            break;
    }

    // Idempotent: the main loop normally does this already.
    finalizeEnrollmentIfNeeded();

    // ---------------------------------------------------------
    // RESPONSE
    // ---------------------------------------------------------

    DynamicJsonDocument response(1024);

    response["status"] =
        statusText;

    response["code"] =
        fingerprint_statusCode(status);

    response["fingerprintId"] =
        enrollingFingerprintId;

    response["bindingSuccess"] =
        enrollBindingSuccess;

    response["isLecturer"] =
        enrollingLecturer;

    if (enrollingLecturer) {

        response["lecturerUid"] =
            enrollingLecturerUid;

    } else {

        response["registrationNumber"] =
            enrollingRegistrationNumber;

        response["studentId"] =
            enrollingStudentId;
    }

    if (status == FP_WAITING) {

        response["message"] =
            "Place finger on sensor";

    } else if (status == FP_REMOVE_FINGER) {

        response["message"] =
            "Remove finger";

    } else if (status == FP_PLACE_AGAIN) {

        response["message"] =
            "Place the same finger again";

    } else if (status == FP_SUCCESS) {

        if (enrollBindingSuccess) {

            response["message"] =
                enrollingLecturer
                    ? "Lecturer fingerprint enrolled successfully"
                    : "Student fingerprint enrolled successfully";

        } else {

            response["message"] =
                "Fingerprint saved, but identity mapping failed";
        }

    } else if (status == FP_FAILED) {

        response["message"] =
            "Fingerprint enrollment failed";

    } else if (status == FP_TIMEOUT) {

        response["message"] =
            "Fingerprint enrollment timed out";

    } else {

        response["message"] =
            "Enrollment idle";
    }

    String json;

    serializeJson(
        response,
        json
    );

    Serial.println(
        "[ENROLL] Status response: " + json
    );

    sendJSON(
        200,
        json
    );
}

static void handleGenerate204() {

    redirectToPortal();
}

static void handleAppleCaptive() {

    redirectToPortal();
}

static void handleNcsi() {

    server.send(
        200,
        "text/plain",
        "HPBA captive portal"
    );
}

// ============================================================
// 404
// ============================================================

static void handleNotFound() {

    String uri =
        server.uri();

    // ---------------------------------------------------------
    // Never redirect API requests.
    // ---------------------------------------------------------

    if (uri.startsWith("/api/")) {

        String message =
            "{\"error\":\"API endpoint not found\",\"uri\":\"";

        message +=
            uri;

        message +=
            "\"}";

        sendJSON(
            404,
            message
        );

        return;
    }

    // ---------------------------------------------------------
    // Captive portal requests
    // ---------------------------------------------------------

    if (isFromPortalNetwork()) {

        redirectToPortal();

        return;
    }

    String message =
        "HPBA endpoint not found\n\n";

    message +=
        "URI: ";

    message +=
        uri;

    message +=
        "\nMethod: ";

    message +=
        server.method() == HTTP_GET
            ? "GET"
            : "POST";

    server.send(
        404,
        "text/plain",
        message
    );
}

// ============================================================
// SERVER INITIALIZATION
// ============================================================

bool api_server_init() {

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        " HPBA - API SERVER"
    );

    Serial.println(
        "========================================"
    );

    // ---------------------------------------------------------
    // Collect API key header
    // ---------------------------------------------------------

    const char* headerKeys[] = {
        "X-API-KEY"
    };

    server.collectHeaders(
        headerKeys,
        1
    );

    // ---------------------------------------------------------
    // Root / student portal
    // ---------------------------------------------------------

    server.on(
        "/",
        HTTP_GET,
        handleRoot
    );

    // ---------------------------------------------------------
    // Student portal sign-in
    // ---------------------------------------------------------

    server.on(
        "/api/portal/signin",
        HTTP_POST,
        handlePortalSignin
    );

    // ---------------------------------------------------------
    // Fingerprint enrollment
    // ---------------------------------------------------------

    server.on(
        "/api/enroll/start",
        HTTP_POST,
        handleEnrollStart
    );

    server.on(
        "/api/enroll/lecturer/start",
        HTTP_POST,
        handleLecturerEnrollStart
    );

    server.on(
        "/api/enroll/status",
        HTTP_GET,
        handleEnrollStatus
    );

    server.on(
        "/api/lecturers",
        HTTP_GET,
        handleLecturersList
    );

    server.on(
        "/api/lecturers/bind",
        HTTP_POST,
        handleLecturerBind
    );

    server.on(
        "/api/fingerprint/lookup",
        HTTP_GET,
        handleFingerprintLookup
    );

    server.on(
        "/api/fingerprint/release",
        HTTP_POST,
        handleFingerprintRelease
    );

    // ---------------------------------------------------------
    // Captive portal probes
    // ---------------------------------------------------------

    server.on(
        "/generate_204",
        HTTP_GET,
        handleGenerate204
    );

    server.on(
        "/gen_204",
        HTTP_GET,
        handleGenerate204
    );

    server.on(
        "/hotspot-detect.html",
        HTTP_GET,
        handleAppleCaptive
    );

    server.on(
        "/library/test/success.html",
        HTTP_GET,
        handleAppleCaptive
    );

    server.on(
        "/ncsi.txt",
        HTTP_GET,
        handleNcsi
    );

    server.on(
        "/connecttest.txt",
        HTTP_GET,
        handleNcsi
    );

    server.on(
        "/fwlink",
        HTTP_GET,
        handleAppleCaptive
    );

    // ---------------------------------------------------------
    // System
    // ---------------------------------------------------------

    server.on(
        "/api/status",
        HTTP_GET,
        handleStatus
    );

    // ---------------------------------------------------------
    // Sessions
    // ---------------------------------------------------------

    server.on(
        "/api/session",
        HTTP_GET,
        handleGetSession
    );

    server.on(
        "/api/session/start",
        HTTP_POST,
        handleStartSession
    );

    server.on(
        "/api/session/end",
        HTTP_POST,
        handleEndSession
    );

    server.on(
        "/api/sessions",
        HTTP_GET,
        handleSessionHistory
    );

    // ---------------------------------------------------------
    // Students
    // ---------------------------------------------------------

    server.on(
        "/api/students",
        HTTP_GET,
        handleGetStudents
    );

    server.on(
        "/api/students",
        HTTP_POST,
        handleAddStudent
    );

    // ---------------------------------------------------------
    // Devices
    // ---------------------------------------------------------

    server.on(
        "/api/devices",
        HTTP_GET,
        handleGetDevices
    );

    server.on(
        "/api/devices/register",
        HTTP_POST,
        handleRegisterDevice
    );

    // ---------------------------------------------------------
    // Attendance
    // ---------------------------------------------------------

    server.on(
        "/api/attendance",
        HTTP_GET,
        handleGetAttendance
    );

    server.on(
        "/api/attendance/all",
        HTTP_GET,
        handleAllAttendance
    );

    server.on(
        "/api/attendance/mark",
        HTTP_POST,
        handleMarkAttendance
    );

    // ---------------------------------------------------------
    // CORS preflight
    // ---------------------------------------------------------

    server.on(
        "/api/status",
        HTTP_OPTIONS,
        []() {

            addCORSHeaders();

            server.send(
                204
            );
        }
    );

    // ---------------------------------------------------------
    // Not found
    // ---------------------------------------------------------

    server.onNotFound(
        handleNotFound
    );

    // ---------------------------------------------------------
    // Start server
    // ---------------------------------------------------------

    server.begin();

    Serial.println(
        "[API] HTTP server started"
    );

    Serial.print(
        "[API] Student portal: http://"
    );

    Serial.print(
        wifi_getAPIP()
    );

    Serial.println(
        "/"
    );

    if (wifi_isConnected()) {

        Serial.print(
            "[API] Flutter/LAN API: http://"
        );

        Serial.print(
            wifi_getIP()
        );

        Serial.println(
            "/"
        );
    }

    return true;
}

// ============================================================
// SERVER LOOP
// ============================================================

void api_server_handle() {

    server.handleClient();

    // Save the fingerprint link as soon as the sensor finishes,
    // whether or not the app is polling.
    finalizeEnrollmentIfNeeded();
}

void api_server_restart() {

    // The Wi-Fi mode was switched (hotspot sync). Re-open the listener.
    server.close();
    server.begin();

    Serial.println("[API] HTTP server restarted");
}

static void handleSessionHistory() {
    if (!checkApiKey()) return;

    String courseId = "";

    if (server.hasArg("courseId")) {
        courseId = server.arg("courseId");
        courseId.trim();
    }

    sendJSON(
        200,
        session_getHistoryJSON(courseId)
    );
}

static void handleAllAttendance() {
    if (!checkApiKey()) return;

    String sessionId = "";

    if (server.hasArg("sessionId")) {
        sessionId = server.arg("sessionId");
        sessionId.trim();
    }

    sendJSON(
        200,
        attendance_getAllJSON(sessionId)
    );
}


// ============================================================
// LECTURER / FINGERPRINT DIAGNOSTICS AND REPAIR
// ============================================================

static void handleLecturersList() {

    if (!checkApiKey()) return;

    sendJSON(
        200,
        lecturer_getAllJSON()
    );
}

// GET /api/fingerprint/lookup?id=3
// Says what the ESP32 thinks fingerprint slot 3 belongs to.
static void handleFingerprintLookup() {

    if (!checkApiKey()) return;

    int id = server.hasArg("id") ? server.arg("id").toInt() : 0;

    if (id < 1 || id > 127) {

        sendJSON(
            400,
            "{\"error\":\"id must be 1 to 127\"}"
        );

        return;
    }

    String slot = String(id);

    String lecturerUid = lecturer_getByFingerprint(slot);

    String studentId = "";

    if (lecturerUid.length() == 0) {
        studentId = student_getByFingerprint(slot);
    }

    DynamicJsonDocument response(768);

    response["fingerprintId"] = id;
    response["onSensor"] = fingerprint_idExists((uint16_t)id);

    if (lecturerUid.length() > 0) {

        response["role"] = "lecturer";
        response["lecturerUid"] = lecturerUid;

    } else if (studentId.length() > 0) {

        response["role"] = "student";
        response["studentId"] = studentId;

    } else {

        response["role"] = "none";
    }

    String output;

    serializeJson(response, output);

    sendJSON(200, output);
}

// POST /api/fingerprint/release   {"fingerprintId": 3}
// Deletes the template from the sensor and removes every link to it.
// Use it to clean up a lecturer who was enrolled as a student.
static void handleFingerprintRelease() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(256);

    if (deserializeJson(doc, server.arg("plain"))) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Invalid JSON\"}"
        );

        return;
    }

    int id = doc["fingerprintId"] | 0;

    if (id < 1 || id > 127) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"fingerprintId must be 1 to 127\"}"
        );

        return;
    }

    if (session_isActive() || fingerprint_isEnrolling()) {

        sendJSON(
            409,
            "{\"success\":false,\"message\":\"Finish the active session or enrollment first\"}"
        );

        return;
    }

    String slot = String(id);

    bool lecturerRemoved = lecturer_unbindFingerprint(slot);
    int studentsCleared = student_unbindFingerprint(slot);
    bool deleted = fingerprint_deleteTemplate((uint16_t)id);

    DynamicJsonDocument response(384);

    response["success"] = deleted;
    response["fingerprintId"] = id;
    response["lecturerLinkRemoved"] = lecturerRemoved;
    response["studentLinksCleared"] = studentsCleared;
    response["deletedFromSensor"] = deleted;

    String output;

    serializeJson(response, output);

    sendJSON(200, output);
}


// POST /api/lecturers/bind   {"lecturerUid": "...", "fingerprintId": 5}
//
// Re-links a lecturer to a fingerprint that is ALREADY stored on the
// sensor, without enrolling again. The app calls this when the ESP32
// has lost the lecturer list (for example an unreliable SD card) but
// the fingerprint is still on the sensor.
static void handleLecturerBind() {

    if (!checkApiKey()) return;

    if (!server.hasArg("plain")) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Request body required\"}"
        );

        return;
    }

    DynamicJsonDocument doc(512);

    if (deserializeJson(doc, server.arg("plain"))) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"Invalid JSON\"}"
        );

        return;
    }

    String uid = doc["lecturerUid"] | "";

    uid.trim();

    int id = doc["fingerprintId"] | 0;

    if (uid.length() == 0 || id < 1 || id > 127) {

        sendJSON(
            400,
            "{\"success\":false,\"message\":\"lecturerUid and fingerprintId (1-127) required\"}"
        );

        return;
    }

    if (session_isActive() || fingerprint_isEnrolling()) {

        sendJSON(
            409,
            "{\"success\":false,\"message\":\"Finish the active session or enrollment first\"}"
        );

        return;
    }

    String slot = String(id);

    String owner = lecturer_getByFingerprint(slot);

    // Already linked to this lecturer: nothing to do.
    if (owner == uid) {

        sendJSON(
            200,
            "{\"success\":true,\"alreadyLinked\":true}"
        );

        return;
    }

    if (owner.length() > 0) {

        sendJSON(
            409,
            "{\"success\":false,\"message\":\"That fingerprint slot belongs to another lecturer\"}"
        );

        return;
    }

    String currentSlot = lecturer_getFingerprintId(uid);

    if (currentSlot.length() > 0) {

        sendJSON(
            409,
            "{\"success\":false,\"message\":\"This lecturer is already linked to slot " +
            currentSlot + "\"}"
        );

        return;
    }

    if (student_getByFingerprint(slot).length() > 0) {

        sendJSON(
            409,
            "{\"success\":false,\"message\":\"That fingerprint slot belongs to a student\"}"
        );

        return;
    }

    // The link must point at a real fingerprint.
    if (!fingerprint_idExists((uint16_t)id)) {

        sendJSON(
            404,
            "{\"success\":false,\"message\":\"No fingerprint is stored in that slot. Enroll again.\"}"
        );

        return;
    }

    if (!lecturer_bindFingerprint(uid, slot)) {

        sendJSON(
            500,
            "{\"success\":false,\"message\":\"Could not save the lecturer link\"}"
        );

        return;
    }

    sendJSON(
        200,
        "{\"success\":true,\"alreadyLinked\":false}"
    );
}
