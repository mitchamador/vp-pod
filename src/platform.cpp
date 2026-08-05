#include "platform.h"
#include "esPod_conf.h"

#if defined(ARDUINO) && !defined(USE_ESP_IDF_TIME)
#include "Arduino.h"
#endif
#if defined(ARDUINO) && !defined(USE_ESP_IDF_GPIO)
#include "Arduino.h" // no-op if already included above, header has its own guard
#endif

#if !(defined(ARDUINO) && !defined(USE_ESP_IDF_TIME))
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#endif
#if !(defined(ARDUINO) && !defined(USE_ESP_IDF_GPIO))
#include "driver/gpio.h"
#endif

namespace platform
{
#if defined(ARDUINO) && !defined(USE_ESP_IDF_TIME)

    uint32_t time_now_ms()
    {
        return ::millis();
    }

    void delay_ms(uint32_t ms)
    {
        ::delay(ms);
    }

#else

    uint32_t time_now_ms()
    {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);
    }

    void delay_ms(uint32_t ms)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

#endif

#if defined(ARDUINO) && !defined(USE_ESP_IDF_GPIO)

    void gpio_configure(uint8_t pin, PinMode mode)
    {
        ::pinMode(pin, mode == PinMode::Output ? OUTPUT : INPUT);
    }

    void gpio_write(uint8_t pin, PinLevel level)
    {
        ::digitalWrite(pin, level == PinLevel::High ? HIGH : LOW);
    }

    PinLevel gpio_read(uint8_t pin)
    {
        return ::digitalRead(pin) == LOW ? PinLevel::Low : PinLevel::High;
    }

#else

    void gpio_configure(uint8_t pin, PinMode mode)
    {
        gpio_num_t gpio = static_cast<gpio_num_t>(pin);
        gpio_reset_pin(gpio);
        gpio_set_direction(gpio, mode == PinMode::Output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    }

    void gpio_write(uint8_t pin, PinLevel level)
    {
        gpio_set_level(static_cast<gpio_num_t>(pin), level == PinLevel::High ? 1 : 0);
    }

    PinLevel gpio_read(uint8_t pin)
    {
        return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0 ? PinLevel::Low : PinLevel::High;
    }

#endif
}
