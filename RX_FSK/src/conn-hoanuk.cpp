/*
 * conn-hoanuk.cpp
 * High-performance non-blocking telemetry connector for hoan.uk Platform
 * Copyright (c) 2026 Antigravity / hoan.uk
 */

#include "../features.h"

#define TAG "conn-hoanuk"
#include "logger.h"

#if FEATURE_HOANUK

#include "conn-hoanuk.h"
#include "posinfo.h"
#include "RS41.h"
#include "../core.h"
#include <WiFi.h>
#include <sys/socket.h>
#include <lwip/dns.h>
#include <fcntl.h>
#include <math.h>

extern const char *sondeTypeStrSH[];
extern const char *version_id;

#define HOANUK_ERROR_RETRY_DELAY 10
#define HOANUK_BUFFER_SIZE 2048

static int hoanuk_sock = -1;
static ip_addr_t hoanuk_ipaddr;

enum HoanUKState {
    HUK_DISCONNECTED,
    HUK_DNSLOOKUP,
    HUK_DNSRESOLVED,
    HUK_CONNECTING,
    HUK_CONN_IDLE,
    HUK_CONN_SENDING,
    HUK_ERROR_RETRY
};

static HoanUKState huk_state = HUK_DISCONNECTED;
static unsigned long huk_last_state_change = 0;
static char huk_status_msg[128] = "Disabled";
static unsigned long huk_last_send = 0;

// NOTE: DO NOT call LOG_I / LOG_W / WiFiUDP from inside this lwIP DNS callback!
// Doing so from the lwIP tcpip task causes stack overflow and deadlocks lwIP.
static void _huk_dns_found(const char *name, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr) {
        hoanuk_ipaddr = *ipaddr;
        huk_state = HUK_DNSRESOLVED;
    } else {
        memset(&hoanuk_ipaddr, 0, sizeof(hoanuk_ipaddr));
        huk_state = HUK_ERROR_RETRY;
        huk_last_state_change = millis() / 1000;
        snprintf(huk_status_msg, sizeof(huk_status_msg), "DNS failed for %s", name ? name : "host");
    }
}

float ConnHoanUK::calculateDewPoint(float temp, float hum) {
    if (isnan(temp) || isnan(hum) || hum <= 0.0f) {
        return NAN;
    }
    const float a = 17.27f;
    const float b = 237.7f;
    float alpha = ((a * temp) / (b + temp)) + logf(hum / 100.0f);
    return (b * alpha) / (a - alpha);
}

void ConnHoanUK::init() {
    snprintf(huk_status_msg, sizeof(huk_status_msg), "Initialized");
}

void ConnHoanUK::netsetup() {
    if (!sonde.config.hoanuk.active) {
        snprintf(huk_status_msg, sizeof(huk_status_msg), "Disabled");
        return;
    }
    huk_state = HUK_DISCONNECTED;
    huk_last_state_change = millis() / 1000;
    snprintf(huk_status_msg, sizeof(huk_status_msg), "Connecting to %s:%d",
             sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk",
             sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80);
    LOG_I(TAG, "Network ready, target hoan.uk at %s:%d\n",
          sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk",
          sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80);
    hoanuk_client_fsm();
}

void ConnHoanUK::netshutdown() {
    if (hoanuk_sock >= 0) {
        close(hoanuk_sock);
        hoanuk_sock = -1;
    }
    huk_state = HUK_DISCONNECTED;
    huk_last_state_change = millis() / 1000;
    snprintf(huk_status_msg, sizeof(huk_status_msg), "Disconnected");
}

void ConnHoanUK::hoanuk_client_fsm() {
    if (!sonde.config.hoanuk.active) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (hoanuk_sock >= 0) {
            close(hoanuk_sock);
            hoanuk_sock = -1;
        }
        huk_state = HUK_DISCONNECTED;
        return;
    }

    unsigned long now = millis() / 1000;

    switch (huk_state) {
        case HUK_ERROR_RETRY:
            if (huk_last_state_change == 0) {
                huk_last_state_change = now;
            } else if (now - huk_last_state_change > HOANUK_ERROR_RETRY_DELAY) {
                huk_state = HUK_DISCONNECTED;
                huk_last_state_change = now;
            }
            break;

        case HUK_DISCONNECTED: {
            const char *host = sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk";
            if (ipaddr_aton(host, &hoanuk_ipaddr)) {
                huk_state = HUK_DNSRESOLVED;
                huk_last_state_change = now;
            } else {
                huk_state = HUK_DNSLOOKUP;
                huk_last_state_change = now;
                err_t res = dns_gethostbyname_addrtype(host, &hoanuk_ipaddr, _huk_dns_found, NULL, LWIP_DNS_ADDRTYPE_IPV4);
                if (res == ERR_OK) {
                    huk_state = HUK_DNSRESOLVED;
                } else if (res != ERR_INPROGRESS) {
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = now;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "DNS failed for %s", host);
                    break;
                } else {
                    break;
                }
            }
            // Fall through if DNS resolved immediately
        }

        case HUK_DNSLOOKUP:
            if (huk_state == HUK_DNSLOOKUP) {
                if (now - huk_last_state_change > 10) {
                    // DNS timeout
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = now;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "DNS timeout");
                }
                break;
            }
            // Fall through if switched to HUK_DNSRESOLVED

        case HUK_DNSRESOLVED: {
            hoanuk_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (hoanuk_sock < 0) {
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = now;
                snprintf(huk_status_msg, sizeof(huk_status_msg), "Socket error %d", errno);
                break;
            }
            int flags = fcntl(hoanuk_sock, F_GETFL);
            fcntl(hoanuk_sock, F_SETFL, flags | O_NONBLOCK);

            int port = sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80;
            struct sockaddr_in sock_info;
            memset(&sock_info, 0, sizeof(sock_info));
            sock_info.sin_family = AF_INET;
            sock_info.sin_addr.s_addr = hoanuk_ipaddr.u_addr.ip4.addr;
            sock_info.sin_port = htons(port);

            err_t res = connect(hoanuk_sock, (struct sockaddr *)&sock_info, sizeof(sock_info));
            if (res) {
                if (errno == EINPROGRESS) {
                    huk_state = HUK_CONNECTING;
                    huk_last_state_change = now;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "Connecting to %s:%d",
                             sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
                } else {
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = now;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "Connect error %d", errno);
                }
            } else {
                huk_state = HUK_CONN_IDLE;
                huk_last_state_change = now;
                snprintf(huk_status_msg, sizeof(huk_status_msg), "Connected to %s:%d",
                         sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
            }
            break;
        }

        case HUK_CONNECTING: {
            if (now - huk_last_state_change > 8) {
                close(hoanuk_sock);
                hoanuk_sock = -1;
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = now;
                snprintf(huk_status_msg, sizeof(huk_status_msg), "Connect timeout");
                break;
            }
            fd_set fdset, fdeset;
            FD_ZERO(&fdset);
            FD_SET(hoanuk_sock, &fdset);
            FD_ZERO(&fdeset);
            FD_SET(hoanuk_sock, &fdeset);
            struct timeval selto = {0, 0};

            int res = select(hoanuk_sock + 1, NULL, &fdset, &fdeset, &selto);
            if (res < 0) {
                close(hoanuk_sock);
                hoanuk_sock = -1;
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = now;
                snprintf(huk_status_msg, sizeof(huk_status_msg), "Select error %d", errno);
            } else if (res > 0) {
                int sockerr = 0;
                socklen_t len = sizeof(sockerr);
                if (getsockopt(hoanuk_sock, SOL_SOCKET, SO_ERROR, &sockerr, &len) < 0 || sockerr != 0) {
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = now;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "Socket err %d", sockerr);
                } else {
                    huk_state = HUK_CONN_IDLE;
                    huk_last_state_change = now;
                    int port = sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "Connected to %s:%d",
                             sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
                    LOG_I(TAG, "Connected to %s:%d\n", sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
                }
            }
            break;
        }

        case HUK_CONN_IDLE:
        case HUK_CONN_SENDING:
            // Check if connection is still alive by receiving leftover data or detecting disconnect
            if (hoanuk_sock >= 0) {
                char rx_buf[128];
                int r = recv(hoanuk_sock, rx_buf, sizeof(rx_buf) - 1, MSG_DONTWAIT);
                if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // Closed by server or socket error
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_DISCONNECTED;
                    huk_last_state_change = now;
                }
            }
            break;
    }
}

void ConnHoanUK::sendBinaryPayload(const char *deviceId, const uint8_t *payload, int len) {
    if (hoanuk_sock < 0 || (huk_state != HUK_CONN_IDLE && huk_state != HUK_CONN_SENDING)) {
        hoanuk_client_fsm();
        if (hoanuk_sock < 0 || (huk_state != HUK_CONN_IDLE && huk_state != HUK_CONN_SENDING)) {
            return;
        }
    }

    const char *host = sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk";
    const char *path = sonde.config.hoanuk.path[0] ? sonde.config.hoanuk.path : "/api/v1/telemetry/ingest";

    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s?deviceId=%s", path, deviceId);

    static char http_req[384];
    int req_len = snprintf(http_req, sizeof(http_req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: rdzTTGOsonde/%s\r\n"
        "Content-Type: application/octet-stream\r\n"
        "X-Device-Id: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n",
        full_path, host, version_id, deviceId, len);

    if (sonde.config.hoanuk.token[0]) {
        req_len += snprintf(http_req + req_len, sizeof(http_req) - req_len,
            "Authorization: Bearer %s\r\n", sonde.config.hoanuk.token);
    }

    req_len += snprintf(http_req + req_len, sizeof(http_req) - req_len, "\r\n");

    if (req_len + len <= (int)sizeof(http_req)) {
        memcpy(http_req + req_len, payload, len);
        req_len += len;
        int sent = send(hoanuk_sock, http_req, req_len, 0);
        if (sent < 0) {
            LOG_W(TAG, "Failed to send binary telemetry to hoan.uk: errno %d\n", errno);
            close(hoanuk_sock);
            hoanuk_sock = -1;
            huk_state = HUK_ERROR_RETRY;
            huk_last_state_change = millis() / 1000;
        } else {
            LOG_I(TAG, "Ingested %d bytes binary telemetry (%d bytes body) for %s to hoan.uk\n", sent, len, deviceId);
            snprintf(huk_status_msg, sizeof(huk_status_msg), "Ingested BIN OK (%lu)", millis() / 1000);
        }
    }
}

void ConnHoanUK::sendPayload(const char *json_body) {
    if (hoanuk_sock < 0 || huk_state != HUK_CONN_IDLE) {
        hoanuk_client_fsm();
        if (hoanuk_sock < 0 || (huk_state != HUK_CONN_IDLE && huk_state != HUK_CONN_SENDING)) {
            return;
        }
    }

    const char *host = sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk";
    const char *path = sonde.config.hoanuk.path[0] ? sonde.config.hoanuk.path : "/api/v1/telemetry/ingest";
    int body_len = strlen(json_body);

    static char http_req[HOANUK_BUFFER_SIZE + 512];
    int req_len = snprintf(http_req, sizeof(http_req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: rdzTTGOsonde/%s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: keep-alive\r\n",
        path, host, version_id, body_len);

    if (sonde.config.hoanuk.token[0]) {
        req_len += snprintf(http_req + req_len, sizeof(http_req) - req_len,
            "Authorization: Bearer %s\r\n", sonde.config.hoanuk.token);
    }

    req_len += snprintf(http_req + req_len, sizeof(http_req) - req_len, "\r\n%s", json_body);

    int sent = send(hoanuk_sock, http_req, req_len, 0);
    if (sent < 0) {
        LOG_W(TAG, "Failed to send telemetry to hoan.uk: errno %d\n", errno);
        close(hoanuk_sock);
        hoanuk_sock = -1;
        huk_state = HUK_ERROR_RETRY;
        huk_last_state_change = millis() / 1000;
    } else {
        LOG_I(TAG, "Ingested %d bytes telemetry to hoan.uk\n", sent);
        snprintf(huk_status_msg, sizeof(huk_status_msg), "Ingested OK (%lu)", millis() / 1000);
    }
}

void ConnHoanUK::updateSonde(SondeInfo *si) {
    if (!sonde.config.hoanuk.active) return;

    // When idle (no sonde received), only run FSM once per second to avoid hogging CPU
    static unsigned long last_idle_fsm = 0;
    if (!si) {
        if (millis() - last_idle_fsm < 1000) return;
        last_idle_fsm = millis();
        hoanuk_client_fsm();
        return;
    }

    hoanuk_client_fsm();

    // Rate-limit to max 1 packet per 2 seconds
    if (millis() - huk_last_send < 2000) return;

    char callsign[32];
    uint8_t realtype = si->type;
    if (TYPE_IS_METEO(realtype)) {
        realtype = (si->d.subtype == 1) ? STYPE_M10 : STYPE_M20;
    }

    const char *proto_name = sondeTypeStrSH[realtype];
    char rs41_sub[16] = {0};
    if (si->type == STYPE_RS41) {
        if (RS41::getSubtype(rs41_sub, sizeof(rs41_sub), si) == 0 && rs41_sub[0]) {
            proto_name = rs41_sub;
        } else if (si->freq > 430.0f) {
            proto_name = "RS41-NFW";
        }
    }

    if (si->d.ser[0]) {
        snprintf(callsign, sizeof(callsign), "%s", si->d.ser);
    } else if (si->d.id[0]) {
        snprintf(callsign, sizeof(callsign), "%s", si->d.id);
    } else {
        snprintf(callsign, sizeof(callsign), "%s-%.3f", proto_name, si->freq);
    }

    bool has_valid_fix = (VALIDPOS(si->d.validPos) && (fabsf(si->d.lat) > 0.001f || fabsf(si->d.lon) > 0.001f));

    // Pack ultra-compact 11 to 24-byte binary telemetry (Version 1)
    uint8_t bin_buf[32];
    int bin_len = 0;

    bin_buf[bin_len++] = 0x01; // Version 1

    uint8_t flags = 0;
    if (has_valid_fix) {
        flags |= 0x01; // Bit 0: GPS lat, lon
        flags |= 0x02; // Bit 1: Alt, speed
        flags |= 0x04; // Bit 2: Heading, sats
    }
    flags |= 0x08; // Bit 3: Temperature & Pressure
    flags |= 0x10; // Bit 4: Humidity & PM2.5
    flags |= 0x20; // Bit 5: Battery voltage

    bin_buf[bin_len++] = flags;

    if (has_valid_fix) {
        // Bit 0: lat, lon (8 bytes, int32 little-endian, scale 1e6)
        int32_t lat_1e6 = (int32_t)(si->d.lat * 1000000.0f);
        int32_t lon_1e6 = (int32_t)(si->d.lon * 1000000.0f);
        memcpy(bin_buf + bin_len, &lat_1e6, 4); bin_len += 4;
        memcpy(bin_buf + bin_len, &lon_1e6, 4); bin_len += 4;

        // Bit 1: Alt & Speed (3 bytes: int16 m, uint8 km/h)
        int16_t alt_m = (si->d.alt > 32767.0f) ? (int16_t)((uint16_t)si->d.alt) : (int16_t)si->d.alt;
        uint8_t speed_kmh = (si->d.validPos & 0x10) ? (uint8_t)fminf(si->d.hs * 3.6f, 255.0f) : 0;
        memcpy(bin_buf + bin_len, &alt_m, 2); bin_len += 2;
        bin_buf[bin_len++] = speed_kmh;

        // Bit 2: Heading & Sats (2 bytes: heading/2, sats)
        uint8_t heading_div2 = (uint8_t)(fminf(fmaxf(si->d.dir, 0.0f), 360.0f) / 2.0f);
        uint8_t sats = (si->d.validPos & 0x40) ? (uint8_t)fminf(fmaxf((float)si->d.sats, 0.0f), 36.0f) : 0;
        bin_buf[bin_len++] = heading_div2;
        bin_buf[bin_len++] = sats;
    }

    // Bit 3: Temperature & Pressure (4 bytes: int16 temp*100, uint16 press*10)
    int16_t temp_100 = !isnan(si->d.temperature) ? (int16_t)(si->d.temperature * 100.0f) : 0;
    uint16_t press_10 = (!isnan(si->d.pressure) && si->d.pressure > 0.0f) ? (uint16_t)(si->d.pressure * 10.0f) : 0;
    memcpy(bin_buf + bin_len, &temp_100, 2); bin_len += 2;
    memcpy(bin_buf + bin_len, &press_10, 2); bin_len += 2;

    // Bit 4: Humidity & PM2.5 (3 bytes: uint8 hum, uint16 pm25=0)
    uint8_t hum = !isnan(si->d.relativeHumidity) ? (uint8_t)fminf(fmaxf(si->d.relativeHumidity, 0.0f), 100.0f) : 0;
    uint16_t pm25 = 0;
    bin_buf[bin_len++] = hum;
    memcpy(bin_buf + bin_len, &pm25, 2); bin_len += 2;

    // Bit 5: Battery voltage in mV (2 bytes: uint16 mV)
    uint16_t batt_mv = (si->d.batteryVoltage > 0.0f) ? (uint16_t)(si->d.batteryVoltage * 1000.0f) : 0;
    memcpy(bin_buf + bin_len, &batt_mv, 2); bin_len += 2;

    sendBinaryPayload(callsign, bin_buf, bin_len);
    huk_last_send = millis();
}

void ConnHoanUK::updateRawPacket(const uint8_t *raw, int len, float freq, int rssi) {
    if (!sonde.config.hoanuk.active) return;
    if (!raw || len <= 0) return;

    // Rate-limit raw forwarding to max 1 packet per second
    if (millis() - huk_last_send < 1000) return;

    hoanuk_client_fsm();

    // Check if subblock 'y' contains a readable serial
    char callsign[32] = "RS41-RAW";
    int p = 57;
    while (p < len - 12) {
        uint8_t typ = raw[p++];
        uint32_t blen = raw[p++] + 2UL;
        if (p + blen > (uint32_t)len) break;
        if (typ == 'y') {
            snprintf(callsign, 9, "%s", (const char *)(raw + p + 2));
            callsign[8] = 0;
            for (int k = 0; k < 8; k++) {
                if (callsign[k] == ' ' || callsign[k] == 0) { callsign[k] = 0; break; }
            }
            break;
        }
        p += blen;
    }
    // Validate serial callsign format: RS41 serial is 8 alphanumeric chars, first char is uppercase letter
    bool valid_callsign = (strlen(callsign) == 8 && callsign[0] >= 'A' && callsign[0] <= 'Z');
    if (valid_callsign) {
        for (int k = 1; k < 8; k++) {
            if (!isalnum((unsigned char)callsign[k])) { valid_callsign = false; break; }
        }
    }
    if (!valid_callsign) strcpy(callsign, "RS41-RAW");

    // Convert raw RSSI value to true negative dBm:
    // In rdz_ttgo_sonde, rssi is stored as positive uint8 representing 2 * (-dBm)
    // E.g. raw 214 -> -107.0 dBm. If already negative, keep it.
    float rssi_dbm = (rssi > 0) ? -((float)rssi / 2.0f) : (float)rssi;

    // Convert raw bytes to hex string (up to 320 bytes = 640 hex chars)
    static char hex_buf[700];
    int max_bytes = len > 320 ? 320 : len;
    for (int i = 0; i < max_bytes; i++) {
        sprintf(hex_buf + (i * 2), "%02x", raw[i]);
    }
    hex_buf[max_bytes * 2] = 0;

    // Current ISO timestamp
    struct tm tim;
    time_t t = time(NULL);
    gmtime_r(&t, &tim);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tim.tm_year + 1900, tim.tm_mon + 1, tim.tm_mday,
             tim.tm_hour, tim.tm_min, tim.tm_sec);

    static char json_buf[HOANUK_BUFFER_SIZE];
    snprintf(json_buf, sizeof(json_buf),
        "{\"deviceId\":\"%s\","
        "\"deviceType\":\"radiosonde\","
        "\"stationRole\":\"stationary\","
        "\"protocol\":\"rs41_raw_forward\","
        "\"timestamp\":\"%s\","
        "\"payload_hex\":\"%s\","
        "\"system\":{\"freq\":%.3f,\"rssi\":%.1f}"
        "}",
        callsign, time_str, hex_buf, freq, rssi_dbm);

    sendPayload(json_buf);
    huk_last_send = millis();
}

void ConnHoanUK::updateStation(PosInfo *pi) {
    // Station positioning update if needed
}

String ConnHoanUK::getStatus() {
    if (!sonde.config.hoanuk.active) return String("disabled");
    char info[128];
    snprintf(info, sizeof(info), "%s [%s:%d]", huk_status_msg,
             sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk",
             sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80);
    return String(info);
}

String ConnHoanUK::getName() {
    return String("api.hoan.uk");
}

ConnHoanUK connHoanUK;

#endif // FEATURE_HOANUK
