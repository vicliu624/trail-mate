#pragma once
#include <cstdint>
int xTaskCreate(void (*entry)(void*), const char*, unsigned, void*, unsigned, void*);
void vTaskDelete(void*);
void vTaskDelay(unsigned);
