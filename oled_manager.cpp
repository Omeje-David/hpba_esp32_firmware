#include "oled_manager.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


static Adafruit_SSD1306 display(
    OLED_WIDTH,
    OLED_HEIGHT,
    &Wire,
    -1
);

static bool oledReady = false;

bool oled_init() {

    Serial.println();
    Serial.println("========================================");
    Serial.println(" HPBA ESP32 - OLED INITIALIZATION");
    Serial.println("========================================");

    Wire.begin(
        OLED_SDA_PIN,
        OLED_SCL_PIN
    );

    if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_I2C_ADDRESS
    )) {

        Serial.println("[OLED] Initialization FAILED");

        oledReady = false;

        return false;
    }

    oledReady = true;

    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);

    display.setCursor(0, 0);

    display.println("HPBA Attendance");

    display.setCursor(0, 16);

    display.println("Initializing...");

    display.display();

    Serial.println("[OLED] Ready");

    return true;
}

void oled_clear() {

    if (!oledReady) return;

    display.clearDisplay();
    display.display();
}

void oled_show(
    const String& line1,
    const String& line2,
    const String& line3,
    const String& line4
) {

    if (!oledReady) return;

    display.clearDisplay();

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.println(line1);

    display.setCursor(0, 16);
    display.println(line2);

    display.setCursor(0, 32);
    display.println(line3);

    display.setCursor(0, 48);
    display.println(line4);

    display.display();
}