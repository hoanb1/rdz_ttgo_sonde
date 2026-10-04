#include "lastpos.h"
#include <LittleFS.h>
#define TAG "LastPos"
#include "logger.h"

extern Sonde sonde;

LastPosData lastPos = {0};

bool loadLastPos() {
    lastPos.valid = false;
    if (!LittleFS.exists("/lastpos.txt")) {
        LOG_I(TAG, "No /lastpos.txt found in LittleFS");
        return false;
    }
    File f = LittleFS.open("/lastpos.txt", "r");
    if (!f) {
        LOG_E(TAG, "Failed to open /lastpos.txt for reading");
        return false;
    }
    memset(&lastPos, 0, sizeof(LastPosData));
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        val.trim();

        if (key.equalsIgnoreCase("id")) {
            strncpy(lastPos.id, val.c_str(), sizeof(lastPos.id) - 1);
        } else if (key.equalsIgnoreCase("type")) {
            strncpy(lastPos.typestr, val.c_str(), sizeof(lastPos.typestr) - 1);
        } else if (key.equalsIgnoreCase("freq")) {
            lastPos.freq = val.toFloat();
        } else if (key.equalsIgnoreCase("lat")) {
            lastPos.lat = val.toFloat();
        } else if (key.equalsIgnoreCase("lon")) {
            lastPos.lon = val.toFloat();
        } else if (key.equalsIgnoreCase("alt")) {
            lastPos.alt = val.toFloat();
        } else if (key.equalsIgnoreCase("vs")) {
            lastPos.vs = val.toFloat();
        } else if (key.equalsIgnoreCase("hs")) {
            lastPos.hs = val.toFloat();
        } else if (key.equalsIgnoreCase("dir")) {
            lastPos.dir = val.toFloat();
        } else if (key.equalsIgnoreCase("sats")) {
            lastPos.sats = (uint8_t)val.toInt();
        } else if (key.equalsIgnoreCase("time")) {
            lastPos.time = (uint32_t)val.toInt();
        } else if (key.equalsIgnoreCase("frame")) {
            lastPos.frame = (uint32_t)val.toInt();
        } else if (key.equalsIgnoreCase("site")) {
            strncpy(lastPos.launchsite, val.c_str(), sizeof(lastPos.launchsite) - 1);
        } else if (key.equalsIgnoreCase("pred_lat")) {
            lastPos.pred_lat = val.toFloat();
        } else if (key.equalsIgnoreCase("pred_lon")) {
            lastPos.pred_lon = val.toFloat();
        } else if (key.equalsIgnoreCase("pred_alt")) {
            lastPos.pred_alt = val.toFloat();
        } else if (key.equalsIgnoreCase("batt")) {
            lastPos.batt = val.toFloat();
        } else if (key.equalsIgnoreCase("saved")) {
            lastPos.saved_time = (uint32_t)val.toInt();
        }
    }
    f.close();

    if (strlen(lastPos.id) > 0 && (lastPos.lat != 0.0f || lastPos.lon != 0.0f)) {
        lastPos.valid = true;
        LOG_I(TAG, "Successfully loaded last sonde position: ID %s at %.6f, %.6f, alt %.1fm\n",
              lastPos.id, lastPos.lat, lastPos.lon, lastPos.alt);
        return true;
    }
    return false;
}

bool updateLastPos(SondeInfo *si, bool force) {
    if (!si) return false;
    if (sonde.config.lastpos.active == 0) return false;
    if (!si->d.validID || !VALIDPOS(si->d.validPos)) return false;
    if ((si->d.validPos & 0x80) && !force) return false;

    // Always update in-memory record
    lastPos.valid = true;
    strncpy(lastPos.id, si->d.id, sizeof(lastPos.id) - 1);
    lastPos.id[sizeof(lastPos.id) - 1] = 0;

    const char *tstr = si->d.typestr[0] ? si->d.typestr : sondeTypeStr[sonde.realType(si)];
    strncpy(lastPos.typestr, tstr ? tstr : "RS41", sizeof(lastPos.typestr) - 1);
    lastPos.typestr[sizeof(lastPos.typestr) - 1] = 0;

    lastPos.freq = si->freq;
    lastPos.lat = si->d.lat;
    lastPos.lon = si->d.lon;
    lastPos.alt = si->d.alt;
    lastPos.vs = si->d.vs;
    lastPos.hs = si->d.hs;
    lastPos.dir = si->d.dir;
    lastPos.sats = si->d.sats;
    lastPos.time = si->d.time;
    lastPos.frame = si->d.frame;
    strncpy(lastPos.launchsite, si->launchsite, sizeof(lastPos.launchsite) - 1);
    lastPos.launchsite[sizeof(lastPos.launchsite) - 1] = 0;
    lastPos.pred_lat = si->d.pred_lat;
    lastPos.pred_lon = si->d.pred_lon;
    lastPos.pred_alt = si->d.pred_alt;
    lastPos.batt = si->d.batteryVoltage;

    static uint32_t lastFlashSaveMillis = 0;
    static float lastSavedAlt = 99999.0f;
    static char lastSavedId[12] = {0};
    uint32_t now = millis();

    uint32_t cooldown_ms = (sonde.config.lastpos.interval > 0 ? (uint32_t)sonde.config.lastpos.interval : 10) * 1000;
    float alt_step = (sonde.config.lastpos.alt_step > 0 ? (float)sonde.config.lastpos.alt_step : 20.0f);
    float sticky_alt = (sonde.config.norx_sticky_alt > 0 ? (float)sonde.config.norx_sticky_alt : 3000.0f);

    bool shouldWrite = false;
    if (force) {
        shouldWrite = true;
    } else if (strcmp(lastSavedId, lastPos.id) != 0) {
        shouldWrite = true;
    } else if (now - lastFlashSaveMillis >= cooldown_ms) {
        if (lastPos.alt < sticky_alt && (lastSavedAlt - lastPos.alt >= alt_step)) {
            shouldWrite = true;
        } else if (lastPos.alt >= sticky_alt && (lastSavedAlt - lastPos.alt >= (alt_step * 5.0f))) {
            shouldWrite = true;
        } else if (lastPos.alt < 500.0f && (now - lastFlashSaveMillis >= 30000)) {
            shouldWrite = true;
        } else if (lastPos.alt < 300.0f && fabsf(lastPos.vs) < 0.5f && (now - lastFlashSaveMillis >= 15000)) {
            shouldWrite = true;
        }
    }

    if (shouldWrite) {
        File f = LittleFS.open("/lastpos.txt", "w");
        if (!f) {
            LOG_E(TAG, "Failed to open /lastpos.txt for writing");
            return false;
        }
        lastPos.saved_time = lastPos.time ? lastPos.time : (uint32_t)(millis() / 1000);
        f.printf("id=%s\n", lastPos.id);
        f.printf("type=%s\n", lastPos.typestr);
        f.printf("freq=%.3f\n", lastPos.freq);
        f.printf("lat=%.6f\n", lastPos.lat);
        f.printf("lon=%.6f\n", lastPos.lon);
        f.printf("alt=%.1f\n", lastPos.alt);
        f.printf("vs=%.1f\n", lastPos.vs);
        f.printf("hs=%.1f\n", lastPos.hs);
        f.printf("dir=%.1f\n", lastPos.dir);
        f.printf("sats=%u\n", lastPos.sats);
        f.printf("time=%u\n", lastPos.time);
        f.printf("frame=%u\n", lastPos.frame);
        f.printf("site=%s\n", lastPos.launchsite);
        f.printf("pred_lat=%.6f\n", lastPos.pred_lat);
        f.printf("pred_lon=%.6f\n", lastPos.pred_lon);
        f.printf("pred_alt=%.1f\n", lastPos.pred_alt);
        f.printf("batt=%.2f\n", lastPos.batt);
        f.printf("saved=%u\n", lastPos.saved_time);
        f.close();

        lastFlashSaveMillis = now;
        lastSavedAlt = lastPos.alt;
        strncpy(lastSavedId, lastPos.id, sizeof(lastSavedId) - 1);
        LOG_I(TAG, "Persisted last-known sonde position to LittleFS: %s at %.6f,%.6f alt=%.1fm\n",
              lastPos.id, lastPos.lat, lastPos.lon, lastPos.alt);
        return true;
    }
    return false;
}

int lastPosToJson(char *buf, int maxlen) {
    if (!lastPos.valid) {
        return snprintf(buf, maxlen, "{\"valid\": false}");
    }
    char mapsUrl[128];
    char osmUrl[128];
    snprintf(mapsUrl, sizeof(mapsUrl), "https://www.google.com/maps/search/?api=1&query=%.6f,%.6f", lastPos.lat, lastPos.lon);
    snprintf(osmUrl, sizeof(osmUrl), "https://www.openstreetmap.org/?mlat=%.6f&mlon=%.6f&zoom=16", lastPos.lat, lastPos.lon);

    return snprintf(buf, maxlen,
        "{\"valid\": true, \"id\": \"%s\", \"type\": \"%s\", \"freq\": %.3f, "
        "\"lat\": %.6f, \"lon\": %.6f, \"alt\": %.1f, \"vs\": %.1f, \"hs\": %.1f, \"dir\": %.1f, "
        "\"sats\": %u, \"time\": %u, \"frame\": %u, \"site\": \"%s\", "
        "\"pred_lat\": %.6f, \"pred_lon\": %.6f, \"pred_alt\": %.1f, \"batt\": %.2f, \"saved\": %u, "
        "\"maps_url\": \"%s\", \"osm_url\": \"%s\"}",
        lastPos.id, lastPos.typestr, lastPos.freq,
        lastPos.lat, lastPos.lon, lastPos.alt, lastPos.vs, lastPos.hs, lastPos.dir,
        lastPos.sats, lastPos.time, lastPos.frame, lastPos.launchsite,
        lastPos.pred_lat, lastPos.pred_lon, lastPos.pred_alt, lastPos.batt, lastPos.saved_time,
        mapsUrl, osmUrl);
}
