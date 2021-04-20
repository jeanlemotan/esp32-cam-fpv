#pragma once

#include <cstring>
#include <cstdint>

#ifdef ESP_PLATFORM
#   include "esp_task_wdt.h"
#else
#   define IRAM_ATTR
#endif

void init_crc8_table();
IRAM_ATTR uint8_t crc8(uint8_t crc, const void *c_ptr, size_t len);
