#if defined(LilyGo_TDeck_Pro_Max)
// =============================================================================
// hyn_platform.cpp -- Meck port.
//
// The LilyGo factory version drove the touch reset through its own
// ExtensionIOXL9555 class. Meck has its own XL9555 access on the board object,
// so the ExtensionIOXL9555 path is removed. Virtual XL9555 GPIOs are routed via
// the write/read callbacks registered from main.cpp (which call
// board.xl9555_digitalWrite / board.xl9555_digitalRead), mirroring the factory
// behaviour: the driver performs its own CST328 reset over XL9555 P07 during
// chip init.
// =============================================================================

#include "hyn_platform.h"

#include <Arduino.h>
#include <esp_log.h>

#include "HynTouch.h"
#include "HynTouchBoard.h"

namespace {

constexpr const char *kTag = "HynTouch";

HynTouchVirtualGpioWriteCallback g_gpio_write_callback = nullptr;
HynTouchVirtualGpioReadCallback g_gpio_read_callback = nullptr;
void *g_gpio_callback_user_data = nullptr;

} // namespace

int hyn_platform_gpio_set_value(uint32_t gpio_id, int value)
{
    if (g_gpio_write_callback && g_gpio_write_callback(gpio_id, value != 0, g_gpio_callback_user_data)) {
        return 1;
    }
    if (XL9555_GPIO_IS((int)gpio_id)) {
        ESP_LOGE(kTag, "Virtual GPIO %lu needs a handler", (unsigned long)gpio_id);
        return -1;
    }
    return 0;
}

int hyn_platform_gpio_get_value(uint32_t gpio_id, int *out_value)
{
    if (!out_value) {
        return -1;
    }
    if (g_gpio_read_callback && g_gpio_read_callback(gpio_id, out_value, g_gpio_callback_user_data)) {
        return 1;
    }
    if (XL9555_GPIO_IS((int)gpio_id)) {
        ESP_LOGE(kTag, "Virtual GPIO %lu needs a handler", (unsigned long)gpio_id);
        return -1;
    }
    return 0;
}

void hyn_touch_set_virtual_gpio_callbacks(
    HynTouchVirtualGpioWriteCallback write_callback,
    HynTouchVirtualGpioReadCallback read_callback,
    void *user_data)
{
    g_gpio_write_callback = write_callback;
    g_gpio_read_callback = read_callback;
    g_gpio_callback_user_data = user_data;
}

#endif // LilyGo_TDeck_Pro_Max
