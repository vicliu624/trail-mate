/**
 * @file      GPS.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-07-07
 *
 */
#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "domain/gnss_satellite.h"

#ifndef GPS_LOG_ENABLE
#define GPS_LOG_ENABLE 0
#endif

#if GPS_LOG_ENABLE
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

class GPS : public TinyGPSPlus
{
  public:
    GPS();

    ~GPS();

    bool init(Stream* stream);
    bool factory();
    bool configureGnss(uint8_t sat_mask);
    bool configureNmeaOutput(uint8_t output_hz, uint8_t sentence_mask);
    bool setReceiverMode(uint8_t mode, uint8_t sat_mask);

    size_t getSatellites(gps::GnssSatInfo* out, size_t max) const;
    gps::GnssStatus getGnssStatus() const;

    uint32_t loop(bool debug = false)
    {
        static uint32_t loop_count = 0;
        static uint32_t last_log_ms = 0;
        uint32_t now = millis();

        uint32_t chars_processed = 0;
        while (_stream->available())
        {
            int c = _stream->read();
            chars_processed++;
            if (debug)
            {
                Serial.write(c);
            }
            else
            {
                encode(c);
            }
            handleNmeaChar(static_cast<char>(c));
        }
        if (debug)
        {
            while (Serial.available())
            {
                _stream->write(Serial.read());
            }
        }

        // Log periodically (every 100 loops or every 5 seconds)
        loop_count++;
        if (GPS_LOG_ENABLE && (loop_count % 100 == 0 || (now - last_log_ms) >= 5000))
        {
            uint32_t total_chars = charsProcessed();
            bool has_fix = location.isValid();
            GPS_LOG("[GPS::loop] Loop #%lu: chars_processed_this_loop=%lu, total_chars=%lu, has_fix=%d",
                    loop_count, chars_processed, total_chars, has_fix);
            if (has_fix)
            {
                GPS_LOG(", lat=%.6f, lng=%.6f, sat=%d", location.lat(), location.lng(), satellites.value());
            }
            GPS_LOG("\n");
            last_log_ms = now;
        }

        return charsProcessed();
    }

    String getModel()
    {
        return model;
    }

  private:
    struct SatEntry
    {
        bool valid = false;
        gps::GnssSystem sys = gps::GnssSystem::UNKNOWN;
        uint16_t id = 0;
        uint16_t azimuth = 0;
        uint8_t elevation = 0;
        int8_t snr = -1;
        bool used = false;
    };

    int getAck(uint8_t* buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID);
    bool sendUbx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, bool wait_ack);
    void calcUbxChecksum(const uint8_t* data, uint16_t len, uint8_t& ck_a, uint8_t& ck_b);

    void handleNmeaChar(char c);
    void parseNmeaSentence(char* sentence);
    void parseGsv(const char* talker, char** fields, int field_count);
    void parseGsa(const char* talker, char** fields, int field_count);
    gps::GnssSystem sysFromTalker(const char* talker, int sat_id) const;
    void clearSatellitesForSystem(gps::GnssSystem sys);
    void clearUsedForSystem(gps::GnssSystem sys);
    int findSatellite(gps::GnssSystem sys, int id) const;
    int allocSatellite();
    void recalcCounts();

    Stream* _stream;
    String model;

    SatEntry sats_[gps::kMaxGnssSats]{};
    uint8_t sats_in_view_ = 0;
    uint8_t sats_in_use_ = 0;
    gps::GnssFix fix_type_ = gps::GnssFix::NOFIX;
    float hdop_ = 0.0f;

    char nmea_buf_[128]{};
    uint8_t nmea_len_ = 0;
    bool nmea_collecting_ = false;
};
