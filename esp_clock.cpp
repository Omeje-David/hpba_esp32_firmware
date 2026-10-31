#include "esp_clock.h"
#include "config.h"
#include "sd_manager.h"

#include <esp_random.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static unsigned long baseEpoch  = 0;
static unsigned long baseMillis = 0;
static bool approximate = true;

static String bootIdValue = "";

static unsigned long lastPersistEpoch = 0;
static unsigned long lastPersistMillis = 0;

static const unsigned long MIN_VALID_EPOCH = 1700000000UL;


// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static long daysFromCivil(long y, unsigned m, unsigned d) {

    y -= (m <= 2) ? 1 : 0;

    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (m > 2) ? m - 3 : m + 9;
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + (long)doe - 719468;
}

static void persist(unsigned long epoch) {

    if (!sd_isReady()) return;

    sd_writeFile(CLOCK_FILE, String(epoch));

    lastPersistEpoch = epoch;
    lastPersistMillis = millis();
}

void clock_init() {

    char buf[9];
    snprintf(buf, sizeof(buf), "%08X", (unsigned int)esp_random());
    bootIdValue = String(buf);

    if (!sd_isReady() || !sd_fileExists(CLOCK_FILE)) {
        Serial.println("[CLOCK] No saved time. Waiting for the app or a sync.");
        return;
    }

    String text = sd_readFile(CLOCK_FILE);
    text.trim();

    unsigned long saved = strtoul(text.c_str(), nullptr, 10);

    if (saved < MIN_VALID_EPOCH) {
        return;
    }

    baseEpoch = saved;
    baseMillis = millis();
    approximate = true;

    lastPersistEpoch = saved;
    lastPersistMillis = millis();

    Serial.print("[CLOCK] Using last saved time (approximate): ");
    Serial.println(clock_isoUtc(saved));
}

void clock_tick() {

    if (!clock_isSet()) return;

    if (millis() - lastPersistMillis < 600000UL) return;

    persist(clock_now());
}

void clock_setEpoch(unsigned long epoch) {

    if (epoch < MIN_VALID_EPOCH) return;

    baseEpoch = epoch;
    baseMillis = millis();
    approximate = false;

    // Avoid wearing the card: save only when the value moved a lot.
    long diff = (long)epoch - (long)lastPersistEpoch;

    if (diff > 600 || diff < -600 || lastPersistEpoch == 0) {
        persist(epoch);
    }
}

bool clock_setFromHttpDate(const String& httpDate) {

    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char mon[4] = {0};

    if (sscanf(httpDate.c_str(), "%*[^,], %d %3s %d %d:%d:%d",
               &day, mon, &year, &hh, &mm, &ss) != 6) {
        return false;
    }

    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    unsigned m = 0;

    for (unsigned i = 0; i < 12; i++) {
        if (strcmp(mon, months[i]) == 0) {
            m = i + 1;
            break;
        }
    }

    if (m == 0) return false;

    long days = daysFromCivil(year, m, (unsigned)day);

    unsigned long epoch =
        (unsigned long)(days * 86400L + hh * 3600L + mm * 60L + ss);

    if (epoch < MIN_VALID_EPOCH) return false;

    clock_setEpoch(epoch);

    Serial.print("[CLOCK] Synced from server: ");
    Serial.println(clock_isoUtc(epoch));

    return true;
}

bool clock_isSet() {
    return baseEpoch != 0;
}

bool clock_isApprox() {
    return !clock_isSet() || approximate;
}

unsigned long clock_now() {

    if (!clock_isSet()) return 0;

    return baseEpoch + (millis() - baseMillis) / 1000UL;
}

unsigned long clock_epochAtMillis(unsigned long ms) {

    if (!clock_isSet()) return 0;

    long long diff = (long long)ms - (long long)baseMillis;

    long long epoch = (long long)baseEpoch + diff / 1000LL;

    if (epoch < (long long)MIN_VALID_EPOCH) return 0;

    return (unsigned long)epoch;
}

String clock_bootId() {
    return bootIdValue;
}

String clock_isoUtc(unsigned long epoch) {

    time_t t = (time_t)epoch;
    struct tm tmv;

    gmtime_r(&t, &tmv);

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);

    return String(buf);
}

String clock_localStamp(unsigned long epoch) {

    time_t t = (time_t)(epoch + LOCAL_UTC_OFFSET_SECONDS);
    struct tm tmv;

    gmtime_r(&t, &tmv);

    char buf[24];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M", &tmv);

    return String(buf);
}
