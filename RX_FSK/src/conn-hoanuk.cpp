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
static time_t huk_last_state_change = 0;
static char huk_status_msg[128] = "Disabled";
static unsigned long huk_last_send = 0;

static void _huk_dns_found(const char *name, const ip_addr_t *ipaddr, void *arg) {
    if (ipaddr) {
        hoanuk_ipaddr = *ipaddr;
        huk_state = HUK_DNSRESOLVED;
        LOG_I(TAG, "DNS resolved for %s", name);
    } else {
        memset(&hoanuk_ipaddr, 0, sizeof(hoanuk_ipaddr));
        huk_state = HUK_ERROR_RETRY;
        huk_last_state_change = 0;
        snprintf(huk_status_msg, sizeof(huk_status_msg), "DNS failed for %s", name);
        LOG_W(TAG, "DNS resolution failed for %s", name);
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
    huk_last_state_change = 0;
    LOG_I(TAG, "Network ready, connecting to hoan.uk at %s:%d",
          sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk",
          sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80);
}

void ConnHoanUK::netshutdown() {
    if (hoanuk_sock >= 0) {
        close(hoanuk_sock);
        hoanuk_sock = -1;
    }
    huk_state = HUK_DISCONNECTED;
    snprintf(huk_status_msg, sizeof(huk_status_msg), "Disconnected");
}

void ConnHoanUK::hoanuk_client_fsm() {
    if (!sonde.config.hoanuk.active) return;

    time_t now;
    time(&now);

    switch (huk_state) {
        case HUK_ERROR_RETRY:
            if (huk_last_state_change == 0) {
                huk_last_state_change = now;
            } else if (now - huk_last_state_change > HOANUK_ERROR_RETRY_DELAY) {
                huk_state = HUK_DISCONNECTED;
            }
            break;

        case HUK_DISCONNECTED: {
            const char *host = sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk";
            huk_state = HUK_DNSLOOKUP;
            err_t res = dns_gethostbyname_addrtype(host, &hoanuk_ipaddr, _huk_dns_found, NULL, LWIP_DNS_ADDRTYPE_IPV4);
            if (res == ERR_OK) {
                huk_state = HUK_DNSRESOLVED;
            } else if (res != ERR_INPROGRESS) {
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = 0;
            }
            break;
        }

        case HUK_DNSLOOKUP:
            // Waiting for DNS callback
            break;

        case HUK_DNSRESOLVED: {
            hoanuk_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (hoanuk_sock < 0) {
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = 0;
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
                } else {
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = 0;
                }
            } else {
                huk_state = HUK_CONN_IDLE;
                snprintf(huk_status_msg, sizeof(huk_status_msg), "Connected to %s:%d",
                         sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
            }
            break;
        }

        case HUK_CONNECTING: {
            fd_set fdset, fdeset;
            FD_ZERO(&fdset);
            FD_SET(hoanuk_sock, &fdset);
            FD_ZERO(&fdeset);
            FD_SET(hoanuk_sock, &fdeset);
            struct timeval selto = {0};

            int res = select(hoanuk_sock + 1, NULL, &fdset, &fdeset, &selto);
            if (res < 0) {
                close(hoanuk_sock);
                hoanuk_sock = -1;
                huk_state = HUK_ERROR_RETRY;
                huk_last_state_change = 0;
            } else if (res > 0) {
                int sockerr = 0;
                socklen_t len = sizeof(sockerr);
                if (getsockopt(hoanuk_sock, SOL_SOCKET, SO_ERROR, &sockerr, &len) < 0 || sockerr != 0) {
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_ERROR_RETRY;
                    huk_last_state_change = 0;
                } else {
                    huk_state = HUK_CONN_IDLE;
                    int port = sonde.config.hoanuk.port > 0 ? sonde.config.hoanuk.port : 80;
                    snprintf(huk_status_msg, sizeof(huk_status_msg), "Connected to %s:%d",
                             sonde.config.hoanuk.host[0] ? sonde.config.hoanuk.host : "api.hoan.uk", port);
                }
            }
            break;
        }

        case HUK_CONN_IDLE:
        case HUK_CONN_SENDING:
            // Check if connection is still alive by receiving leftover data
            if (hoanuk_sock >= 0) {
                char rx_buf[256];
                int r = recv(hoanuk_sock, rx_buf, sizeof(rx_buf) - 1, MSG_DONTWAIT);
                if (r == 0) {
                    // Closed by server
                    close(hoanuk_sock);
                    hoanuk_sock = -1;
                    huk_state = HUK_DISCONNECTED;
                }
            }
            break;
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

    char http_req[HOANUK_BUFFER_SIZE + 512];
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
        LOG_W(TAG, "Failed to send telemetry to hoan.uk: errno %d", errno);
        close(hoanuk_sock);
        hoanuk_sock = -1;
        huk_state = HUK_ERROR_RETRY;
        huk_last_state_change = 0;
    } else {
        LOG_I(TAG, "Ingested %d bytes telemetry to hoan.uk", sent);
        snprintf(huk_status_msg, sizeof(huk_status_msg), "Ingested OK (%lu)", millis() / 1000);
    }
}

void ConnHoanUK::updateSonde(SondeInfo *si) {
    if (!sonde.config.hoanuk.active) return;
    if (!si) return;

    // Rate-limit to max 1 packet per 2 seconds
    if (millis() - huk_last_send < 2000) return;

    hoanuk_client_fsm();

    char callsign[32];
    uint8_t realtype = si->type;
    if (TYPE_IS_METEO(realtype)) {
        realtype = (si->d.subtype == 1) ? STYPE_M10 : STYPE_M20;
    }

    if (si->d.ser[0]) {
        snprintf(callsign, sizeof(callsign), "%s", si->d.ser);
    } else if (si->d.id[0]) {
        snprintf(callsign, sizeof(callsign), "%s", si->d.id);
    } else {
        snprintf(callsign, sizeof(callsign), "%s-%.3f", sondeTypeStrSH[realtype], si->freq);
    }

    bool has_valid_fix = (VALIDPOS(si->d.validPos) && (fabsf(si->d.lat) > 0.001f || fabsf(si->d.lon) > 0.001f));
    float speed_kmh = (si->d.validPos & 0x10) ? (si->d.hs * 3.6f) : 0.0f;
    float dew_point = calculateDewPoint(si->d.temperature, si->d.relativeHumidity);

    // Build ISO timestamp
    struct tm tim;
    time_t t = si->d.time ? (time_t)si->d.time : (time_t)time(NULL);
    gmtime_r(&t, &tim);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tim.tm_year + 1900, tim.tm_mon + 1, tim.tm_mday,
             tim.tm_hour, tim.tm_min, tim.tm_sec);

    char json_buf[HOANUK_BUFFER_SIZE];
    int len = snprintf(json_buf, sizeof(json_buf),
        "{\"deviceId\":\"%s\","
        "\"deviceType\":\"radiosonde\","
        "\"stationRole\":\"%s\","
        "\"protocol\":\"%s\","
        "\"timestamp\":\"%s\","
        "\"speed\":%.1f,"
        "\"sats\":%d,"
        "\"system_voltage\":%.2f",
        callsign,
        speed_kmh >= 2.5f ? "mobile" : "stationary",
        sondeTypeStrSH[realtype],
        time_str,
        speed_kmh,
        (si->d.validPos & 0x40) ? si->d.sats : 0,
        si->d.batteryVoltage > 0 ? si->d.batteryVoltage : 0.0f
    );

    if (has_valid_fix) {
        len += snprintf(json_buf + len, sizeof(json_buf) - len,
            ",\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,"
            "\"vs\":%.1f,\"hs\":%.1f,\"dir\":%.1f,"
            "\"location\":{\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.1f,\"speed\":%.1f,\"vs\":%.1f,\"dir\":%.1f,\"sats\":%d,\"gps_fix\":true}",
            si->d.lat, si->d.lon, si->d.alt,
            si->d.vs, si->d.hs, si->d.dir,
            si->d.lat, si->d.lon, si->d.alt, speed_kmh, si->d.vs, si->d.dir,
            (si->d.validPos & 0x40) ? si->d.sats : 0
        );
    } else {
        len += snprintf(json_buf + len, sizeof(json_buf) - len,
            ",\"location\":{\"speed\":%.1f,\"sats\":%d,\"gps_fix\":false}",
            speed_kmh, (si->d.validPos & 0x40) ? si->d.sats : 0
        );
    }

    // Environment block
    len += snprintf(json_buf + len, sizeof(json_buf) - len, ",\"environment\":{");
    bool has_env = false;
    if (!isnan(si->d.temperature)) {
        len += snprintf(json_buf + len, sizeof(json_buf) - len, "\"temperature\":%.1f", si->d.temperature);
        has_env = true;
    }
    if (!isnan(si->d.relativeHumidity)) {
        len += snprintf(json_buf + len, sizeof(json_buf) - len, "%s\"humidity\":%.1f", has_env ? "," : "", si->d.relativeHumidity);
        has_env = true;
    }
    if (!isnan(si->d.pressure) && si->d.pressure > 0) {
        len += snprintf(json_buf + len, sizeof(json_buf) - len, "%s\"pressure\":%.1f", has_env ? "," : "", si->d.pressure);
        has_env = true;
    }
    if (!isnan(dew_point)) {
        len += snprintf(json_buf + len, sizeof(json_buf) - len, "%s\"dewPoint\":%.1f", has_env ? "," : "", dew_point);
    }
    len += snprintf(json_buf + len, sizeof(json_buf) - len, "}");

    // System and raw stats
    len += snprintf(json_buf + len, sizeof(json_buf) - len,
        ",\"system\":{\"voltage\":%.2f,\"rssi\":%d,\"freq\":%.3f,\"frame\":%u},"
        "\"raw\":{\"callsign\":\"%s\",\"type\":\"%s\",\"freq\":%.3f,\"rssi\":%d}"
        "}",
        si->d.batteryVoltage > 0 ? si->d.batteryVoltage : 0.0f,
        si->rssi,
        si->freq,
        si->d.frame,
        callsign,
        sondeTypeStrSH[realtype],
        si->freq,
        si->rssi
    );

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
