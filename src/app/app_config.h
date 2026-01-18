/**
 * @file app_config.h
 * @brief Application configuration
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "../chat/domain/chat_types.h"
#include "../chat/domain/chat_policy.h"
#include "../gps/domain/motion_config.h"

namespace app {

/**
 * @brief Application configuration
 */
struct AppConfig {
    // Chat settings
    chat::ChatPolicy chat_policy;
    chat::MeshConfig mesh_config;
    
    // Device settings
    char node_name[32];
    char short_name[16];
    
    // Channel settings
    bool primary_enabled;
    bool secondary_enabled;
    uint8_t secondary_key[16]; // PSK for secondary channel

    // GPS settings
    uint32_t gps_interval_ms;
    gps::MotionConfig motion_config;
    
    AppConfig() {
        chat_policy = chat::ChatPolicy::outdoor();
        mesh_config = chat::MeshConfig();
        strcpy(node_name, "TrailMate");
        strcpy(short_name, "TM");
        primary_enabled = true;
        secondary_enabled = false;
        memset(secondary_key, 0, 16);
        gps_interval_ms = 60000;
        motion_config = gps::MotionConfig();
    }
    
    /**
     * @brief Load from Preferences
     */
    bool load(Preferences& prefs) {
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
        
        // Load device name
        size_t len = prefs.getBytes("node_name", node_name, sizeof(node_name) - 1);
        node_name[len] = '\0';
        len = prefs.getBytes("short_name", short_name, sizeof(short_name) - 1);
        short_name[len] = '\0';
        
        // Load channel settings
        primary_enabled = prefs.getBool("primary_enabled", true);
        secondary_enabled = prefs.getBool("secondary_enabled", false);
        prefs.getBytes("secondary_key", secondary_key, 16);
        
        prefs.end();

        prefs.begin("gps", true);
        gps_interval_ms = prefs.getUInt("gps_interval", gps_interval_ms);
        motion_config.idle_timeout_ms = prefs.getUInt("motion_idle_ms", motion_config.idle_timeout_ms);
        motion_config.sensor_id = prefs.getUChar("motion_sensor_id", motion_config.sensor_id);
        prefs.end();
        return true;
    }
    
    /**
     * @brief Save to Preferences
     */
    bool save(Preferences& prefs) {
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
        
        // Save device name
        prefs.putBytes("node_name", node_name, strlen(node_name));
        prefs.putBytes("short_name", short_name, strlen(short_name));
        
        // Save channel settings
        prefs.putBool("primary_enabled", primary_enabled);
        prefs.putBool("secondary_enabled", secondary_enabled);
        prefs.putBytes("secondary_key", secondary_key, 16);
        
        prefs.end();

        prefs.begin("gps", false);
        prefs.putUInt("gps_interval", gps_interval_ms);
        prefs.putUInt("motion_idle_ms", motion_config.idle_timeout_ms);
        prefs.putUChar("motion_sensor_id", motion_config.sensor_id);
        prefs.end();
        return true;
    }
};

} // namespace app
