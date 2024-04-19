#include <cstdint>
#include <cstring>
#include <cstdbool>
#include <cstdio>
#include <cinttypes>
#include <ctime>

#include <sys/time.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "driver/i2c.h"
#include "esp32-hal-periman.h"
#include "flutter_remote_display.h"

#include <Arduino.h>
#include <axp20x.h>
#include <TFT_eSPI.h>
#include <focaltech_touch.h>

const char *spp_log_tag = "Flutterino Demo";
const char *bluetooth_spp_name = "Flutterino SPP";
const char *bluetooth_name = "Flutterino";

static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB;
static const bool esp_spp_enable_l2cap_ertm = true;

static struct timeval time_new, time_old;
static long data_num = 0;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

__attribute__((unused)) static void print_speed(void)
{
    float time_old_s = time_old.tv_sec + time_old.tv_usec / 1000000.0;
    float time_new_s = time_new.tv_sec + time_new.tv_usec / 1000000.0;
    float time_interval = time_new_s - time_old_s;
    float speed = data_num * 8 / time_interval / 1000.0;
    ESP_LOGI(spp_log_tag, "speed(%fs ~ %fs): %f kbit/s" , time_old_s, time_new_s, speed);
    data_num = 0;
    time_old.tv_sec = time_new.tv_sec;
    time_old.tv_usec = time_new.tv_usec;
}

TFT_eSPI tft = TFT_eSPI(240, 240);
AXP20X_Class axp = AXP20X_Class();
struct flrd flrd;

FocalTech_Class touchscreen;

struct btspp_connection {
    uint32_t conn_handle;
} flrd_btspp_connection;

static void on_flrd_btspp_send_bytes(void *context, size_t n_bytes, void *bytes) {
    struct btspp_connection *conn;
    esp_err_t ok;
    
    conn = (struct btspp_connection*) context;

    ok = esp_spp_write(conn->conn_handle, n_bytes, (uint8_t*) bytes);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "esp_spp_write failed: %s", esp_err_to_name(ok));
    }
}

const static struct flrd_btspp_interface btspp_driver = {
    .send_bytes = on_flrd_btspp_send_bytes
};

static void on_spp_event(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(spp_log_tag, "ESP_SPP_INIT_EVT");
            esp_spp_start_srv(sec_mask, role_slave, 0, bluetooth_spp_name);
        } else {
            ESP_LOGE(spp_log_tag, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_OPEN_EVT handle: %" PRIu32, param->open.handle);
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_CLOSE_EVT status:%d handle:%" PRIu32 " close_by_remote:%d", param->close.status,
                 param->close.handle, param->close.async);
        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(spp_log_tag, "ESP_SPP_START_EVT handle:%" PRIu32 " sec_id:%d scn:%d", param->start.handle, param->start.sec_id,
                     param->start.scn);
            esp_bt_dev_set_device_name(bluetooth_name);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        } else {
            ESP_LOGE(spp_log_tag, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_DATA_IND_EVT: {
        int64_t time = esp_timer_get_time();
        
        flrd_add_btspp_bytes(&flrd, param->data_ind.len, param->data_ind.data);

        ESP_LOGI(spp_log_tag, "flrd_add_btspp_bytes took %lldus", esp_timer_get_time() - time);

        break;
    }
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_CONG_EVT");
        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_WRITE_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(
            spp_log_tag, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%" PRIu32 ", rem_bda:[%s]",
            param->srv_open.status,
            param->srv_open.handle,
            bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str))
        );
        gettimeofday(&time_old, NULL);
        flrd_btspp_connection.conn_handle = param->srv_open.handle;
        break;
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_SRV_STOP_EVT");
        break;
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_UNINIT_EVT");
        break;
    default:
        break;
    }
}

static void on_bt_gap_event(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(spp_log_tag, "authentication success: %s bda:[%s]", param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        } else {
            ESP_LOGE(spp_log_tag, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(spp_log_tag, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(spp_log_tag, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(spp_log_tag, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_EXAMPLE_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%"PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(spp_log_tag, "ESP_BT_GAP_MODE_CHG_EVT mode:%d bda:[%s]", param->mode_chg.mode,
                 bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;

    default: {
        ESP_LOGI(spp_log_tag, "event: %d", event);
        break;
    }
    }
    return;
}


bool in_transaction = false;

static void display_set_window(void *context, struct rect window) {
    TFT_eSPI *tft = (TFT_eSPI*) context;

    if (!in_transaction) {
        tft->startWrite();
        in_transaction = true;
    }

    tft->setWindow(window.left, window.top, window.left + window.width - 1, window.top + window.height - 1);
}

static void display_write_pixels(void *context, size_t n_pixels, uint16_t *rgb565_pixels) {
    TFT_eSPI *tft = (TFT_eSPI*) context;

    if (!in_transaction) {
        tft->startWrite();
        in_transaction = true;
    }

    tft->pushPixels(rgb565_pixels, n_pixels);
}

static void display_write_pixel_run(void *context, size_t n_pixels, uint16_t rgb565) {
    TFT_eSPI *tft = (TFT_eSPI*) context;

    if (!in_transaction) {
        tft->startWrite();
        in_transaction = true;
    }

    tft->pushBlock(rgb565, n_pixels);
}

static void display_present(void *context) {
    TFT_eSPI *tft = (TFT_eSPI*) context;
    
    if (in_transaction) {
        tft->endWrite();
        in_transaction = false;
    }
}

const static struct flrd_display_driver display_driver = {
    .set_window = display_set_window,
    .write_pixels = display_write_pixels,
    .write_pixel_run = display_write_pixel_run,
    .present = display_present
};


// This task is responsible for committing frames produced by the bluetooth
// SPP task to the TFT display.
static void packet_handler_task(void *arg) {
    struct flrd *flrd = (struct flrd*) arg;
    
    struct timeval last_frame_time;
    int n_frames = 0;

    while (true) {
        struct flrd_packet *packet = flrd_wait_for_packet(flrd);
        if (packet == NULL) {
            ESP_LOGE(spp_log_tag, "flrd_wait_for_packet failed");
            continue;
        }

        switch (packet->type) {
            case FLRD_PACKET_BACKLIGHT:
                ESP_LOGI(spp_log_tag, "received backlight packet. intensity: %d%%", packet->backlight.intensity * 100 / 255);
                digitalWrite(TFT_BL, packet->backlight.intensity != 0 ? TFT_BACKLIGHT_ON : 0 == TFT_BACKLIGHT_ON);
                break;
            case FLRD_PACKET_VIBRATION:
                ESP_LOGI(spp_log_tag, "received vibration packet. duration: %dms (not supported right now)", (int) packet->vibration.duration_millis * 10);
                break;
            case FLRD_PACKET_PING:
                ESP_LOGI(spp_log_tag, "received ping packet");
                flrd_send_pong(flrd);
                break;
            case FLRD_PACKET_FRAME: {
                if (n_frames == 60) {
                    struct timeval now;
                    gettimeofday(&now, NULL);

                    float fps = 60.0 / (now.tv_sec - last_frame_time.tv_sec + (now.tv_usec - last_frame_time.tv_usec) / 1000000.0);
                    ESP_LOGI(spp_log_tag, "fps: %f", fps);

                    last_frame_time = now;
                    n_frames = 0;
                } else if (n_frames == 0) {
                    gettimeofday(&last_frame_time, NULL);
                }

                n_frames++;

                int64_t time = esp_timer_get_time();

                flrd_frame_present(flrd, &packet->frame, &display_driver, &tft);

                ESP_LOGI(spp_log_tag, "flrd_frame_present took %lldus", esp_timer_get_time() - time);

                break;
            }
            default:
                ESP_LOGE(spp_log_tag, "unhandled packet: %d", packet->type);
                break;
        }

        flrd_packet_free(packet);
    }
}

// Init I2C port as Bus Master with SDA pin 21 and SCL pin 22
// and a clock speed of 100kHz
static void i2c_init_master(i2c_port_t port, gpio_num_t sda, gpio_num_t scl) {
    esp_err_t esp_ok;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 100000UL
        },
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL
    };
    
    esp_ok = i2c_param_config(port, &conf);
    if (esp_ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c_param_config failed: %s", esp_err_to_name(esp_ok));
        return;
    }

    esp_ok = i2c_driver_install(
        port,
        I2C_MODE_MASTER,
        0, 0, 0
    );
    if (esp_ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c_driver_install failed: %s", esp_err_to_name(esp_ok));
        return;
    }

    esp_ok = i2c_set_timeout(port, 0xFFFFF);
    if (esp_ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c_set_timeout failed: %s", esp_err_to_name(esp_ok));
        return;
    }
}

static esp_err_t i2c_read_register(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    i2c_cmd_handle_t cmd;
    esp_err_t ok;

    cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        ok = ESP_ERR_NO_MEM;
        goto fail_clear_data;
    }

    ok = i2c_master_start(cmd);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_write_byte(cmd, reg_addr, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_start(cmd);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }
    
    ok = i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_stop(cmd);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_cmd_begin(port, cmd, 50 / portTICK_PERIOD_MS);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c_master_cmd_begin failed: %s", esp_err_to_name(ok));
        goto fail_free_cmd;
    }

    i2c_cmd_link_delete(cmd);
    return ESP_OK;

fail_free_cmd:
    i2c_cmd_link_delete(cmd);

fail_clear_data:
    memset(data, 0, len);
    return ok;
}

static esp_err_t i2c_write_register(i2c_port_t port, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    i2c_cmd_handle_t cmd;
    esp_err_t ok;

    cmd = i2c_cmd_link_create();

    ok = i2c_master_start(cmd);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_write_byte(cmd, reg_addr, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }
    
    ok = i2c_master_write(cmd, data, len, true);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_stop(cmd);
    if (ok != ESP_OK) {
        goto fail_free_cmd;
    }

    ok = i2c_master_cmd_begin(port, cmd, 50 / portTICK_PERIOD_MS);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c_master_cmd_begin failed: %s", esp_err_to_name(ok));
        goto fail_free_cmd;
    }

    i2c_cmd_link_delete(cmd);
    return ESP_OK;

fail_free_cmd:
    i2c_cmd_link_delete(cmd);
    return ok;
}

static esp_err_t i2c0_read_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    return i2c_read_register(I2C_NUM_0, dev_addr, reg_addr, data, len);
}

static esp_err_t i2c0_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    return i2c_write_register(I2C_NUM_0, dev_addr, reg_addr, data, len);
}

static uint8_t i2c1_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    esp_err_t ok;
    
    ok = i2c_read_register(I2C_NUM_1, dev_addr, reg_addr, data, len);
    if (ok != ESP_OK) {
        return 0;
    }

    return 1;
}

static uint8_t i2c1_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
    esp_err_t ok;
    
    ok = i2c_write_register(I2C_NUM_1, dev_addr, reg_addr, data, len);
    if (ok != ESP_OK) {
        return 0;
    }

    return 1;
}

// T-Watch stuff. AXP202 is the power management IC on the T-Watch
static bool axp_probe() {
    uint8_t output_reg;
    uint8_t chip_id;
    esp_err_t ok;

    ok = i2c0_read_register(AXP202_SLAVE_ADDRESS, AXP202_IC_TYPE, &chip_id, 1);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c0_read_register failed: %s", esp_err_to_name(ok));
        return false;
    }

    if (chip_id != AXP202_CHIP_ID) {
        ESP_LOGE(spp_log_tag, "Unsupport AXP IC type: %02x", chip_id);
        return false;
    }

    ok = i2c0_read_register(AXP202_SLAVE_ADDRESS, AXP202_LDO234_DC23_CTL, &output_reg, 1);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c0_read_register failed: %s", esp_err_to_name(ok));
        return false;
    }

    ESP_LOGD(spp_log_tag, "AXP202_LDO234_DC23_CTL: 0x%x", output_reg);
    return true;
}

// Sets a specific power management domain on the AXP202 to be enabled or disabled
// The T-Watch uses channel LDO2 to power the TFT display backlight
static bool axp_set_channel(uint8_t channel, bool enabled) {
    uint8_t data;
    esp_err_t ok;
    
    ok = i2c0_read_register(AXP202_SLAVE_ADDRESS, AXP202_LDO234_DC23_CTL, &data, 1);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c0_read_register failed: %s", esp_err_to_name(ok));
        return false;
    }

    ESP_LOGD(spp_log_tag, "Current AXP202_LDO234_DC23_CTL: 0x%x", data);

    if (enabled) {
        data |= (AXP202_ON << channel);
    } else {
        data &= (~(AXP202_ON << channel));
    }
    
    // AXP202_DCDC3 must be on in T-Watch
    data |= AXP202_ON << AXP202_DCDC3;

    ESP_LOGD(spp_log_tag, "Setting AXP202_LDO234_DC23_CTL: 0x%x", data);

    ok = i2c0_write_register(AXP202_SLAVE_ADDRESS, AXP202_LDO234_DC23_CTL, &data, 1);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c0_write_register failed: %s", esp_err_to_name(ok));
        return false;
    }

    uint8_t val;

    ok = i2c0_read_register(AXP202_SLAVE_ADDRESS, AXP202_LDO234_DC23_CTL, &val, 1);
    if (ok != ESP_OK) {
        ESP_LOGE(spp_log_tag, "i2c0_read_register failed: %s", esp_err_to_name(ok));
        return false;
    }

    if (data != val) {
        ESP_LOGE(spp_log_tag, "AXP202_LDO234_DC23_CTL not set correctly: 0x%x != 0x%x", data, val);
        return false;
    }

    return true;
}

struct touch_task_arg {
    struct flrd *flrd;
    FocalTech_Class *touchscreen;
};

static void touch_task(void *arg_void) {
    struct touch_task_arg *arg = (struct touch_task_arg*) arg_void;

    uint16_t last_x, last_y;
    bool last_pressed = false;

    while (true) {
        // delay for 5ms
        vTaskDelay(16 / portTICK_PERIOD_MS);
        
        struct flrd_touch_event_packet touch_event;
        uint16_t x, y;
        bool pressed;
        
        pressed = arg->touchscreen->getPoint(x, y);

        if (pressed && (!last_pressed || x != last_x || y != last_y)) {
            ESP_LOGI(spp_log_tag, "touch %s, x=%d y=%d", last_pressed ? "move" : "down", x, y);

            touch_event.pointer = 0;
            touch_event.timestamp = 0;
            touch_event.phase = last_pressed ? FLRD_TOUCH_EVENT_PHASE_MOVE : FLRD_TOUCH_EVENT_PHASE_DOWN;
            touch_event.x = TFT_WIDTH - x;
            touch_event.y = TFT_HEIGHT - y;
            flrd_send_touch_event(arg->flrd, &touch_event);

        } else if (last_pressed && !pressed) {
            ESP_LOGI(spp_log_tag, "touch up");

            touch_event.pointer = 0;
            touch_event.timestamp = 0;
            touch_event.phase = FLRD_TOUCH_EVENT_PHASE_UP;
            touch_event.x = x;
            touch_event.y = y;
            flrd_send_touch_event(arg->flrd, &touch_event);
        }

        last_x = x;
        last_y = y;
        last_pressed = pressed;
    }

    free(arg);
}

extern "C" void app_main(void) {
    bool ok;

    // Initialize the I2C0 on pins 21, 22 for the AXP202 power management IC
    i2c_init_master(I2C_NUM_0, GPIO_NUM_21, GPIO_NUM_22);

    // Initialize the I2C1 on pins 23, 32 for the touchscreen
    i2c_init_master(I2C_NUM_1, GPIO_NUM_23, GPIO_NUM_32);
    
    // Initialize the AXP202 power management IC
    ok = axp_probe();
    if (!ok) {
        ESP_LOGE(spp_log_tag, "AXP20x probe failed");
    }

    // On TTGO T-Watch, LDO2 powers the TFT backlight.
    // Se we need to enable this to turn on the display
    ok = axp_set_channel(AXP202_LDO2, true);
    if (!ok) {
        ESP_LOGE(spp_log_tag, "AXP20x enable LD02 failed");
    }

    // Initialize the TFT display, also make it able to use DMA for faster transfers
    tft.init();
    tft.initDMA();

    // Clear the screen to white
    tft.fillScreen(0xFFFF);

    bool hasTouch = touchscreen.begin(i2c1_read, i2c1_write);
    if (!hasTouch) {
        ESP_LOGE(spp_log_tag, "Could not initialize FT5206 touchscreen controller.");
    }

    esp_bt_controller_status_t status = esp_bt_controller_get_status();
    ESP_LOGI(spp_log_tag, "bt controller status: %s", status == ESP_BT_CONTROLLER_STATUS_IDLE ? "idle" :
        status == ESP_BT_CONTROLLER_STATUS_INITED ? "inited" :
        status == ESP_BT_CONTROLLER_STATUS_ENABLED ? "enabled" : "?"
    );

    char bda_str[18] = {0};
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_gap_register_callback(on_bt_gap_event);
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s gap register failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_spp_register_callback(on_spp_event);
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s spp register failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    const esp_spp_cfg_t spp_cfg = {
        .mode = ESP_SPP_MODE_CB,
        .enable_l2cap_ertm = true,
        .tx_buffer_size = ESP_SPP_MAX_TX_BUFFER_SIZE,
    };

    ret = esp_spp_enhanced_init(&spp_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(spp_log_tag, "%s spp init failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    ESP_LOGI(spp_log_tag, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));

    flrd_init(&flrd, TFT_WIDTH, TFT_HEIGHT, &btspp_driver, &flrd_btspp_connection);

    xTaskCreate(packet_handler_task, "packet_handler", 4096, &flrd, 5, NULL);

    if (hasTouch) {
        struct touch_task_arg *arg = (struct touch_task_arg*) malloc(sizeof(struct touch_task_arg));

        arg->flrd = &flrd;
        arg->touchscreen = &touchscreen;

        xTaskCreate(touch_task, "touch_task", 4096, arg, 5, NULL);
    }
}
