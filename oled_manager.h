#ifndef OLED_MANAGER_H
#define OLED_MANAGER_H

#include <Arduino.h>

bool oled_init();

void oled_clear();

void oled_show(
    const String& line1,
    const String& line2 = "",
    const String& line3 = "",
    const String& line4 = ""
);

#endif