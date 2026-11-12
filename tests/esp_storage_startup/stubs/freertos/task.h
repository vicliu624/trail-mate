#pragma once
#include "freertos/FreeRTOS.h"
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* name, uint32_t stack_bytes,
                                   void* argument, UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
