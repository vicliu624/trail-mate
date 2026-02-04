/**
 * @file app_config.h
 * @brief Application configuration
 */

#pragma once

#include "../chat/domain/chat_policy.h"
#include "../chat/domain/chat_types.h"
#include "../gps/domain/motion_config.h"
#include <Arduino.h>
#include <Preferences.h>

namespace app
{

/**
 * @brief Application configuration
 */
struct AppConfig
{
    // Chat settings
    chat::ChatPolicy chat_policy;
    chat::MeshConfig mesh_config;
    chat::MeshProtocol mesh_protocol;

    // Device settings
    char node_name[32];
    char short_name[16];

    // Channel settings
    bool primary_enabled;
    bool secondary_enabled;
    uint8_t secondary_key[16]; // PSK for secondary channel

    // GPS settings
    uint32_t gps_interval_ms;
    uint8_t gps_mode;
    uint8_t gps_sat_mask;
    uint8_t gps_strategy;
    uint8_t gps_alt_ref;
    uint8_t gps_coord_format;
    gps::MotionConfig motion_config;

    // Map settings
    uint8_t map_coord_system;
    uint8_t map_source;
    bool map_track_enabled;
    uint8_t map_track_interval;
    uint8_t map_track_format;

    // Chat settings (UI defaults)
    uint8_t chat_channel;

    // Network settings
    bool net_duty_cycle;
    uint8_t net_channel_util;

    // Privacy settings
    uint8_t privacy_encrypt_mode;
    bool privacy_pki;
    uint8_t privacy_nmea_output;
    uint8_t privacy_nmea_sentence;

    AppConfig()
    {
        chat_policy = chat::ChatPolicy::outdoor();
        mesh_config = chat::MeshConfig();
        mesh_protocol = chat::MeshProtocol::Meshtastic;
        strcpy(node_name, "TrailMate");
        strcpy(short_name, "TM");
        primary_enabled = true;
        secondary_enabled = false;
        memset(secondary_key, 0, 16);
        gps_interval_ms = 60000;
        gps_mode = 0;
        gps_sat_mask = 0x1 | 0x8 | 0x4;
        gps_strategy = 0;
        gps_alt_ref = 0;
        gps_coord_format = 0;
        motion_config = gps::MotionConfig();

        map_coord_system = 0;
        map_source = 0;
        map_track_enabled = false;
        map_track_interval = 1;
        map_track_format = 0;

        chat_channel = 0;

        net_duty_cycle = true;
        net_channel_util = 0;

        privacy_encrypt_mode = 1;
        privacy_pki = false;
        privacy_nmea_output = 0;
        privacy_nmea_sentence = 0;
    }

    /**
     * @brief Load from Preferences
     */
    bool load(Preferences& prefs)
    {
        prefs.begin("chat", true);

        // Load policy
        chat_policy.enable_relay = prefs.getBool("relay", true);
        chat_policy.hop_limit_default = prefs.getUChar("hop_limit", 2);
        chat_policy.ack_for_broadcast = prefs.getBool("ack_bcast", false);
        chat_policy.ack_for_squad = prefs.getBool("ack_squad", true);
        chat_policy.max_tx_retries = prefs.getUChar("max_retries", 1);

        // Load mesh config
        mesh_config.region = prefs.getUChar("region", 0);
        mesh_config.modem_preset = prefs.getUChar("modem_preset", 0);
        mesh_config.tx_power = prefs.getChar("tx_power", 14);
        mesh_config.hop_limit = prefs.getUChar("mesh_hop_limit", 2);
        mesh_config.enable_relay = prefs.getBool("mesh_relay", true);
        mesh_protocol = static_cast<chat::MeshProtocol>(
            prefs.getUChar("mesh_protocol", static_cast<uint8_t>(chat::MeshProtocol::Meshtastic)));

        // Load device name
        size_t len = prefs.getBytes("node_name", node_name, sizeof(node_name) - 1);
        node_name[len] = '\0';
        len = prefs.getBytes("short_name", short_name, sizeof(short_name) - 1);
        short_name[len] = '\0';

        // Load channel settings
        primary_enabled = prefs.getBool("primary_enabled", true);
        secondary_enabled = prefs.getBool("secondary_enabled", false);
        prefs.getBytes("secondary_key", secondary_key, 16);
        memcpy(mesh_config.secondary_key, secondary_key, sizeof(mesh_config.secondary_key));

        prefs.end();

        prefs.begin("gps", true);
        gps_interval_ms = prefs.getUInt("gps_interval", gps_interval_ms);
        gps_mode = prefs.getUChar("gps_mode", gps_mode);
        gps_sat_mask = prefs.getUChar("gps_sat_mask", gps_sat_mask);
        gps_strategy = prefs.getUChar("gps_strategy", gps_strategy);
        gps_alt_ref = prefs.getUChar("gps_alt_ref", gps_alt_ref);
        gps_coord_format = prefs.getUChar("gps_coord_fmt", gps_coord_format);
        motion_config.idle_timeout_ms = prefs.getUInt("motion_idle_ms", motion_config.idle_timeout_ms);
        motion_config.sensor_id = prefs.getUChar("motion_sensor_id", motion_config.sensor_id);
        prefs.end();

        prefs.begin("settings_v2", true);
        map_coord_system = prefs.getUChar("map_coord", map_coord_system);
        map_source = prefs.getUChar("map_source", map_source);
        map_track_enabled = prefs.getBool("map_track", map_track_enabled);
        map_track_interval = prefs.getUChar("map_track_interval", map_track_interval);
        map_track_format = prefs.getUChar("map_track_format", map_track_format);
        chat_channel = prefs.getUChar("chat_channel", chat_channel);
        net_duty_cycle = prefs.getBool("net_duty_cycle", net_duty_cycle);
        net_channel_util = prefs.getUChar("net_util", net_channel_util);
        privacy_encrypt_mode = prefs.getUChar("privacy_encrypt", privacy_encrypt_mode);
        privacy_pki = prefs.getBool("privacy_pki", privacy_pki);
        privacy_nmea_output = prefs.getUChar("privacy_nmea", privacy_nmea_output);
        privacy_nmea_sentence = prefs.getUChar("privacy_nmea_sent", privacy_nmea_sentence);
        if (prefs.isKey("chat_user"))
        {
            String name = prefs.getString("chat_user", node_name);
            strncpy(node_name, name.c_str(), sizeof(node_name) - 1);
            node_name[sizeof(node_name) - 1] = '\0';
        }
        if (prefs.isKey("chat_short"))
        {
            String short_name_pref = prefs.getString("chat_short", short_name);
            strncpy(short_name, short_name_pref.c_str(), sizeof(short_name) - 1);
            short_name[sizeof(short_name) - 1] = '\0';
        }
        prefs.end();
        return true;
    }

    /**
     * @brief Save to Preferences
     */
    bool save(Preferences& prefs)
    {
        prefs.begin("chat", false);

        // Save policy
        prefs.putBool("relay", chat_policy.enable_relay);
        prefs.putUChar("hop_limit", chat_policy.hop_limit_default);
        prefs.putBool("ack_bcast", chat_policy.ack_for_broadcast);
        prefs.putBool("ack_squad", chat_policy.ack_for_squad);
        prefs.putUChar("max_retries", chat_policy.max_tx_retries);

        // Save mesh config
        prefs.putUChar("region", mesh_config.region);
        prefs.putUChar("modem_preset", mesh_config.modem_preset);
        prefs.putChar("tx_power", mesh_config.tx_power);
        prefs.putUChar("mesh_hop_limit", mesh_config.hop_limit);
        prefs.putBool("mesh_relay", mesh_config.enable_relay);
        prefs.putUChar("mesh_protocol", static_cast<uint8_t>(mesh_protocol));

        // Save device name
        prefs.putBytes("node_name", node_name, strlen(node_name));
        prefs.putBytes("short_name", short_name, strlen(short_name));

        // Save channel settings
        prefs.putBool("primary_enabled", primary_enabled);
        prefs.putBool("secondary_enabled", secondary_enabled);
        memcpy(secondary_key, mesh_config.secondary_key, sizeof(secondary_key));
        prefs.putBytes("secondary_key", secondary_key, 16);

        prefs.end();

        prefs.begin("gps", false);
        prefs.putUInt("gps_interval", gps_interval_ms);
        prefs.putUChar("gps_mode", gps_mode);
        prefs.putUChar("gps_sat_mask", gps_sat_mask);
        prefs.putUChar("gps_strategy", gps_strategy);
        prefs.putUChar("gps_alt_ref", gps_alt_ref);
        prefs.putUChar("gps_coord_fmt", gps_coord_format);
        prefs.putUInt("motion_idle_ms", motion_config.idle_timeout_ms);
        prefs.putUChar("motion_sensor_id", motion_config.sensor_id);
        prefs.end();

        prefs.begin("settings_v2", false);
        prefs.putUChar("map_coord", map_coord_system);
        prefs.putUChar("map_source", map_source);
        prefs.putBool("map_track", map_track_enabled);
        prefs.putUChar("map_track_interval", map_track_interval);
        prefs.putUChar("map_track_format", map_track_format);
        prefs.putUChar("chat_channel", chat_channel);
        prefs.putBool("net_duty_cycle", net_duty_cycle);
        prefs.putUChar("net_util", net_channel_util);
        prefs.putUChar("privacy_encrypt", privacy_encrypt_mode);
        prefs.putBool("privacy_pki", privacy_pki);
        prefs.putUChar("privacy_nmea", privacy_nmea_output);
        prefs.putUChar("privacy_nmea_sent", privacy_nmea_sentence);
        prefs.putString("chat_user", node_name);
        prefs.putString("chat_short", short_name);
        prefs.end();
        return true;
    }
};

} // namespace app
