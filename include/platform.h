#pragma once

#include <stdint.h>

namespace platform
{
   uint32_t time_now_ms();
   void delay_ms(uint32_t ms);

   enum class PinMode
   {
      Input,
      Output
   };

   enum class PinLevel
   {
      Low,
      High
   };

   inline PinLevel operator!(PinLevel level)
   {
      return level == PinLevel::High ? PinLevel::Low : PinLevel::High;
   }

   void gpio_configure(uint8_t pin, PinMode mode);
   void gpio_write(uint8_t pin, PinLevel level);
   PinLevel gpio_read(uint8_t pin);
}
