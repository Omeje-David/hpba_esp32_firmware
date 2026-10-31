#include "device_manager.h"
#include "student_manager.h"

#include <ArduinoJson.h>

static DynamicJsonDocument deviceDoc(16384);

static bool deviceReady = false;

bool device_manager_init() {

    deviceDoc.clear();

    deviceDoc["devices"] =
        JsonArray();

    deviceReady = true;

    Serial.println(
        "[DEVICE] Device manager ready"
    );

    return true;
}

bool device_register(
    const String& deviceId,
    const String& studentId
) {

    if (!deviceReady) return false;

    JsonArray devices =
        deviceDoc["devices"]
            .as<JsonArray>();

    for (JsonObject device : devices) {

        if (
            device["deviceId"]
                .as<String>()
            == deviceId
        ) {

            device["studentId"] =
                studentId;

            device["active"] =
                true;

            return student_bindDevice(
                studentId,
                deviceId
            );
        }
    }

    JsonObject device =
        devices.createNestedObject();

    device["deviceId"] =
        deviceId;

    device["studentId"] =
        studentId;

    device["rssi"] =
        0;

    device["lastSeen"] =
        millis();

    device["active"] =
        true;

    return student_bindDevice(
        studentId,
        deviceId
    );
}

bool device_isRegistered(
    const String& deviceId
) {

    return device_getStudent(
        deviceId
    ).length() > 0;
}

String device_getStudent(
    const String& deviceId
) {

    if (!deviceReady) return "";

    JsonArray devices =
        deviceDoc["devices"]
            .as<JsonArray>();

    for (JsonObject device : devices) {

        if (
            device["deviceId"]
                .as<String>()
            == deviceId
        ) {

            return device["studentId"]
                .as<String>();
        }
    }

    return "";
}

void device_updateRSSI(
    const String& deviceId,
    int rssi
) {

    if (!deviceReady) return;

    JsonArray devices =
        deviceDoc["devices"]
            .as<JsonArray>();

    for (JsonObject device : devices) {

        if (
            device["deviceId"]
                .as<String>()
            == deviceId
        ) {

            device["rssi"] =
                rssi;

            device["lastSeen"] =
                millis();

            device["active"] =
                true;

            return;
        }
    }
}

String device_getAllJSON() {

    if (!deviceReady) {
        return "{\"devices\":[]}";
    }

    String output;

    serializeJson(
        deviceDoc,
        output
    );

    return output;
}