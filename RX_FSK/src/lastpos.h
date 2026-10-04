#ifndef LASTPOS_H
#define LASTPOS_H

#include <Arduino.h>
#include "Sonde.h"

typedef struct {
    bool valid;
    char id[12];
    char typestr[10];
    float freq;
    float lat;
    float lon;
    float alt;
    float vs;
    float hs;
    float dir;
    uint8_t sats;
    uint32_t time;
    uint32_t frame;
    char launchsite[18];
    float pred_lat;
    float pred_lon;
    float pred_alt;
    float batt;
    uint32_t saved_time; // Timestamp / epoch when saved to flash
} LastPosData;

extern LastPosData lastPos;

// Initializes and loads last-known sonde position from LittleFS (/lastpos.txt)
bool loadLastPos();

// Updates and periodically persists the sonde position to LittleFS
// Rate-limited and thresholded to prevent flash wear
bool updateLastPos(SondeInfo *si, bool force = false);

// Formats lastPos as a JSON object into buf
int lastPosToJson(char *buf, int maxlen);

#endif
