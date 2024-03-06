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

#include <Arduino.h>
#include <axp20x.h>

#include <TFT_eSPI.h>

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

static void print_speed(void)
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

struct pixelbuffer {
    uint8_t left, top, width, height;
    bool is_dma;

    uint8_t __attribute__ ((aligned (4))) data[];
};

#define FRAME_MAX_N_BUFFERS 8

struct frame {
    uint8_t left, top, width, height;

    size_t n_buffers;
    struct pixelbuffer *buffers[FRAME_MAX_N_BUFFERS];
};

static size_t sizeof_pixelbuffer(uint8_t width, uint8_t height) {
    return sizeof(struct pixelbuffer) + width * height * 2;
} 

static struct pixelbuffer *pixelbuffer_new(uint8_t left, uint8_t top, uint8_t width, uint8_t height) {
    struct pixelbuffer *frame = (struct pixelbuffer*) heap_caps_malloc(sizeof_pixelbuffer(width, height), MALLOC_CAP_DMA);
    if (frame == NULL) {
        frame = (struct pixelbuffer*) heap_caps_malloc(sizeof_pixelbuffer(width, height), MALLOC_CAP_DEFAULT);
        if (frame == NULL) {
            return NULL;
        }

        frame->is_dma = false;
    } else {
        frame->is_dma = true;
    }

    frame->left = left;
    frame->top = top;
    frame->width = width;
    frame->height = height;

    return frame;
}

static void pixelbuffer_destroy(struct pixelbuffer *buffer) {
    heap_caps_free(buffer);
}

static size_t pixelbuffer_size(struct pixelbuffer *buffer) {
    return buffer->width * buffer->height * 2;
}

static struct frame *frame_new(uint8_t left, uint8_t top, uint8_t width, uint8_t height) {
    struct frame *frame = (struct frame*) heap_caps_malloc(sizeof(struct frame), MALLOC_CAP_DEFAULT);
    if (frame == NULL) {
        return NULL;
    }

    frame->left = left;
    frame->top = top;
    frame->width = width;
    frame->height = height;
    frame->n_buffers = 0;

    for (size_t n_buffers = 1; n_buffers <= FRAME_MAX_N_BUFFERS; n_buffers++) {
        struct pixelbuffer *buffers[n_buffers];

        if (height % n_buffers != 0) {
            continue;
        }

        int buffer_width = width;
        int buffer_height = height / n_buffers;

        for (int i = 0; i < n_buffers; i++) {
            struct pixelbuffer *buffer = pixelbuffer_new(left, top + i * buffer_height, buffer_width, buffer_height);
            if (buffer == NULL) {
                goto fail_free_buffers;
            }

            buffers[i] = buffer;
            continue;


            fail_free_buffers:
            for (int j = 0; j < i; j++) {
                pixelbuffer_destroy(buffers[j]);
            }

            goto fail_continue;
        }

        frame->n_buffers = n_buffers;
        memcpy(frame->buffers, buffers, sizeof(buffers));
        goto success;


        fail_continue:
        continue;
    }

    heap_caps_free(frame);
    return NULL;


    success:
    return frame;
}

static void frame_destroy(struct frame *frame) {
    for (size_t i = 0; i < frame->n_buffers; i++) {
        pixelbuffer_destroy(frame->buffers[i]);
    }

    heap_caps_free(frame);
}

static void frame_write_pixels(struct frame *frame, size_t offset, void *source, size_t length) {
    size_t buffer_index = 0;
    size_t buffer_offset = offset;

    while (buffer_offset >= pixelbuffer_size(frame->buffers[buffer_index])) {
        buffer_offset -= pixelbuffer_size(frame->buffers[buffer_index]);
        buffer_index++;
    }

    while (length > 0) {
        size_t to_copy = pixelbuffer_size(frame->buffers[buffer_index]) - buffer_offset;
        if (to_copy > length) {
            to_copy = length;
        }

        memcpy((uint8_t*) (frame->buffers[buffer_index]->data) + buffer_offset, source, to_copy);

        source += to_copy;
        length -= to_copy;
        buffer_offset += to_copy;

        if (buffer_offset == pixelbuffer_size(frame->buffers[buffer_index])) {
            buffer_index++;
            buffer_offset = 0;
        }
    }
}

static size_t frame_size(struct frame *frame) {
    return frame->width * frame->height * 2;
}

static StaticQueue_t frame_queue_buffer;
static uint8_t frame_queue_storage[ 2 * sizeof(void*)];
static QueueHandle_t frame_queue;

static void on_spp_event(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
    static struct frame *frame = NULL;
    static int left, top, width, height;
    static int frame_bytes_remaining = 0, pixel_offset = 0, frame_offset = 0;
    static bool discarding = false;

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
        ESP_LOGI(spp_log_tag, "ESP_SPP_OPEN_EVT");
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
    case ESP_SPP_DATA_IND_EVT:
        for (int i = 0; i < param->data_ind.len;) {
            if (discarding) {
                int to_discard = param->data_ind.len - i;
                if (to_discard > frame_bytes_remaining) {
                    to_discard = frame_bytes_remaining;
                }

                i += to_discard;
                frame_bytes_remaining -= to_discard;

                if (frame_bytes_remaining == 0) {
                    discarding = false;
                }
            } else if (frame == NULL && frame_offset == 0) {
                left = param->data_ind.data[i++];
                frame_offset += 1;
            } else if (frame == NULL && frame_offset == 1) {
                top = param->data_ind.data[i++];
                frame_offset += 1;
            } else if (frame == NULL && frame_offset == 2) {
                width = param->data_ind.data[i++];
                frame_offset += 1;
            } else if (frame == NULL && frame_offset == 3) {
                height = param->data_ind.data[i++];
                frame_offset += 1;
                pixel_offset = 0;
                frame_bytes_remaining = width * height * 2;

                frame = frame_new(left, top, width, height);
                if (frame == NULL) {
                    ESP_LOGW(spp_log_tag, "Allocating frame failed. Dropping frame.");
                    discarding = true;
                    continue;
                }
            } else if (frame != NULL) {
                // find the correct frame buffer to write to, by iterating over the buffers and adding their sizes
                int to_copy = param->data_ind.len - i;
                if (frame_bytes_remaining < to_copy) {
                    to_copy = frame_bytes_remaining;
                }
                
                frame_write_pixels(frame, pixel_offset, param->data_ind.data + i, to_copy);

                frame_offset += to_copy;
                pixel_offset += to_copy;
                frame_bytes_remaining -= to_copy;
                i += to_copy;
            }

            if (frame != NULL && frame_bytes_remaining == 0) {
                BaseType_t ok = xQueueSend(frame_queue, &frame, 0);
                if (ok == errQUEUE_FULL) {
                    ESP_LOGW(spp_log_tag, "Frame queue full, dropping frame");
                }

                frame = NULL;
                frame_bytes_remaining = 0;
                pixel_offset = 0;
                frame_offset = 0;
            }
        }

        gettimeofday(&time_new, NULL);
        data_num += param->data_ind.len;
        if (time_new.tv_sec - time_old.tv_sec >= 3) {
            print_speed();
        }

        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_CONG_EVT");
        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_WRITE_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(spp_log_tag, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%" PRIu32 ", rem_bda:[%s]", param->srv_open.status,
                 param->srv_open.handle, bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        gettimeofday(&time_old, NULL);
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

// This task is responsible for committing frames produced by the bluetooth
// SPP task to the TFT display.
static void frame_committer_task(void *pvParameters) {
    struct frame *frame = NULL;

    while (true) {
        BaseType_t result = xQueueReceive(frame_queue, &frame, portMAX_DELAY);
        if (result != pdTRUE) {
            continue;
        }

        for (size_t i = 0; i < frame->n_buffers; i++) {
            struct pixelbuffer *buffer = frame->buffers[i];

            ESP_LOGD(spp_log_tag, "Committing buffer to TFT. size: %d bytes, dma: %s", pixelbuffer_size(buffer), buffer->is_dma ? "yes" : "no");

            tft.startWrite();
            tft.setWindow(buffer->left, buffer->top, buffer->left + buffer->width - 1, buffer->top + buffer->height - 1);
            if (buffer->is_dma) {
                tft.pushPixelsDMA((uint16_t*) buffer->data, pixelbuffer_size(buffer) / 2);
            } else {
                tft.pushPixels((uint16_t*) buffer->data, pixelbuffer_size(buffer) / 2);
            }
            tft.endWrite();
        }

        frame_destroy(frame);
    }
}

// Init I2C port as Bus Master with SDA pin 21 and SCL pin 22
// and a clock speed of 100kHz
static void i2c_init_master(i2c_port_t port) {
    esp_err_t esp_ok;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA,
        .scl_io_num = SCL,
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

static esp_err_t i2c0_read_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
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

    ok = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50 / portTICK_PERIOD_MS);
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

static esp_err_t i2c0_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len) {
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

    ok = i2c_master_cmd_begin(I2C_NUM_0, cmd, 50 / portTICK_PERIOD_MS);
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

// T-Watch stuff. AXP202 is the power management IC on the T-Watch
static bool axp_probe() {
    uint8_t output_reg;
    uint8_t chip_id;
    uint8_t data;
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

extern "C" void app_main(void) {
    bool ok;
    frame_queue = xQueueCreateStatic(2, sizeof(void*), frame_queue_storage, &frame_queue_buffer);

    // Initialize the I2C bus (Needed to communicate with the AXP)
    i2c_init_master(I2C_NUM_0);
    
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

    // ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    
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

    ret = esp_spp_init(esp_spp_mode);
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

    xTaskCreate(frame_committer_task, "frame_committer", 4096, NULL, 5, NULL);
}
