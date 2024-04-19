#ifndef _FLUTTER_REMOTE_DISPLAY_H
#define _FLUTTER_REMOTE_DISPLAY_H

#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

struct flrd_btspp_interface {
    void (*send_bytes)(void *context, size_t n_bytes, void *bytes);
};

struct rect {
    int left, top, width, height;
};

struct flrd {
    StaticQueue_t packet_handler_queue_buffer;
    uint8_t packet_handler_queue_storage[sizeof(void*) * 32];
    QueueHandle_t packet_handler_queue;

    StaticQueue_t btspp_buffer_queue_buffer;
    uint8_t btspp_buffer_queue_storage[sizeof(void*) * 32];
    QueueHandle_t btspp_buffer_queue;

    int width, height;

    TaskHandle_t packet_builder_task;

    StaticSemaphore_t btspp_byte_data_consumed_buffer;
    SemaphoreHandle_t btspp_byte_data_consumed;

    struct flrd_btspp_interface btspp_driver;
    void *btspp_driver_context;
};

enum flrd_frame_encoding {
    FLRD_FRAME_ENCODING_KEYFRAME_RAW = 0,
    FLRD_FRAME_ENCODING_KEYFRAME_RLE = 1,
    FLRD_FRAME_ENCODING_DELTAFRAME_RAW = 2,
    FLRD_FRAME_ENCODING_DELTAFRAME_RLE = 3,
};

static inline const char *flrd_frame_encoding_to_string(enum flrd_frame_encoding encoding) {
    switch (encoding) {
        case FLRD_FRAME_ENCODING_KEYFRAME_RAW:
            return "FLRD_FRAME_ENCODING_KEYFRAME_RAW";
        case FLRD_FRAME_ENCODING_KEYFRAME_RLE:
            return "FLRD_FRAME_ENCODING_KEYFRAME_RLE";
        case FLRD_FRAME_ENCODING_DELTAFRAME_RAW:
            return "FLRD_FRAME_ENCODING_DELTAFRAME_RAW";
        case FLRD_FRAME_ENCODING_DELTAFRAME_RLE:
            return "FLRD_FRAME_ENCODING_DELTAFRAME_RLE";
        default:
            return "?";
    }
} 

struct flrd_rle_run {
    size_t n_pixels;
    uint16_t rgb565;
};

struct flrd_rle_runs {
    size_t n_runs;
    struct flrd_rle_run *runs;
};

struct flrd_frame_damaged_rect {
    uint8_t x, y, width, height;
    union {
        struct {
            uint16_t *rgb565_pixels;
        } raw;
        struct flrd_rle_runs rle;
    };
};

struct flrd_frame {
    enum flrd_frame_encoding encoding;
    union {
        struct {
            struct {
                uint16_t *rgb565_pixels;
            } raw;
            struct flrd_rle_runs rle;
        } keyframe;
        struct {
            size_t n_rects;
            struct flrd_frame_damaged_rect *rects;
        } deltaframe;
    };
};

enum flrd_packet_type {
    FLRD_PACKET_QUERY_DEVICE_INFO,
    FLRD_PACKET_DEVICE_INFO,
    FLRD_PACKET_TOUCH_EVENT,
    FLRD_PACKET_ACCELERATION_EVENT,
    FLRD_PACKET_PHYSICAL_BUTTON_EVENT,
    FLRD_PACKET_BACKLIGHT,
    FLRD_PACKET_VIBRATION,
    FLRD_PACKET_PING,
    FLRD_PACKET_PONG,
    FLRD_PACKET_FRAME
};

static inline const char *flrd_packet_type_to_string(enum flrd_packet_type type) {
    switch (type) {
        case FLRD_PACKET_QUERY_DEVICE_INFO:
            return "FLRD_PACKET_QUERY_DEVICE_INFO";
        case FLRD_PACKET_DEVICE_INFO:
            return "FLRD_PACKET_DEVICE_INFO";
        case FLRD_PACKET_TOUCH_EVENT:
            return "FLRD_PACKET_TOUCH_EVENT";
        case FLRD_PACKET_ACCELERATION_EVENT:
            return "FLRD_PACKET_ACCELERATION_EVENT";
        case FLRD_PACKET_PHYSICAL_BUTTON_EVENT:
            return "FLRD_PACKET_PHYSICAL_BUTTON_EVENT";
        case FLRD_PACKET_BACKLIGHT:
            return "FLRD_PACKET_BACKLIGHT";
        case FLRD_PACKET_VIBRATION:
            return "FLRD_PACKET_VIBRATION";
        case FLRD_PACKET_PING:
            return "FLRD_PACKET_PING";
        case FLRD_PACKET_PONG:
            return "FLRD_PACKET_PONG";
        case FLRD_PACKET_FRAME:
            return "FLRD_PACKET_FRAME";
        default:
            return "?";
    }
}

enum flrd_touch_event_phase {
    FLRD_TOUCH_EVENT_PHASE_DOWN,
    FLRD_TOUCH_EVENT_PHASE_MOVE,
    FLRD_TOUCH_EVENT_PHASE_UP
};

struct flrd_touch_event_packet {
    uint8_t pointer;
    uint32_t timestamp;
    uint8_t phase;
    uint8_t x, y;
};

struct flrd_device_info_packet {
    int width, height;
    int width_mm, height_mm;
    bool supports_vibration;
    bool supports_backlight;
    bool supports_touch;
    bool supports_accelerometer;
};

enum flrd_acceleration_event_kind {
    FLRD_ACCELERATION_EVENT_KIND_STEP,
    FLRD_ACCELERATION_EVENT_KIND_WAKE
};

struct flrd_acceleration_event_packet {
    uint8_t kind;
};

struct flrd_physical_button_event_packet {
    uint8_t button;
};

struct flrd_backlight_packet {
    uint8_t intensity;
};

struct flrd_vibration_packet {
    uint8_t duration_millis;
};

struct flrd_packet {
    enum flrd_packet_type type;
    union {
        struct flrd_device_info_packet device_info;
        struct flrd_touch_event_packet touch_event;
        struct flrd_acceleration_event_packet acceleration_event;
        struct flrd_physical_button_event_packet physical_button_event;
        struct flrd_backlight_packet backlight;
        struct flrd_vibration_packet vibration;
        struct flrd_frame frame;
    };
};

int flrd_init(struct flrd *flrd, int width, int height, const struct flrd_btspp_interface *btspp_driver, void *btspp_driver_context);

void flrd_deinit(struct flrd *flrd);

int flrd_add_btspp_bytes(struct flrd *instance, size_t n_bytes, void *bytes);

struct flrd_packet *flrd_wait_for_packet(struct flrd *instance);

void flrd_packet_free(struct flrd_packet *packet);

int flrd_send_pong(struct flrd *flrd);

int flrd_send_touch_event(struct flrd *flrd, struct flrd_touch_event_packet *event);

struct flrd_display_driver {
    void (*set_window)(void *context, struct rect window);
    void (*write_pixels)(void *context, size_t n_pixels, uint16_t *rgb565_pixels);
    void (*write_pixel_run)(void *context, size_t n_pixels, uint16_t rgb565);
    void (*present)(void *context);
};

int flrd_frame_present(struct flrd *flrd, struct flrd_frame *frame, const struct flrd_display_driver *driver, void *driver_context);

#ifdef __cplusplus
}
#endif

#endif
