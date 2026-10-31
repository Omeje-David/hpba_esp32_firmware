#include "sd_manager.h"
#include "config.h"

#include <SPI.h>
#include <SD.h>

static bool sdReady = false;
static unsigned long lastMountAttempt = 0;
static bool spiStarted = false;

// ------------------------------------------------------------
// Mount at a given SPI speed
// ------------------------------------------------------------

static bool mountCard(uint32_t frequency) {

    SD.end();

    if (!SD.begin(SD_CS_PIN, SPI, frequency)) {
        return false;
    }

    if (SD.cardType() == CARD_NONE) {
        SD.end();
        return false;
    }

    return true;
}

static const char* cardTypeName(uint8_t type) {

    switch (type) {
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        default:        return "UNKNOWN";
    }
}

// ------------------------------------------------------------
// Write / read / delete a small file to prove the card works
// ------------------------------------------------------------

static bool selfTest() {

    const char* path = "/HPBA.TST";

    File f = SD.open(path, FILE_WRITE);

    if (!f) {
        return false;
    }

    f.print("HPBA-OK");
    f.close();

    File r = SD.open(path, FILE_READ);

    if (!r) {
        return false;
    }

    String back = r.readString();
    r.close();

    SD.remove(path);

    return back == "HPBA-OK";
}

static bool tryMountAll() {

    const uint32_t speeds[] = {
        (uint32_t)SD_SPI_FREQUENCY,
        1000000UL,
        400000UL
    };

    for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {

        if (!mountCard(speeds[i])) {
            Serial.printf("[SD] Mount failed at %lu Hz\n", (unsigned long)speeds[i]);
            continue;
        }

        if (!selfTest()) {
            Serial.printf("[SD] Mounted at %lu Hz but the write test failed\n",
                          (unsigned long)speeds[i]);
            SD.end();
            continue;
        }

        Serial.printf("[SD] Ready at %lu Hz\n", (unsigned long)speeds[i]);
        return true;
    }

    return false;
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

bool sd_init() {

    Serial.println();
    Serial.println("========================================");
    Serial.println(" HPBA ESP32 - SD CARD INITIALIZATION");
    Serial.println("========================================");

    if (!spiStarted) {
        SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
        spiStarted = true;
    }

    lastMountAttempt = millis();

    sdReady = tryMountAll();

    if (!sdReady) {

        Serial.println("[SD] Card is NOT available. Attendance still works.");
        Serial.println("[SD] Check: wiring (CS=5, SCK=18, MISO=19, MOSI=23),");
        Serial.println("[SD]        power (the module needs 5 V on VCC),");
        Serial.println("[SD]        card formatted as FAT32.");

        return false;
    }

    Serial.printf("[SD] Type: %s\n", cardTypeName(SD.cardType()));
    Serial.printf("[SD] Size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
    Serial.printf("[SD] Used: %llu MB\n", SD.usedBytes() / (1024ULL * 1024ULL));

    return true;
}

bool sd_remount() {

    if (sdReady) {
        return true;
    }

    unsigned long now = millis();

    if (lastMountAttempt != 0 && now - lastMountAttempt < 30000UL) {
        return false;
    }

    lastMountAttempt = now;

    sdReady = tryMountAll();

    if (sdReady) {
        Serial.println("[SD] Card mounted after retry");
    }

    return sdReady;
}

bool sd_isReady() {
    return sdReady;
}

bool sd_fileExists(const char* path) {

    if (!sdReady) return false;

    return SD.exists(path);
}

bool sd_mkdir(const char* path) {

    if (!sdReady) return false;

    if (SD.exists(path)) return true;

    return SD.mkdir(path);
}

File sd_open(const char* path, const char* mode) {

    if (!sdReady) return File();

    return SD.open(path, mode);
}

String sd_readFile(const char* path) {

    if (!sdReady) return "";

    File file = SD.open(path, FILE_READ);

    if (!file) return "";

    String data = file.readString();

    file.close();

    return data;
}

bool sd_writeFile(const char* path, const String& data) {

    if (!sdReady) return false;

    File file = SD.open(path, FILE_WRITE);

    if (!file) return false;

    size_t written = file.print(data);

    file.close();

    return written == data.length();
}

bool sd_appendFile(const char* path, const String& data) {

    if (!sdReady) return false;

    File file = SD.open(path, FILE_APPEND);

    if (!file) return false;

    size_t written = file.print(data);

    file.close();

    return written == data.length();
}

bool sd_deleteFile(const char* path) {

    if (!sdReady) return false;

    if (!SD.exists(path)) return true;

    return SD.remove(path);
}

bool sd_rename(const char* from, const char* to) {

    if (!sdReady) return false;

    if (SD.exists(to)) {
        SD.remove(to);
    }

    return SD.rename(from, to);
}
