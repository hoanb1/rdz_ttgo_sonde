/*
 * conn-hoanuk.h
 * High-performance non-blocking telemetry connector for hoan.uk Platform
 * Supports RS41, RS41-NFW, Horus, DFM, M10/M20, MP3H radiosondes & trackers
 * Copyright (c) 2026 Antigravity / hoan.uk
 */

#ifndef conn_hoanuk_h
#define conn_hoanuk_h

#include "conn.h"

#if FEATURE_HOANUK

class ConnHoanUK : public Conn
{
public:
    virtual void init();
    virtual void netsetup();
    virtual void netshutdown();
    virtual void updateSonde(SondeInfo *si);
    virtual void updateStation(PosInfo *pi);
    virtual String getStatus();
    virtual String getName();

    void updateRawPacket(const uint8_t *raw, int len, float freq, int rssi);
    void hoanuk_client_fsm();

private:
    void sendBinaryPayload(const char *deviceId, const uint8_t *payload, int len);
    void sendPayload(const char *json_body);
    static float calculateDewPoint(float temp, float hum);
};

extern ConnHoanUK connHoanUK;

#endif // FEATURE_HOANUK
#endif // conn_hoanuk_h
