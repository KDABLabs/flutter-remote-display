#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "flutter_remote_display.h"

#include <esp_log.h>
#include "esp_timer.h"

struct byte_data {
    int64_t timestamp;
    
    size_t n_bytes;
    uint8_t bytes[];
};

struct byte_reader {
    QueueHandle_t queue;
    struct byte_data *data;

    size_t offset;
};

size_t sizeof_byte_data(size_t n_bytes) {
    return sizeof(struct byte_data) + n_bytes;
}

int flrd_init(struct flrd *flrd, int width, int height, const struct flrd_btspp_interface *btspp_driver, void *btspp_driver_context) {
    memset(flrd, 0, sizeof(struct flrd));

    flrd->packet_handler_queue = xQueueCreateStatic(
        sizeof(flrd->packet_handler_queue_storage) / sizeof(void*),
        sizeof(void*),
        flrd->packet_handler_queue_storage,
        &flrd->packet_handler_queue_buffer
    );

    flrd->btspp_buffer_queue = xQueueCreateStatic(
        sizeof(flrd->btspp_buffer_queue_storage) / sizeof(void*),
        sizeof(void*),
        flrd->btspp_buffer_queue_storage,
        &flrd->btspp_buffer_queue_buffer
    );

    flrd->width = width;
    flrd->height = height;

    flrd->btspp_byte_data_consumed = xSemaphoreCreateBinaryStatic(&flrd->btspp_byte_data_consumed_buffer);
    memcpy(&flrd->btspp_driver, btspp_driver, sizeof(struct flrd_btspp_interface));
    flrd->btspp_driver_context = btspp_driver_context;
    return 0;
}

void flrd_deinit(struct flrd *flrd) {
    vTaskDelete(flrd->packet_builder_task);
    vSemaphoreDelete(flrd->btspp_byte_data_consumed);
    vQueueDelete(flrd->packet_handler_queue);
    vQueueDelete(flrd->btspp_buffer_queue);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

static inline void byte_reader_read_bytes(
    struct byte_reader *reader,
    size_t n_bytes,
    void *bytes_out
) {
    static int64_t last_receive = 0;

    while (n_bytes > 0) {
        static_assert(sizeof(void*) == sizeof(uint32_t));

        // If we don't have data yet, wait for the bluetooth spp task to give us some
        if (reader->data == NULL) {
            BaseType_t ok = xQueueReceive(reader->queue, &reader->data, portMAX_DELAY);
            if (ok != pdPASS) {
                ESP_LOGE("flrd", "Error receiving data from the bluetooth spp task");
                return;
            }

            reader->offset = 0;

            last_receive = esp_timer_get_time();

            ESP_LOGD("flrd", "Scheduling overhead (acquire) %lld us", last_receive - reader->data->timestamp);
        }

        size_t to_copy = min(n_bytes, reader->data->n_bytes - reader->offset);

        // bytes_out can be NULL if we just want to discard the data
        if (bytes_out != NULL) {
            memcpy(bytes_out, reader->data->bytes + reader->offset, to_copy);
        }

        n_bytes -= to_copy;
        reader->offset += to_copy;

        // Signal to the bluetooth spp task that we consumed all it's data
        if (reader->data->n_bytes == reader->offset) {
            free(reader->data);
            reader->data = NULL;

            int64_t now = esp_timer_get_time();
            if (last_receive != 0) {
                ESP_LOGI("flrd", "Processing data took %lld us", now - last_receive);
            }
        }
    }
}

static inline uint8_t byte_reader_read_byte(
    struct byte_reader *reader
) {
    uint8_t result;

    byte_reader_read_bytes(reader, 1, &result);

    return result;
}

static inline uint16_t byte_reader_read_word(
    struct byte_reader *reader
) {
    uint16_t result;

    byte_reader_read_bytes(reader, sizeof(result), &result);

    return result;
}

static bool read_rle_runs(struct byte_reader *reader, struct flrd_rle_runs *runs_out) {
    struct flrd_rle_run *runs;

    size_t n_runs = byte_reader_read_word(reader);

    if (runs_out != NULL) {
        runs = malloc(n_runs * sizeof(struct flrd_rle_run));
    } else {
        runs = NULL;
    }

    if (runs != NULL) {
        for (size_t i = 0; i < n_runs; i++) {
            runs[i].n_pixels = byte_reader_read_byte(reader);
            runs[i].rgb565 = byte_reader_read_word(reader);
        }
    } else {
        byte_reader_read_bytes(reader, n_runs * (sizeof(uint8_t) + sizeof(uint16_t)), NULL);
    }

    runs_out->n_runs = n_runs;
    runs_out->runs = runs;

    return runs != NULL;
}

static struct flrd_packet *read_raw_keyframe_packet(struct flrd *flrd, struct byte_reader *reader) {
    struct flrd_packet *packet;
    uint16_t *rgb565_pixels;
    size_t n_pixels;

    packet = malloc(sizeof(struct flrd_packet));
    if (packet == NULL) {
        ESP_LOGE("flrd", "Out of memory while reading raw keyframe packet. Discarding the rest of the data.");
    }

    n_pixels = flrd->width * flrd->height;

    if (packet != NULL) {
        rgb565_pixels = malloc(n_pixels * sizeof(uint16_t));
        if (rgb565_pixels == NULL) {
            ESP_LOGE("flrd", "Out of memory while reading raw keyframe packet. Discarding the rest of the data.");
            free(packet);
        }
    } else{
        rgb565_pixels = NULL;
    }

    byte_reader_read_bytes(reader, n_pixels * sizeof(uint16_t), rgb565_pixels);

    if (packet != NULL) {
        packet->type = FLRD_PACKET_FRAME;
        packet->frame.encoding = FLRD_FRAME_ENCODING_KEYFRAME_RAW;
        packet->frame.keyframe.raw.rgb565_pixels = rgb565_pixels;
    }

    return packet;
}

static struct flrd_packet *read_rle_keyframe_packet(struct flrd *flrd, struct byte_reader *reader) {
    struct flrd_packet *packet;
    bool ok;
    
    packet = calloc(1, sizeof(struct flrd_packet));
    if (packet == NULL) {
        ESP_LOGE("flrd", "Out of memory while reading RLE keyframe packet. Discarding the rest of the data.");
    }

    ok = read_rle_runs(reader, packet == NULL ? NULL : &packet->frame.keyframe.rle);
    if (!ok) {
        if (packet != NULL) {
            free(packet);
            packet = NULL;
        }
    }

    if (packet != NULL) {
        packet->type = FLRD_PACKET_FRAME;
        packet->frame.encoding = FLRD_FRAME_ENCODING_KEYFRAME_RLE;
    }

    return packet;
}

static struct flrd_packet *read_raw_deltaframe_packet(struct flrd *flrd, struct byte_reader *reader) {
    struct flrd_frame_damaged_rect *rects;
    struct flrd_packet *packet;
    size_t n_rects;

    packet = malloc(sizeof(struct flrd_packet));
    if (packet == NULL) {
        ESP_LOGE("flrd", "Out of memory while reading raw deltaframe packet. Discarding the rest of the data.");
    }

    n_rects = byte_reader_read_byte(reader);

    if (packet != NULL) {
        rects = malloc(n_rects * sizeof(struct flrd_frame_damaged_rect));
        if (rects == NULL) {
            ESP_LOGE("flrd", "Out of memory while reading raw deltaframe packet. Discarding the rest of the data.");
            free(packet);
        }
    } else {
        rects = NULL;
    }

    for (size_t i = 0; i < n_rects; i++) {
        uint8_t x = byte_reader_read_byte(reader);
        uint8_t y = byte_reader_read_byte(reader);
        uint8_t width = byte_reader_read_byte(reader);
        uint8_t height = byte_reader_read_byte(reader);

        size_t n_pixels = width * height;

        uint16_t *rgb565_pixels;
        
        if (rects != NULL) {
            rgb565_pixels = malloc(n_pixels * sizeof(uint16_t));
            if (rgb565_pixels == NULL) {
                ESP_LOGE("flrd", "Out of memory while reading raw deltaframe packet. Discarding the rest of the data.");
                for (size_t j = 0; j < i; j++) {
                    free(rects[j].raw.rgb565_pixels);
                }
                free(rects);
                free(packet);
                rects = NULL;
                packet = NULL;
                break;
            }
        } else {
            rgb565_pixels = NULL;
        }

        byte_reader_read_bytes(reader, n_pixels * sizeof(uint16_t), rgb565_pixels);

        if (rects != NULL) {
            rects[i].x = x;
            rects[i].y = y;
            rects[i].width = width;
            rects[i].height = height;
            rects[i].raw.rgb565_pixels = rgb565_pixels;
        }
    }

    if (packet != NULL) {
        packet->type = FLRD_PACKET_FRAME;
        packet->frame.encoding = FLRD_FRAME_ENCODING_DELTAFRAME_RAW;
        packet->frame.deltaframe.n_rects = n_rects;
        packet->frame.deltaframe.rects = rects;
    }

    return packet;
}

static struct flrd_packet *read_rle_deltaframe_packet(struct flrd *flrd, struct byte_reader *reader) {
    struct flrd_frame_damaged_rect *rects;
    struct flrd_packet *packet;
    size_t n_rects;

    packet = malloc(sizeof *packet);
    if (packet == NULL) {
        ESP_LOGE("flrd", "Out of memory while reading RLE deltaframe packet. Discarding the rest of the data.");
    }

    n_rects = byte_reader_read_word(reader);

    if (packet != NULL) {
        rects = malloc(n_rects * sizeof *rects);
        if (rects == NULL) {
            ESP_LOGE("flrd", "Out of memory while reading RLE deltaframe packet. Discarding the rest of the data.");
            free(packet);
            packet = NULL;
        }
    } else {
        rects = NULL;
    }

    for (size_t i = 0; i < n_rects; i++) {
        uint8_t x = byte_reader_read_byte(reader);
        uint8_t y = byte_reader_read_byte(reader);
        uint8_t width = byte_reader_read_byte(reader);
        uint8_t height = byte_reader_read_byte(reader);

        if (rects != NULL) {
            rects[i].x = x;
            rects[i].y = y;
            rects[i].width = width;
            rects[i].height = height;
        }

        bool ok = read_rle_runs(reader, rects == NULL ? NULL : &rects[i].rle);
        if (!ok) {
            if (rects != NULL) {
                for (size_t j = 0; j < i; j++) {
                    free(rects[j].rle.runs);
                }
                free(rects);
                rects = NULL;
            }
            if (packet != NULL) {
                free(packet);
                packet = NULL;
            }
            break;
        }
    }

    if (packet != NULL) {
        packet->type = FLRD_PACKET_FRAME;
        packet->frame.encoding = FLRD_FRAME_ENCODING_DELTAFRAME_RLE;
        packet->frame.deltaframe.n_rects = n_rects;
        packet->frame.deltaframe.rects = rects;
    }

    return packet;
}

static struct flrd_packet *read_backlight_packet(struct byte_reader *reader) {
    struct flrd_packet *packet;
    uint8_t intensity;

    packet = malloc(sizeof(struct flrd_packet));
    if (packet == NULL) {
        // out of memory. just discard the rest of the data.
    }

    intensity = byte_reader_read_byte(reader);

    if (packet != NULL) {
        packet->type = FLRD_PACKET_BACKLIGHT;
        packet->backlight.intensity = intensity;
    }

    return packet;
}

static struct flrd_packet *read_vibration_packet(struct byte_reader *reader) {
    struct flrd_packet *packet;
    uint8_t duration_millis;

    packet = malloc(sizeof(struct flrd_packet));
    if (packet == NULL) {
        // out of memory. just discard the rest of the data.
    }

    duration_millis = byte_reader_read_byte(reader);

    if (packet != NULL) {
        packet->type = FLRD_PACKET_VIBRATION;
        packet->vibration.duration_millis = duration_millis;
    }

    return packet;

}

static struct flrd_packet *read_ping_packet(struct byte_reader *reader) {
    struct flrd_packet *packet;

    packet = malloc(sizeof(struct flrd_packet));
    if (packet == NULL) {
        // out of memory. just discard the rest of the data.
    }

    if (packet != NULL) {
        packet->type = FLRD_PACKET_PING;
    }

    return packet;
}

static struct flrd_packet *read_frame_packet(struct flrd *flrd, struct byte_reader *reader) {
    enum flrd_frame_encoding encoding = (enum flrd_frame_encoding) byte_reader_read_byte(reader);

    switch (encoding) {
        case FLRD_FRAME_ENCODING_KEYFRAME_RAW: 
            return read_raw_keyframe_packet(flrd, reader);
        case FLRD_FRAME_ENCODING_KEYFRAME_RLE:
            return read_rle_keyframe_packet(flrd, reader);
        case FLRD_FRAME_ENCODING_DELTAFRAME_RAW: 
            return read_raw_deltaframe_packet(flrd, reader);
        case FLRD_FRAME_ENCODING_DELTAFRAME_RLE:
            return read_rle_deltaframe_packet(flrd, reader);
        default:
            return NULL;
    }
}

static struct flrd_packet *read_packet(struct flrd *flrd, struct byte_reader *reader) {
    enum flrd_packet_type packet_type = byte_reader_read_byte(reader);

    switch (packet_type) {
        case FLRD_PACKET_BACKLIGHT:
            return read_backlight_packet(reader);
        case FLRD_PACKET_VIBRATION:
            return read_vibration_packet(reader);
        case FLRD_PACKET_PING:
            return read_ping_packet(reader);
        case FLRD_PACKET_FRAME:
            return read_frame_packet(flrd, reader);
        default:
            return NULL;
    }
}

static void packet_builder_task(void *args) {
    struct flrd *flrd = args;
    struct byte_reader reader = {
        .queue = flrd->btspp_buffer_queue,
        .data = NULL,
        .offset = 0
    };

    while (true) {
        struct flrd_packet *packet = read_packet(flrd, &reader);
        if (packet == NULL) {
            ESP_LOGE("flrd", "Error reading packet. Data was discarded.");
            continue;
        }

        BaseType_t ok = xQueueSend(flrd->packet_handler_queue, &packet, portMAX_DELAY);
        if (ok != pdPASS) {
            // error sending packet to the handler task
            ESP_LOGE("flrd", "Error sending packet to the handler task");
        }
    }
}

int flrd_add_btspp_bytes(struct flrd *flrd, size_t n_bytes, void *bytes) {
    BaseType_t ok;

    if (flrd->packet_builder_task == NULL) {
        ok = xTaskCreate(packet_builder_task, "packet_builder_task", 4096, flrd, 1, &flrd->packet_builder_task);
        if (ok != pdPASS) {
            return 1;
        }
    }

    if (n_bytes == 0) {
        return 0;
    }

    struct byte_data *data = malloc(sizeof_byte_data(n_bytes));
    if (data == NULL) {
        return 1;
    }

    data->timestamp = esp_timer_get_time();
    data->n_bytes = n_bytes;
    memcpy(data->bytes, bytes, n_bytes);

    ok = xQueueSend(flrd->btspp_buffer_queue, &data, portMAX_DELAY);
    if (ok != pdPASS) {
        ESP_LOGE("flrd", "Error byte buffer to the packet builder task");
        free(data);
        return 1;
    }

    return 0;
}

struct flrd_packet *flrd_wait_for_packet(struct flrd *flrd) {
    struct flrd_packet *packet;
    BaseType_t ok;
    
    ok = xQueueReceive(flrd->packet_handler_queue, &packet, portMAX_DELAY);
    if (ok != pdPASS) {
        return NULL;
    }

    return packet;
}

void flrd_packet_free(struct flrd_packet *packet) {
    if (packet->type == FLRD_PACKET_FRAME) {
        if (packet->frame.encoding == FLRD_FRAME_ENCODING_KEYFRAME_RAW) {
            free(packet->frame.keyframe.raw.rgb565_pixels);
        } else if (packet->frame.encoding == FLRD_FRAME_ENCODING_KEYFRAME_RLE) {
            free(packet->frame.keyframe.rle.runs);
        } else if (packet->frame.encoding == FLRD_FRAME_ENCODING_DELTAFRAME_RAW) {
            for (size_t i = 0; i < packet->frame.deltaframe.n_rects; i++) {
                free(packet->frame.deltaframe.rects[i].raw.rgb565_pixels);
            }
            free(packet->frame.deltaframe.rects);
        } else if (packet->frame.encoding == FLRD_FRAME_ENCODING_DELTAFRAME_RLE) {
            for (size_t i = 0; i < packet->frame.deltaframe.n_rects; i++) {
                free(packet->frame.deltaframe.rects[i].rle.runs);
            }
            free(packet->frame.deltaframe.rects);
        }
    }

    free(packet);
}

int flrd_send_pong(struct flrd *flrd) {
    flrd->btspp_driver.send_bytes(
        flrd->btspp_driver_context,
        1,
        (uint8_t[]) {
            FLRD_PACKET_PONG,
        }
    );
    return 0;
}

int flrd_send_touch_event(struct flrd *flrd, struct flrd_touch_event_packet *event) {
    // struct flrd_touch_event_packet {
    //     uint8_t pointer;
    //     uint32_t timestamp;
    //     uint8_t phase;
    //     uint8_t x, y;
    // };

    uint8_t data[] = {
        FLRD_PACKET_TOUCH_EVENT,
        event->pointer,
        event->timestamp & 0xFF,
        (event->timestamp >> 8) & 0xFF,
        (event->timestamp >> 16) & 0xFF,
        (event->timestamp >> 24) & 0xFF,
        event->phase,
        event->x,
        event->y,
    };

    flrd->btspp_driver.send_bytes(flrd->btspp_driver_context, sizeof(data), data);
    return 0;
}

void flrd_frame_destroy(struct flrd_frame *frame) {
    switch (frame->encoding) {
        case FLRD_FRAME_ENCODING_KEYFRAME_RAW:
            free(frame->keyframe.raw.rgb565_pixels);
            break;
        case FLRD_FRAME_ENCODING_KEYFRAME_RLE:
            free(frame->keyframe.rle.runs);
            break;
        case FLRD_FRAME_ENCODING_DELTAFRAME_RAW:
            for (size_t i = 0; i < frame->deltaframe.n_rects; i++) {
                free(frame->deltaframe.rects[i].raw.rgb565_pixels);
            }
            free(frame->deltaframe.rects);
            break;
        case FLRD_FRAME_ENCODING_DELTAFRAME_RLE:
            for (size_t i = 0; i < frame->deltaframe.n_rects; i++) {
                free(frame->deltaframe.rects[i].rle.runs);
            }
            free(frame->deltaframe.rects);
            break;
    }

    free(frame);
}

int flrd_frame_present(struct flrd *flrd, struct flrd_frame *frame, const struct flrd_display_driver *driver, void *driver_context) {
    switch (frame->encoding) {
        case FLRD_FRAME_ENCODING_KEYFRAME_RAW:
            driver->set_window(
                driver_context,
                (struct rect) {
                    .left = 0,
                    .top = 0,
                    .width = flrd->width,
                    .height = flrd->height,
                }
            );
            driver->write_pixels(
                driver_context,
                flrd->width * flrd->height,
                frame->keyframe.raw.rgb565_pixels
            );
            driver->present(driver_context);
            break;

        case FLRD_FRAME_ENCODING_KEYFRAME_RLE:
            driver->set_window(
                driver_context,
                (struct rect) {
                    .left = 0,
                    .top = 0,
                    .width = flrd->width,
                    .height = flrd->height,
                }
            );
            for (int i = 0; i < frame->keyframe.rle.n_runs; i++) {
                driver->write_pixel_run(
                    driver_context,
                    frame->keyframe.rle.runs[i].n_pixels,
                    frame->keyframe.rle.runs[i].rgb565
                );
            }
            driver->present(driver_context);
            break;

        case FLRD_FRAME_ENCODING_DELTAFRAME_RAW:
            for (int i = 0; i < frame->deltaframe.n_rects; i++) {
                driver->set_window(
                    driver_context,
                    (struct rect) {
                        .left = frame->deltaframe.rects[i].x,
                        .top = frame->deltaframe.rects[i].y,
                        .width = frame->deltaframe.rects[i].width,
                        .height = frame->deltaframe.rects[i].height,
                    }
                );
                driver->write_pixels(
                    driver_context,
                    frame->deltaframe.rects[i].width * frame->deltaframe.rects[i].height,
                    frame->deltaframe.rects[i].raw.rgb565_pixels
                );
            }
            driver->present(driver_context);
            break;

        case FLRD_FRAME_ENCODING_DELTAFRAME_RLE:
            for (int i = 0; i < frame->deltaframe.n_rects; i++) {
                driver->set_window(
                    driver_context,
                    (struct rect) {
                        .left = frame->deltaframe.rects[i].x,
                        .top = frame->deltaframe.rects[i].y,
                        .width = frame->deltaframe.rects[i].width,
                        .height = frame->deltaframe.rects[i].height,
                    }
                );
                for (int j = 0; j < frame->deltaframe.rects[i].rle.n_runs; j++) {
                    driver->write_pixel_run(
                        driver_context,
                        frame->deltaframe.rects[i].rle.runs[j].n_pixels,
                        frame->deltaframe.rects[i].rle.runs[j].rgb565
                    );
                }
            }
            driver->present(driver_context);
            break;
    }

    return 0;
}
