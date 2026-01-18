/**
 * @file app_tasks.cpp
 * @brief Application task implementation
 */

#include "app_tasks.h"
#include <Arduino.h>

#ifndef LORA_LOG_ENABLE
#define LORA_LOG_ENABLE 1
#endif

#if LORA_LOG_ENABLE
#define LORA_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LORA_LOG(...) do {} while (0)
#endif

namespace app {

// Static members
QueueHandle_t AppTasks::radio_tx_queue_ = nullptr;
QueueHandle_t AppTasks::radio_rx_queue_ = nullptr;
QueueHandle_t AppTasks::mesh_queue_ = nullptr;
TaskHandle_t AppTasks::radio_task_handle_ = nullptr;
TaskHandle_t AppTasks::mesh_task_handle_ = nullptr;
TLoRaPagerBoard* AppTasks::board_ = nullptr;
chat::meshtastic::MtAdapter* AppTasks::adapter_ = nullptr;

bool AppTasks::init(TLoRaPagerBoard& board, chat::meshtastic::MtAdapter* adapter) {
    board_ = &board;
    adapter_ = adapter;
    
    // Create queues
    radio_tx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    radio_rx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    mesh_queue_ = xQueueCreate(MESH_QUEUE_SIZE, sizeof(RadioPacket));
    
    if (!radio_tx_queue_ || !radio_rx_queue_ || !mesh_queue_) {
        return false;
    }
    
    // Create radio task (high priority)
    BaseType_t result = xTaskCreate(
        radioTask,
        "radio_task",
        4 * 1024,  // Stack size
        nullptr,
        10,  // High priority
        &radio_task_handle_
    );
    
    if (result != pdPASS) {
        return false;
    }
    
    // Create mesh task (medium priority)
    result = xTaskCreate(
        meshTask,
        "mesh_task",
        6 * 1024,  // Stack size
        nullptr,
        5,  // Medium priority
        &mesh_task_handle_
    );
    
    return (result == pdPASS);
}

void AppTasks::radioTask(void* pvParameters) {
    (void)pvParameters;
    
    const TickType_t poll_delay = pdMS_TO_TICKS(10);
    uint8_t rx_buffer[255];
    bool rx_started = false;
    
    while (true) {
        // Process TX queue
        RadioPacket tx_packet;
        if (xQueueReceive(radio_tx_queue_, &tx_packet, 0) == pdPASS) {
            if (tx_packet.is_tx && tx_packet.data && tx_packet.size > 0) {
                // Send packet
                if (board_ && board_->isHardwareOnline(HW_RADIO_ONLINE)) {
                    int state = RADIOLIB_ERR_NONE;
#if defined(ARDUINO_LILYGO_LORA_SX1262)
                    state = board_->radio.transmit(tx_packet.data, tx_packet.size);
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
                    state = board_->radio.transmit(tx_packet.data, tx_packet.size);
#endif
                    LORA_LOG("[LORA] TX queue len=%u state=%d\n", (unsigned)tx_packet.size, state);
                    if (state == RADIOLIB_ERR_NONE) {
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
                        int rx_state = board_->radio.startReceive();
                        if (rx_state == RADIOLIB_ERR_NONE) {
                            rx_started = true;
                        } else {
                            LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                        }
#endif
                    }
                    // Free buffer (if allocated)
                    if (tx_packet.data) {
                        free(tx_packet.data);
                    }
                } else {
                    LORA_LOG("[LORA] TX drop (radio offline) len=%u\n", (unsigned)tx_packet.size);
                }
            }
        }
        
        // Poll for RX (non-blocking)
        if (board_ && board_->isHardwareOnline(HW_RADIO_ONLINE)) {
            if (!rx_started) {
#if defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_SX1280)
                int rx_state = board_->radio.startReceive();
                if (rx_state == RADIOLIB_ERR_NONE) {
                    rx_started = true;
                } else {
                    LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                }
#endif
            }
            // Check if data available using RadioLib IRQs
            int packet_length = 0;
#if defined(ARDUINO_LILYGO_LORA_SX1262)
            uint32_t irq = board_->radio.getIrqFlags();
            if (irq & RADIOLIB_SX126X_IRQ_RX_DONE) {
                packet_length = static_cast<int>(board_->radio.getPacketLength(true));
                if (packet_length > 0 && packet_length <= 255) {
                    int state = board_->radio.readData(rx_buffer, packet_length);
                    if (state == RADIOLIB_ERR_NONE) {
                        RadioPacket rx_packet;
                        rx_packet.data = (uint8_t*)malloc(packet_length);
                        if (rx_packet.data) {
                            memcpy(rx_packet.data, rx_buffer, packet_length);
                            rx_packet.size = packet_length;
                            rx_packet.is_tx = false;
                            
                            LORA_LOG("[LORA] RX len=%d\n", packet_length);
                            // Send to mesh queue
                            xQueueSend(mesh_queue_, &rx_packet, portMAX_DELAY);
                        }
                    } else {
                        LORA_LOG("[LORA] RX read fail len=%d state=%d\n", packet_length, state);
                    }
                }
            } else if (irq) {
                board_->radio.clearIrqFlags(irq);
            }
#elif defined(ARDUINO_LILYGO_LORA_SX1280)
            uint32_t irq = board_->radio.getIrqFlags();
            if (irq & RADIOLIB_SX128X_IRQ_RX_DONE) {
                packet_length = static_cast<int>(board_->radio.getPacketLength(true));
                if (packet_length > 0 && packet_length <= 255) {
                    int state = board_->radio.readData(rx_buffer, packet_length);
                    if (state == RADIOLIB_ERR_NONE) {
                        RadioPacket rx_packet;
                        rx_packet.data = (uint8_t*)malloc(packet_length);
                        if (rx_packet.data) {
                            memcpy(rx_packet.data, rx_buffer, packet_length);
                            rx_packet.size = packet_length;
                            rx_packet.is_tx = false;
                            
                            LORA_LOG("[LORA] RX len=%d\n", packet_length);
                            // Send to mesh queue
                            xQueueSend(mesh_queue_, &rx_packet, portMAX_DELAY);
                        }
                    } else {
                        LORA_LOG("[LORA] RX read fail len=%d state=%d\n", packet_length, state);
                    }
                }
            } else if (irq) {
                board_->radio.clearIrqFlags(irq);
            }
#endif
            if (packet_length > 0) {
                int rx_state = board_->radio.startReceive();
                if (rx_state == RADIOLIB_ERR_NONE) {
                    rx_started = true;
                } else {
                    rx_started = false;
                    LORA_LOG("[LORA] RX restart fail state=%d\n", rx_state);
                }
            }
        }
        
        vTaskDelay(poll_delay);
    }
}

void AppTasks::meshTask(void* pvParameters) {
    (void)pvParameters;
    
    const TickType_t poll_delay = pdMS_TO_TICKS(50);
    
    while (true) {
        // Process received packets
        RadioPacket rx_packet;
        if (xQueueReceive(mesh_queue_, &rx_packet, 0) == pdPASS) {
            if (!rx_packet.is_tx && rx_packet.data && adapter_) {
                // Decode and process
                adapter_->processReceivedPacket(rx_packet.data, rx_packet.size);
                
                // Free buffer
                free(rx_packet.data);
            }
        }
        
        // Process send queue in adapter
        if (adapter_) {
            adapter_->processSendQueue();
        }
        
        vTaskDelay(poll_delay);
    }
}

} // namespace app
