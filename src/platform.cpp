#include "platform.h"

#if defined(ARDUINO) && !defined(USE_ESP_IDF_TIME)
#include "Arduino.h"

namespace platform
{
    uint32_t millis()
    {
        return ::millis();
    }

    void delay(uint32_t ms)
    {
        ::delay(ms);
    }
}

#else
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"

namespace platform
{
    uint32_t millis()
    {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);
    }

    void delay(uint32_t ms)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

#endif