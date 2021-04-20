#include "safe_printf.h"

SemaphoreHandle_t s_safe_printf_mux = xSemaphoreCreateBinary();

static auto _init_result = []() -> bool
{
  xSemaphoreGive(s_safe_printf_mux);
  return true;
}();


