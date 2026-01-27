/**
 * @file app_tasks.h
 * @brief Application task management
 */

#pragma once

#include "../board/LoraBoard.h"
#include "../chat/ports/i_mesh_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>

namespace app
{

/**
 * @brief Task management
 */
class AppTasks
{
  public:
    static constexpr size_t RADIO_QUEUE_SIZE = 10;
    static constexpr size_t MESH_QUEUE_SIZE = 10;

    struct RadioPacket
    {
        uint8_t* data;
        size_t size;
        bool is_tx; // true for TX, false for RX
    };

    /**
     * @brief Initialize tasks
     * @param board Board instance
     * @param adapter Mesh adapter
     */
    static bool init(LoraBoard& board, chat::IMeshAdapter* adapter);

    /**
     * @brief Radio task (high priority)
     */
    static void radioTask(void* pvParameters);

    /**
     * @brief Mesh task (medium priority)
     */
    static void meshTask(void* pvParameters);

    /**
     * @brief Get radio TX queue
     */
    static QueueHandle_t getRadioTxQueue()
    {
        return radio_tx_queue_;
    }

    /**
     * @brief Get radio RX queue
     */
    static QueueHandle_t getRadioRxQueue()
    {
        return radio_rx_queue_;
    }

  private:
    static QueueHandle_t radio_tx_queue_;
    static QueueHandle_t radio_rx_queue_;
    static QueueHandle_t mesh_queue_;
    static TaskHandle_t radio_task_handle_;
    static TaskHandle_t mesh_task_handle_;
    static LoraBoard* board_;
    static chat::IMeshAdapter* adapter_;
};

} // namespace app
