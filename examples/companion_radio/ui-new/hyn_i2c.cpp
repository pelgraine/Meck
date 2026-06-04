#if defined(LilyGo_TDeck_Pro_Max)
// =============================================================================
// hyn_i2c.cpp -- Meck port of the HynTouch I2C transport.
//
// The LilyGo factory hyn_i2c.c drove the raw ESP-IDF I2C master on port 0 at
// 200 kHz. On the MAX the CST328 shares the Wire bus with the XL9555, keyboard,
// charger, gyro, codec and RTC, so this transport is rewritten to use the same
// Arduino Wire object as the rest of Meck. The register transaction shape is
// unchanged from the original driver: write the address bytes with a STOP, then
// a separate read. Wire is already begun by the board, so hyn_i2c_init is a
// no-op here.
//
// (Replaces hyn_i2c.c -- delete the .c when adding this .cpp.)
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

#include "hyn_core.h"      // declares these functions extern "C"
#include "hyn_platform.h"

esp_err_t hyn_i2c_init(u8 pin_sda, u8 pin_scl)
{
    // Wire is initialised by the board at the correct pins; nothing to do.
    (void)pin_sda;
    (void)pin_scl;
    return ESP_OK;
}

int hyn_write_data(struct hyn_ts_data *ts_data, u8 *buf, u8 reg_len, u16 len)
{
    (void)reg_len;
    Wire.beginTransmission(ts_data->salve_addr);
    Wire.write(buf, len);
    return Wire.endTransmission(true) == 0 ? 0 : -1;
}

int hyn_read_data(struct hyn_ts_data *ts_data, u8 *buf, u16 len)
{
    uint8_t got = Wire.requestFrom((int)ts_data->salve_addr, (int)len);
    if (got < len) return -1;
    for (u16 i = 0; i < len; i++) buf[i] = Wire.read();
    return 0;
}

int hyn_wr_reg(struct hyn_ts_data *ts_data, u32 reg_addr, u8 reg_len, u8 *rbuf, u16 rlen)
{
    u8 wbuf[4];
    int i;
    reg_len = reg_len & 0x0F;          // high bit is a flag for some chips; mask it
    memset(wbuf, 0, sizeof(wbuf));
    i = reg_len;
    while (i) {
        i--;
        wbuf[i] = (u8)(reg_addr & 0xFF);
        reg_addr >>= 8;
    }

    Wire.beginTransmission(ts_data->salve_addr);
    if (reg_len) Wire.write(wbuf, reg_len);
    if (Wire.endTransmission(true) != 0) return -1;

    if (rlen) {
        uint8_t got = Wire.requestFrom((int)ts_data->salve_addr, (int)rlen);
        if (got < rlen) return -1;
        for (u16 k = 0; k < rlen; k++) rbuf[k] = Wire.read();
    }
    return 0;
}

void hyn_delay_ms(int cnt)
{
    delay(cnt);
}

// gpio ctl -- virtual XL9555 pins are handled by the platform callbacks;
// any real GPIO falls through to the native driver.
int gpio_set_value(uint32_t gpio_id, bool vlue)
{
    int handled = hyn_platform_gpio_set_value(gpio_id, vlue ? 1 : 0);
    if (handled > 0) {
        return 0;
    }
    if (handled < 0 || (int32_t)gpio_id < 0) {
        return -1;
    }
    gpio_set_level((gpio_num_t)gpio_id, vlue);
    return 0;
}

bool gpio_get_value(uint32_t gpio_id)
{
    int value = 0;
    int handled = hyn_platform_gpio_get_value(gpio_id, &value);
    if (handled > 0) {
        return value ? true : false;
    }
    if (handled < 0 || (int32_t)gpio_id < 0) {
        return false;
    }
    return gpio_get_level((gpio_num_t)gpio_id);
}

#endif // LilyGo_TDeck_Pro_Max
