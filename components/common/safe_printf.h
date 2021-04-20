#pragma once

#include <cassert>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t s_safe_printf_mux;

#define SAFE_PRINTF(...)  \
    do { xSemaphoreTake(s_safe_printf_mux, portMAX_DELAY); \
        ets_printf(__VA_ARGS__); \
        xSemaphoreGive(s_safe_printf_mux); \
    } while (false)

