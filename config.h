#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// HPBA ESP32 CONFIGURATION
// ============================================================


// ============================================================
// LEGACY / OPTIONAL ROUTER CONFIGURATION
// ============================================================
//
// These values are kept because other HPBA files may still
// reference them.
//
// CURRENT ARCHITECTURE DOES NOT USE THE ROUTER.
//
// The ESP32 operates as its own Wi-Fi Access Point and phones
// communicate directly with the ESP32.
//
// Router/STA support can be restored later if needed.
//

#define WIFI_SSID       "HPBA_App"
#define WIFI_PASSWORD   "Hpba_app"

#define ESP32_IP        192,168,0,150
#define ESP32_GATEWAY   192,168,0,1
#define ESP32_SUBNET    255,255,255,0
#define ESP32_DNS       192,168,0,1


// ============================================================
// ESP32 STUDENT / LOCAL ACCESS POINT
// ============================================================
//
// Students and the lecturer's Flutter device connect directly
// to this network.
//
// ESP32 AP address:
//     192.168.4.1
//
// Flutter base URL:
//     http://192.168.4.1
//
// Captive portal:
//     http://192.168.4.1/
//

#define AP_SSID         "Attendance-SignIn"

#define AP_PASSWORD     "Hpba_app"

#define AP_IP           192,168,4,1
#define AP_GATEWAY      192,168,4,1
#define AP_SUBNET       255,255,255,0


// ============================================================
// HTTP / DNS
// ============================================================

#define HTTP_PORT       80
#define DNS_PORT        53


// ============================================================
// API SECURITY
// ============================================================
//
// This must be identical to the value used by Flutter.
//
// ESP32:
//     API_KEY
//
// Flutter:
//     HpbaConfig.apiKey
//

#define API_KEY         "CHANGE_THIS_TO_A_SECRET"


// ============================================================
// R307 FINGERPRINT SENSOR
// ============================================================

#define FINGERPRINT_RX_PIN 16
#define FINGERPRINT_TX_PIN 17


// ============================================================
// OLED SSD1306
// ============================================================

#define OLED_SDA_PIN       21
#define OLED_SCL_PIN       22

#define OLED_WIDTH         128
#define OLED_HEIGHT        64

#define OLED_I2C_ADDRESS   0x3C


// ============================================================
// SD CARD
// ============================================================
//
// SD is currently optional.
//
// The attendance system should continue working when the
// SD card is unavailable.
//

#define SD_CS_PIN      5
#define SD_SCK_PIN     18
#define SD_MISO_PIN    19
#define SD_MOSI_PIN    23

#define STUDENTS_FILE      "/students.json"
#define ATTENDANCE_FILE    "/attendance.json"


// ============================================================
// SD STORAGE
// ============================================================

#define SD_SPI_FREQUENCY      4000000      // 4 MHz. Dropped automatically if mounting fails.

#define LECTURERS_FILE        "/lecturers.json"
#define CLOCK_FILE            "/CLOCK.TXT"

// Completed app-started sessions:  /COURSES/<CODE>/<CODE>_<YYYYMMDD>_<HHMM>.JSN
// Completed emergency sessions:    /EMERG/EMERGENCY_<YYYYMMDD>_<HHMM>.JSN
#define COURSES_DIR           "/COURSES"
#define EMERGENCY_DIR         "/EMERG"
#define SESSION_FILE_EXT      ".JSN"
#define SYNCED_MARKER_EXT     ".OK"

// Nigeria (WAT) is UTC+1. Used only to build file and session names.
#define LOCAL_UTC_OFFSET_SECONDS   3600


// ============================================================
// PHONE HOTSPOT (internet for Firebase sync)
// ============================================================
//
// Turn the hotspot ON before ending an emergency session.
// The ESP32 only supports the 2.4 GHz band.

#define HOTSPOT_SSID               "Infinix SMART 8"
#define HOTSPOT_PASSWORD           "DavidTj2200"
#define HOTSPOT_CONNECT_TIMEOUT_MS 25000
#define HOTSPOT_ATTEMPTS           2


// ============================================================
// FIREBASE (the same project the Flutter app uses)
// ============================================================
//
// 1. Firebase console > Project settings > General:
//      Project ID        -> FIREBASE_PROJECT_ID
//      Web API Key       -> FIREBASE_API_KEY
// 2. Firebase console > Authentication > Users > Add user:
//      create the account below (Email/Password sign-in must be enabled).
//
// Until these are filled in, sessions are still saved to the SD
// card and the sync step is skipped with a message on the OLED.

#define FIREBASE_PROJECT_ID    "hybrid-attendance-system-2ed26"
#define FIREBASE_API_KEY       "AIzaSyAAa2hmydQ-DF2-dngP75ElGfRn6-1-6a0"
#define FIREBASE_ESP_EMAIL     "esp32@hpba-device.local"
#define FIREBASE_ESP_PASSWORD  "Hpba_app"



// ============================================================
// DEVICE GUARD
// ============================================================

// How many distinct devices can be remembered per session.
#define DEVICE_GUARD_MAX_ENTRIES   300

// Remove phones from the attendance Wi-Fi after they have signed, so
// their connection slots are free for other students.
//
// 1 = on, 0 = off. If the sketch fails to compile with an error that
// mentions esp_wifi_ap_get_sta_aid, your ESP32 board package is too old:
// set this to 0 (the portal then only asks students to leave).
#define SIGNED_CLIENT_KICK_ENABLED   1

// How long after signing a phone is disconnected.
#define SIGNED_CLIENT_KICK_DELAY_MS  10000

// A phone that reconnects by itself is disconnected again, but not more
// often than this.
#define SIGNED_CLIENT_KICK_RETRY_MS  6000


#endif
