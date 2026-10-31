#pragma once
#include "freertos/FreeRTOS.h"
int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*, int);
void vTaskDelete(void*);
void vTaskDelay(TickType_t);
