#ifdef TOUCH_GT911_I2C

#include <esp_lcd_touch_gt911.h>
#include <string.h>
#include <esp_rom_gpio.h>
#include <esp32-hal-log.h>

// Registers
const uint16_t GT911_READ_KEY_REG = 0x8093;
const uint16_t GT911_READ_XY_REG = 0x814E;
const uint16_t GT911_READ_XY_REG_POINTS = 0x814F;
const uint16_t GT911_CONFIG_REG = 0x8047;
const uint16_t GT911_PRODUCT_ID_REG = 0x8140;
const uint16_t GT911_CONTROL_REG = 0x8040;

// Limits of points / buttons
#define GT911_MAX_BUTTONS 4
#define GT911_TOUCH_MAX_POINTS 5

#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > GT911_TOUCH_MAX_BUTTONS)
#error more buttons than available
#endif

// Touch events
const uint8_t GT911_TOUCH_EVENT_NONE = 0;
const uint8_t GT911_TOUCH_EVENT_DOWN = 1;
const uint8_t GT911_TOUCH_EVENT_UP = 2;
const uint8_t GT911_TOUCH_EVENT_SLIDE = 3;
const uint8_t GT911_TOUCH_EVENT_SLIDE_END = 4;

// Two I2C Addresses possible
#define GT911_IO_I2C_CONFIG_DEV_ADDRESS_5D 0x5D // 0x5D (0xBA/0xBB): INT High during reset or
#define GT911_IO_I2C_CONFIG_DEV_ADDRESS_14 0x14 // 0x14 (0x28/0x29): INT Low during reset

#define FLAGS_BUFFER_STATUS 0x80    // coordinate (or key) is ready for host to read
#define FLAGS_LARGE_DETECT 0x40     // large-area touch on th
#define FLAGS_HAVE_KEY 0x10         //  Have touch key
#define FLAGS_NUM_TOUCH_POINTS 0x0F // Number of touch points

typedef struct __attribute__((packed))
{
    char productId[4];    // 0x8140 - 0x8143
    uint16_t fwId;        // 0x8144 - 0x8145
    uint16_t xResolution; // 0x8146 - 0x8147
    uint16_t yResolution; // 0x8148 - 0x8149
    uint8_t vendorId;     // 0x814A
} gt911_info;

typedef struct __attribute__((packed))
{
    // 0x814F-0x8156, ... 0x8176 (5 points)
    uint8_t event;
    uint16_t x;
    uint16_t y;
    uint16_t area;
    uint8_t reserved;
} gt911_touch_event;

typedef struct __attribute__((packed))
{
    uint8_t flags;
    union esp_lcd_touch_gt911
    {
        uint8_t buttons[GT911_MAX_BUTTONS];
        gt911_touch_event points[GT911_TOUCH_MAX_POINTS];
    } data;
} gt911_touch_data;

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t gt911_reset(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_reset. th:%08x", th);

        esp_err_t res;

        // Set RST active
        if ((res = gpio_set_level(th->config.rst_gpio_num, th->config.levels.reset)) != ESP_OK)
        {
            log_e("Setting RST failed");
            return res;
        }

        // Wait at least 100us
        vTaskDelay(pdMS_TO_TICKS(1));

        // Set RST high
        if ((res = gpio_set_level(th->config.rst_gpio_num, !th->config.levels.reset)) != ESP_OK)
        {
            log_e("Resetting RST failed");
            return res;
        }

        // Wait at least 5ms
        vTaskDelay(pdMS_TO_TICKS(5));

        return ESP_OK;
    }

    esp_err_t gt911_read_info(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_read_info. th:%08x", th);

        esp_err_t res;

        // Info is stored in the user_data
        if (th->config.user_data != NULL)
        {
            free(th->config.user_data);
            th->config.user_data = NULL;
        }

        gt911_info *info = heap_caps_calloc(1, sizeof(gt911_info), MALLOC_CAP_DEFAULT);
        if (info == NULL)
        {
            log_e("No memory available for gt911_info");
            return ESP_ERR_NO_MEM;
        }

        if ((res = esp_lcd_panel_io_rx_param(th->io, GT911_PRODUCT_ID_REG, info, sizeof(gt911_info))) != ESP_OK)
        {
            free(info);
            log_e("Unable to read GT911 info");
            return res;
        }

        if (strcmp((char *)&info->productId, "911") != 0)
        {
            log_e("GT911 chip not found");
            return ESP_FAIL;
        }

        if (info->xResolution == 0 || info->yResolution == 0)
        {
            log_e("Invalid resolution in GT911");
            return ESP_FAIL;
        }

        log_d("GT911 productId: %s", info->productId);                                          // 0x8140 - 0x8143
        log_d("GT911 fwId: %04x", info->fwId);                                                  // 0x8144 - 0x8145
        log_d("GT911 xResolution/yResolution: (%d, %d)", info->xResolution, info->yResolution); // 0x8146 - 0x8147 // 0x8148 - 0x8149
        log_d("GT911 vendorId: %02x", info->vendorId);                                          // 0x814A

        th->config.user_data = info;

        return ESP_OK;
    }

    esp_err_t gt911_enter_sleep(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_enter_sleep. th:%08x", th);

        esp_err_t res;
        const uint8_t data[] = {0x05}; // Sleep
        if ((res = esp_lcd_panel_io_tx_param(th->io, GT911_CONTROL_REG, data, sizeof(data))) != ESP_OK)
            log_e("Unable to write GT911_ENTER_SLEEP");

        return res;
    }

    esp_err_t gt911_exit_sleep(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_exit_sleep. th:%08x", th);

        esp_err_t res;
        if (th->config.int_gpio_num == GPIO_NUM_NC)
        {
            log_w("No INT pin defined");
            return ESP_OK;
        }

        gpio_config_t cfg = {
            .pin_bit_mask = BIT64(th->config.int_gpio_num),
            .mode = GPIO_MODE_OUTPUT};
        if ((res = gpio_config(&cfg)) != ESP_OK)
        {
            log_e("Setting INT pin high failed");
            return res;
        }

        gpio_set_level(th->config.int_gpio_num, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
        cfg.mode = GPIO_MODE_OUTPUT_OD;
        if ((res = gpio_config(&cfg)) != ESP_OK)
        {
            log_e("Setting INT pin float failed");
            return res;
        }

        return ESP_OK;
    }

    esp_err_t gt911_read_data(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_read_data. th:%08x", th);

        esp_err_t res;
        gt911_touch_data buffer;
        uint8_t i;

        // Read only the XY register
        if ((res = esp_lcd_panel_io_rx_param(th->io, GT911_READ_XY_REG, &buffer.flags, sizeof(buffer.flags))) != ESP_OK)
        {
            log_e("Unable to read GT911_READ_XY_REG");
            return res;
        }

        if ((buffer.flags & FLAGS_BUFFER_STATUS) > 0)
        {
#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
            if ((buffer.flags & FLAGS_HAVE_KEY) > 0)
            {
                log_v("buttons available");
                if ((res = esp_lcd_panel_io_rx_param(th->io, GT911_READ_KEY_REG, &buffer.data.buttons, CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS)) != ESP_OK)
                {
                    log_e("Unable to read GT911_READ_KEY_REG");
                    return res;
                }

                portENTER_CRITICAL(&th->data.lock);
                th->data.buttons = CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS;
                for (i = 0; i < CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS; i++)
                    th->data.button[i].status = buffer.data.buttons[i];

                portEXIT_CRITICAL(&th->data.lock);
            }
#endif
            uint8_t points_available = buffer.flags & FLAGS_NUM_TOUCH_POINTS;
            //  Check if data is present
            if (points_available > 0)
            {
                log_v("points_available: %d", points_available);
                // Read the number of touches
                if (points_available <= GT911_TOUCH_MAX_POINTS)
                {
                    // Read the points
                    if ((res = esp_lcd_panel_io_rx_param(th->io, GT911_READ_XY_REG_POINTS, &buffer.data.points, points_available * sizeof(gt911_touch_event))) != ESP_OK)
                    {
                        log_e("Unable to read GT911_READ_XY_REG_POINTS");
                        return res;
                    }

#if ARDUHAL_LOG_LEVEL > ARDUHAL_LOG_LEVEL_NONE
                    for (uint8_t i = 0; i < points_available; i++)
                        log_v("event:%d, x:%d, y:%d, area:%d", buffer.data.points[i].event, buffer.data.points[i].x, buffer.data.points[i].y, buffer.data.points[i].area);
#endif

                    portENTER_CRITICAL(&th->data.lock);
                    gt911_info *info = th->config.user_data;
                    assert(info);
                    th->data.points = points_available;
                    for (i = 0; i < points_available; i++)
                    {
                        // Correct the points for the info returned from the GT911 and configured resolution
                        th->data.coords[i].x = (double)buffer.data.points[i].x * th->config.x_max / info->xResolution;
                        th->data.coords[i].y = (double)buffer.data.points[i].y * th->config.y_max / info->yResolution;
                        th->data.coords[i].strength = buffer.data.points[i].area;
                    }

                    portEXIT_CRITICAL(&th->data.lock);
                }
            }
        }

        uint8_t clear[] = {0};
        if ((res = esp_lcd_panel_io_tx_param(th->io, GT911_READ_XY_REG, clear, sizeof(clear))) != ESP_OK)
        {
            log_e("Unable to write T911_READ_XY_REG");
            return res;
        }

        return ESP_OK;
    }

    bool gt911_get_xy(esp_lcd_touch_handle_t th, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
    {
        log_v("gt911_get_xy. th:%08x, x:0x%08x, y:0x%08x, strength:0x%08x, point_num:0x%08x, max_point_num:%d", th, x, y, strength, point_num, max_point_num);

        portENTER_CRITICAL(&th->data.lock);
        *point_num = th->data.points > max_point_num ? max_point_num : th->data.points;
        for (uint8_t i = 0; i < *point_num; i++)
        {
            x[i] = th->data.coords[i].y;
            y[i] = th->data.coords[i].x;
            if (strength != NULL)
                strength[i] = th->data.coords[i].strength;
        }

        th->data.points = 0;
        portEXIT_CRITICAL(&th->data.lock);

        return *point_num > 0;
    }

#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
    esp_err_t gt911_get_button_state(esp_lcd_touch_handle_t th, uint8_t n, uint8_t *state)
    {
        log_v("gt911_get_xy. th:%08x, n:%d, state:0x%08x", th, n, state);

        if (n > th->data.buttons)
        {
            log_e("Button out of range");
            return ESP_ERR_INVALID_ARG;
        }

        portENTER_CRITICAL(&th->data.lock);
        *state = th->data.button[n].status;
        portEXIT_CRITICAL(&th->data.lock);

        return ESP_OK;
    }
#endif

    esp_err_t gt911_del(esp_lcd_touch_handle_t th)
    {
        log_v("gt911_del. th:%08x", th);

        portENTER_CRITICAL(&th->data.lock);
        // Remove gt911_info
        if (th->config.user_data != NULL)
            free(th->config.user_data);

        // Remove interrupts and reset INT
        if (th->config.int_gpio_num != GPIO_NUM_NC)
        {
            if (th->config.interrupt_callback)
                gpio_isr_handler_remove(th->config.int_gpio_num);

            gpio_reset_pin(th->config.int_gpio_num);
        }

        // Reset RST
        if (th->config.rst_gpio_num != GPIO_NUM_NC)
            gpio_reset_pin(th->config.rst_gpio_num);

        free(th);

        return ESP_OK;
    }

    esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *handle)
    {
        log_v("esp_lcd_touch_new_spi_gt911. io:%08x, config:%08x, handle:%08x", io, config, handle);

        assert(io != NULL);
        assert(config != NULL);
        assert(handle != NULL);

        if (config->int_gpio_num != GPIO_NUM_NC && !GPIO_IS_VALID_GPIO(config->int_gpio_num))
        {
            log_e("Invalid GPIO INT pin: %d", config->int_gpio_num);
            return ESP_ERR_INVALID_ARG;
        }

        if (config->rst_gpio_num != GPIO_NUM_NC && !GPIO_IS_VALID_GPIO(config->rst_gpio_num))
        {
            log_e("Invalid GPIO RST pin: %d", config->rst_gpio_num);
            return ESP_ERR_INVALID_ARG;
        }

        esp_err_t res;
        const esp_lcd_touch_handle_t th = heap_caps_aligned_alloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
        if (th == NULL)
        {
            log_e("No memory available for esp_lcd_touch_t");
            return ESP_ERR_NO_MEM;
        }

        th->io = io;
        th->enter_sleep = gt911_enter_sleep;
        th->exit_sleep = gt911_exit_sleep;
        th->read_data = gt911_read_data;
        th->get_xy = gt911_get_xy;
#if (CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS > 0)
        th->get_button_state = gt911_get_button_state;
#endif
        th->del = gt911_del;
        memcpy(&th->config, config, sizeof(esp_lcd_touch_config_t));
        portMUX_INITIALIZE(&th->data.lock);

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
                free(th);
                log_e("Configuring GPIO for INT failed");
                return res;
            }

            if (config->interrupt_callback != NULL)
            {
                if ((res = esp_lcd_touch_register_interrupt_callback(th, config->interrupt_callback)) != ESP_OK)
                {
                    gpio_reset_pin(th->config.int_gpio_num);
                    free(th);
                    log_e("Registering INT callback failed");
                    return res;
                }
            }

            if (config->rst_gpio_num != GPIO_NUM_NC)
            {
                esp_rom_gpio_pad_select_gpio(config->rst_gpio_num);
                const gpio_config_t cfg = {
                    .pin_bit_mask = BIT64(config->rst_gpio_num),
                    .mode = GPIO_MODE_OUTPUT};
                if ((res = gpio_config(&cfg)) != ESP_OK)
                {
                    if (th->config.int_gpio_num != GPIO_NUM_NC)
                    {
                        if (config->interrupt_callback != NULL)
                            gpio_isr_handler_remove(th->config.int_gpio_num);

                        gpio_reset_pin(th->config.int_gpio_num);
                    }

                    free(th);
                    log_e("Configuring or setting GPIO for RST failed");
                    return res;
                }
            }

            // Reset controller
            if ((res = gt911_reset(th)) != ESP_OK)
            {
                log_e("GT911 reset failed");
                gt911_del(th);
                return res;
            }

            // Read type and resolution
            if ((res = gt911_read_info(th)) != ESP_OK)
            {
                log_e("GT911 read info failed");
                gt911_del(th);
                return res;
            }
        }

        *handle = th;

        return ESP_OK;
    }

#ifdef __cplusplus
}
#endif

#endif // TOUCH_GT911_I2C