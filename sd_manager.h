#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <FS.h>

// ============================================================
// SD CARD (FAT32, SPI)
//
// The attendance system never depends on the card. Every function
// here fails quietly when the card is missing.
// ============================================================

// Mounts the card and runs a write/read self-test.
bool sd_init();

// Tries to mount again if the card was missing at boot or was
// removed. Rate-limited to one attempt every 30 seconds.
bool sd_remount();

bool sd_isReady();

bool sd_fileExists(const char* path);

// Creates the folder if it does not exist.
bool sd_mkdir(const char* path);

File sd_open(const char* path, const char* mode);

String sd_readFile(const char* path);

bool sd_writeFile(const char* path, const String& data);

bool sd_appendFile(const char* path, const String& data);

bool sd_deleteFile(const char* path);

// Replaces the destination if it already exists.
bool sd_rename(const char* from, const char* to);

#endif
