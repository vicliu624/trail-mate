#include "platform/esp/arduino_common/team/pairing/team_pairing_transport_espnow.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace team::infra
{
namespace
{
constexpr uint8_t kPairingChannel = 1;

void format_mac(const uint8_t* mac, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!mac)
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t node_id_from_mac(const uint8_t* mac)
{
    if (!mac)
    {
        return 0;
    }
    return (static_cast<uint32_t>(mac[2]) << 24) |
           (static_cast<uint32_t>(mac[3]) << 16) |
           (static_cast<uint32_t>(mac[4]) << 8) |
           static_cast<uint32_t>(mac[5]);
}

void log_local_mac()
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK)
    {
        Serial.printf("[Pairing] get mac err=%d (%s)\n",
                      static_cast<int>(err),
                      esp_err_to_name(err));
        return;
    }
    char mac_str[20];
    format_mac(mac, mac_str, sizeof(mac_str));
    Serial.printf("[Pairing] local sta mac=%s node_id=%08lX\n",
                  mac_str,
                  static_cast<unsigned long>(node_id_from_mac(mac)));
}

bool init_wifi_stack()
{
    static bool netif_ready = false;
    if (!netif_ready)
    {
        esp_err_t netif_err = esp_netif_init();
        if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE)
        {
            Serial.printf("[Pairing] netif init err=%d (%s)\n",
                          static_cast<int>(netif_err),
                          esp_err_to_name(netif_err));
        }
        esp_err_t loop_err = esp_event_loop_create_default();
        if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE)
        {
            Serial.printf("[Pairing] event loop err=%d (%s)\n",
                          static_cast<int>(loop_err),
                          esp_err_to_name(loop_err));
        }
        esp_netif_t* sta = esp_netif_create_default_wifi_sta();
        if (!sta)
        {
            Serial.printf("[Pairing] netif create sta failed\n");
        }
        netif_ready = true;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK && init_err != ESP_ERR_WIFI_INIT_STATE)
    {
        Serial.printf("[Pairing] wifi init err=%d (%s)\n",
                      static_cast<int>(init_err),
                      esp_err_to_name(init_err));
        return false;
    }
    esp_err_t storage_err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (storage_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi storage err=%d (%s)\n",
                      static_cast<int>(storage_err),
                      esp_err_to_name(storage_err));
    }
    esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi set mode err=%d (%s)\n",
                      static_cast<int>(mode_err),
                      esp_err_to_name(mode_err));
        return false;
    }
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ps_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi ps err=%d (%s)\n",
                      static_cast<int>(ps_err),
                      esp_err_to_name(ps_err));
    }
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_NOT_STOPPED)
    {
        Serial.printf("[Pairing] wifi start err=%d (%s)\n",
                      static_cast<int>(start_err),
                      esp_err_to_name(start_err));
        return false;
    }
    esp_err_t ch_err = esp_wifi_set_channel(kPairingChannel, WIFI_SECOND_CHAN_NONE);
    if (ch_err != ESP_OK)
    {
        Serial.printf("[Pairing] set channel failed err=%d (%s)\n",
                      static_cast<int>(ch_err),
                      esp_err_to_name(ch_err));
        return false;
    }
    log_local_mac();
    return true;
}

} // namespace

EspNowTeamPairingTransport* EspNowTeamPairingTransport::instance_ = nullptr;

bool EspNowTeamPairingTransport::begin(Receiver& receiver)
{
    if (initialized_)
    {
        receiver_ = &receiver;
        return true;
    }
    if (!init_wifi_stack())
    {
        return false;
    }

    esp_err_t now_err = esp_now_init();
    if (now_err != ESP_OK)
    {
        Serial.printf("[Pairing] esp_now init failed err=%d (%s)\n",
                      static_cast<int>(now_err),
                      esp_err_to_name(now_err));
        return false;
    }

    receiver_ = &receiver;
    instance_ = this;
    esp_now_register_recv_cb([](const uint8_t* mac, const uint8_t* data, int len)
                             {
        if (instance_ && instance_->receiver_)
        {
            instance_->receiver_->onPairingReceive(mac, data, static_cast<size_t>(len));
        } });
    initialized_ = true;
    return true;
}

void EspNowTeamPairingTransport::end()
{
    if (!initialized_)
    {
        return;
    }
    esp_now_deinit();
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK)
    {
        Serial.printf("[Pairing] wifi stop err=%d (%s)\n",
                      static_cast<int>(stop_err),
                      esp_err_to_name(stop_err));
    }
    receiver_ = nullptr;
    initialized_ = false;
    if (instance_ == this)
    {
        instance_ = nullptr;
    }
}

bool EspNowTeamPairingTransport::ensurePeer(const uint8_t* mac)
{
    if (!mac)
    {
        return false;
    }
    if (esp_now_is_peer_exist(mac))
    {
        return true;
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = kPairingChannel;
    peer.ifidx = WIFI_IF_STA;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST)
    {
        Serial.printf("[TeamPairing] add peer failed err=%d\n", static_cast<int>(err));
        return false;
    }
    return true;
}

bool EspNowTeamPairingTransport::send(const uint8_t* mac, const uint8_t* data, size_t len)
{
    if (!mac || !data || len == 0)
    {
        return false;
    }
    esp_err_t err = esp_now_send(mac, data, len);
    return err == ESP_OK;
}

} // namespace team::infra
