#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>

struct DeviceInfo {

    String deviceId;
    String studentId;

    int rssi;

    unsigned long lastSeen;

    bool active;
};

bool device_manager_init();

bool device_register(
    const String& deviceId,
    const String& studentId
);

bool device_isRegistered(
    const String& deviceId
);

String device_getStudent(
    const String& deviceId
);

void device_updateRSSI(
    const String& deviceId,
    int rssi
);

String device_getAllJSON();

#endif