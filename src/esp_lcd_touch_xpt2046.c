#include <esp_lcd_touch_xpt2046.h>
#include <string.h>
#include <esp_rom_gpio.h>
#include <esp32-hal-log.h>

// See datasheet XPT2046.pdf
const uint8_t XPT2046_START_Z1_CONVERSION = 0xB1;  // S=1, ADDR=011, MODE=0 (12bits), SER/DFR=0, PD1=0, PD2=1
const uint8_t XPT2046_START_Z2_CONVERSION = 0xC1;  // S=1, ADDR=100, MODE=0 (12bits), SER/DFR=0, PD1=0, PD2=1
const uint8_t XPT2046_START_Y_CONVERSION = 0x91;   // S=1, ADDR=001, MODE=0 (12bits), SER/DFR=0, PD1=0, PD2=1
const uint8_t XPT2046_START_X_CONVERSION = 0xD1;   // S=1, ADDR=101, MODE=0 (12bits), SER/DFR=0, PD1=0, PD2=1
const uint8_t XPT2046_START_BAT_CONVERSION = 0xA7; // S=1, ADDR=010, MODE=0 (12bits), SER/DFR=1, PD1=1, PD2=1
const uint8_t XPT2046_START_Z1_POWER_DOWN = 0xB0;  // S=1, ADDR=011, MODE=0 (12bits), SER/DFR=1, PD1=0, PD2=0
// 12 bits ADC limit
const uint16_t XPT2046_ADC_LIMIT = (1 << 12);

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t xpt2046_set_swap_xy(esp_lcd_touch_handle_t tp, bool swap)
    {
        log_v("xpt2046_set_swap_xy. tp:%08x, swap:%d", tp, swap);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        tp->config.flags.swap_xy = swap;
        return ESP_OK;
    }

    esp_err_t xpt2046_get_swap_xy(esp_lcd_touch_handle_t tp, bool *swap)
    {
        log_v("xpt2046_get_swap_xy. tp:%08x", tp);
        if (tp == NULL || swap == NULL)
        {
            log_e("tp or swap can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        *swap = tp->config.flags.swap_xy;
        return ESP_OK;
    }

    esp_err_t xpt2046_set_mirror_x(esp_lcd_touch_handle_t tp, bool mirror)
    {
        log_v("xpt2046_set_mirror_x. tp:%08x, mirror:%d", tp, mirror);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        tp->config.flags.mirror_x = mirror;
        return ESP_OK;
    }

    esp_err_t xpt2046_get_mirror_x(esp_lcd_touch_handle_t tp, bool *mirror)
    {
        log_v("xpt2046_get_mirror_x. tp:%08x", tp);
        if (tp == NULL || mirror == NULL)
        {
            log_e("tp or mirror can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        *mirror = tp->config.flags.mirror_x;
        return ESP_OK;
    }

    esp_err_t xpt2046_set_mirror_y(esp_lcd_touch_handle_t tp, bool mirror)
    {
        log_v("xpt2046_set_mirror_y. tp:%08x, mirror:%d", tp, mirror);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        tp->config.flags.mirror_y = mirror;
        return ESP_OK;
    }

    esp_err_t xpt2046_get_mirror_y(esp_lcd_touch_handle_t tp, bool *mirror)
    {
        log_v("xpt2046_get_mirror_y. tp:%08x", tp);
        if (tp == NULL || mirror == NULL)
        {
            log_e("tp or mirror can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        *mirror = tp->config.flags.mirror_y;
        return ESP_OK;
    }

    esp_err_t xpt2046_read_register(esp_lcd_touch_handle_t tp, uint8_t reg, uint16_t *value)
    {
        assert(tp != NULL);
        assert(value != NULL);
        uint8_t buf[2];
        esp_err_t res = esp_lcd_panel_io_rx_param(tp->io, reg, buf, sizeof(buf));
        if (res != ESP_OK)
            return res;

        *value = (buf[0] << 8) + buf[1];

        return ESP_OK;
    }

    esp_err_t xpt2046_enter_sleep(esp_lcd_touch_handle_t tp)
    {
        log_v("xpt2046_enter_sleep. tp:%08x", tp);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        uint16_t discard;
        if ((res = xpt2046_read_register(tp, XPT2046_START_Z1_POWER_DOWN, &discard)) != ESP_OK)
        {
            log_w("Could not read XPT2046_START_Z1_POWER_DOWN");
            return res;
        }

        return ESP_OK;
    }

    esp_err_t xpt2046_exit_sleep(esp_lcd_touch_handle_t tp)
    {
        log_v("xpt2046_exit_sleep. tp:%08x", tp);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        uint16_t discard;
        if ((res = xpt2046_read_register(tp, XPT2046_START_Z1_CONVERSION, &discard)) != ESP_OK)
        {
            log_w("Could not read XPT2046_START_Z1_CONVERSION");
            return res;
        }

        return ESP_OK;
    }

    esp_err_t xpt2046_read_data(esp_lcd_touch_handle_t tp)
    {
        log_v("xpt2046_read_data. tp:%08x", tp);
        if (tp == NULL)
        {
            log_e("tp can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        uint16_t z1, z2, z = 0;
        uint16_t x_temp, y_temp;
        uint32_t x = 0, y = 0;
        uint8_t num_points = 0;

        if (((res = xpt2046_read_register(tp, XPT2046_START_Z1_CONVERSION, &z1)) != ESP_OK) ||
            ((res = xpt2046_read_register(tp, XPT2046_START_Z2_CONVERSION, &z2)) != ESP_OK))
        {
            log_w("Could not XPT2046_START_Z1_CONVERSION or XPT2046_START_Z2_CONVERSION");
            return res;
        }

        // Convert to 12 bits Z value.
        z = (z1 >> 3) + (XPT2046_ADC_LIMIT - (z2 >> 3));
        // If the Z exceeds the Z threshold the user has pressed the screen
        if (z >= XPT2046_Z_THRESHOLD)
        {
            // Discard first value as it is usually not reliable.
            if ((res = xpt2046_read_register(tp, XPT2046_START_X_CONVERSION, &x_temp)) != ESP_OK)
            {
                log_w("Could not read XPT2046_START_X_CONVERSION");
                return res;
            }

            // CONFIG_ESP_LCD_TOUCH_MAX_POINTS is to average the points read and gives a better precision
            for (uint8_t idx = 0; idx < CONFIG_ESP_LCD_TOUCH_MAX_POINTS; idx++)
            {
                // Read X and Y positions
                if (((res = xpt2046_read_register(tp, XPT2046_START_X_CONVERSION, &x_temp)) != ESP_OK) ||
                    ((res = xpt2046_read_register(tp, XPT2046_START_Y_CONVERSION, &y_temp)) != ESP_OK))
                {
                    log_w("Could not read XPT2046_START_X_CONVERSION or XPT2046_START_Y_CONVERSION");
                    return res;
                }

                // Add to accumulated raw ADC values
                x += x_temp;
                y += y_temp;
            }

            // Convert X and Y to 12 bits by dropping upper 3 bits and average the accumulated coordinate data points.
            x = (double)(x >> 3) / XPT2046_ADC_LIMIT / CONFIG_ESP_LCD_TOUCH_MAX_POINTS * tp->config.x_max;
            y = (double)(y >> 3) / XPT2046_ADC_LIMIT / CONFIG_ESP_LCD_TOUCH_MAX_POINTS * tp->config.y_max;
            num_points = 1;
        }

        portENTER_CRITICAL(&tp->data.lock);
        tp->data.coords[0].x = x;
        tp->data.coords[0].y = y;
        tp->data.coords[0].strength = z;
        tp->data.points = num_points;
        portEXIT_CRITICAL(&tp->data.lock);

        return ESP_OK;
    }

    bool xpt2046_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
    {
        log_v("xpt2046_get_xy. tp:%08x, x:0x%08x, y:0x%08x, strength:0x%08x, point_num:0x%08x, max_point_num:%d", tp, x, y, strength, point_num, max_point_num);
        if (tp == NULL || x == NULL || y == NULL || point_num == NULL)
        {
            log_e("tp, x, y or point_num can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        portENTER_CRITICAL(&tp->data.lock);
        *point_num = tp->data.points > max_point_num ? max_point_num : tp->data.points;
        for (uint8_t i = 0; i < *point_num; i++)
        {
            if (tp->config.flags.swap_xy)
            {
                x[i] = tp->config.flags.mirror_y ? tp->config.y_max - tp->data.coords[i].y : tp->data.coords[i].y;
                y[i] = tp->config.flags.mirror_x ? tp->config.x_max - tp->data.coords[i].x : tp->data.coords[i].x;
            }
            else
            {
                x[i] = tp->config.flags.mirror_x ? tp->config.x_max - tp->data.coords[i].x : tp->data.coords[i].x;
                y[i] = tp->config.flags.mirror_y ? tp->config.y_max - tp->data.coords[i].y : tp->data.coords[i].y;
            }

            if (strength != NULL)
                strength[i] = tp->data.coords[i].strength;
        }

        tp->data.points = 0;
        portEXIT_CRITICAL(&tp->data.lock);

        return *point_num > 0;
    }

    esp_err_t xpt2046_del(esp_lcd_touch_handle_t tp)
    {
        log_v("xpt2046_del. tp:%08x", tp);
        if (tp != NULL)
        {
            portENTER_CRITICAL(&tp->data.lock);
            // Remove interrupts and reset INT
            if (tp->config.int_gpio_num != GPIO_NUM_NC)
            {
                if (tp->config.interrupt_callback != NULL)
                    gpio_isr_handler_remove(tp->config.int_gpio_num);

                gpio_reset_pin(tp->config.int_gpio_num);
            }

            free(tp);
        }

        return ESP_OK;
    }

    esp_err_t esp_lcd_touch_new_spi_xpt2046(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *handle)
    {
        log_v("esp_lcd_touch_new_spi_xpt2046. io:%08x, config:%08x, handle:%08x", io, config, handle);
        if (io == NULL || config == NULL || handle == NULL)
        {
            log_e("io, config or handle can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        if (config->int_gpio_num != GPIO_NUM_NC && !GPIO_IS_VALID_GPIO(config->int_gpio_num))
        {
            log_e("Invalid GPIO INT pin: %d", config->int_gpio_num);
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        const esp_lcd_touch_handle_t tp = calloc(1, sizeof(esp_lcd_touch_t));
        if (tp == NULL)
        {
            log_e("No memory available for esp_lcd_touch_t");
            return ESP_ERR_NO_MEM;
        }

        tp->io = io;
        tp->enter_sleep = xpt2046_enter_sleep;
        tp->exit_sleep = xpt2046_exit_sleep;
        tp->read_data = xpt2046_read_data;
        tp->set_swap_xy = xpt2046_set_swap_xy;
        tp->get_swap_xy = xpt2046_get_swap_xy;
        tp->set_mirror_x = xpt2046_set_mirror_x;
        tp->get_mirror_x = xpt2046_get_mirror_x;
        tp->set_mirror_y = xpt2046_set_mirror_y;
        tp->get_mirror_y = xpt2046_get_mirror_y;
        tp->get_xy = xpt2046_get_xy;
        tp->del = xpt2046_del;
        tp->data.lock.owner = portMUX_FREE_VAL;
        memcpy(&tp->config, config, sizeof(esp_lcd_touch_config_t));

        if (config->int_gpio_num != GPIO_NUM_NC)
        {
            esp_rom_gpio_pad_select_gpio(config->int_gpio_num);
            const gpio_config_t cfg = {
                .pin_bit_mask = BIT64(config->int_gpio_num),
                .mode = GPIO_MODE_INPUT,
                // If the user has provided a callback routine for the interrupt enable the interrupt mode on the negative edge.
                .intr_type = config->interrupt_callback ? GPIO_INTR_NEGEDGE : GPIO_INTR_DISABLE};
            if ((res = gpio_config(&cfg)) != ESP_OK)
            {
                free(tp);
                log_e("Configuring GPIO for INT failed");
                return res;
            }

            if (config->interrupt_callback != NULL)
            {
                if ((res = esp_lcd_touch_register_interrupt_callback(tp, config->interrupt_callback)) != ESP_OK)
                {
                    gpio_reset_pin(tp->config.int_gpio_num);
                    free(tp);
                    log_e("Registering INT callback failed");
                    return res;
                }
            }
        }

        if (config->rst_gpio_num != GPIO_NUM_NC)
            log_w("RST pin defined but is not available on the XPT2046");

        *handle = tp;

        return ESP_OK;
    }

    esp_err_t esp_lcd_touch_xpt2046_read_battery_level(const esp_lcd_touch_handle_t tp, float *output)
    {
        log_v("esp_lcd_touch_xpt2046_read_battery_level. tp:%08x, output:0x%08x", tp, output);
        if (tp == NULL || output == NULL)
        {
            log_e("tp or output can not be null");
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        uint16_t level;
        // Read Y position and convert returned data to 12bit value
        if ((res = xpt2046_read_register(tp, XPT2046_START_BAT_CONVERSION, &level)) != ESP_OK)
        {
            log_w("Could not read battery level");
            return res;
        }

        // battery voltage is reported as 1/4 the actual voltage due to logic in the chip.
        // adjust for internal vref of 2.5v
        // adjust for ADC bit count
        *output = level * 4 * 2.5f / XPT2046_ADC_LIMIT;

        return ESP_OK;
    }

#ifdef __cplusplus
}
#endif