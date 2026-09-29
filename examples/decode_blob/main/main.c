#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "tuz_dec.h"

static const char *TAG = "decode_blob";

/* Reject streams whose dictionary would not fit the memory budget. */
#define MAX_DICT_SIZE (4 * 1024)
/* Input cache: only affects speed, any size > 0 works. */
#define CACHE_SIZE 64
/* Output chunk: the consumer processes this much at a time. */
#define OUT_CHUNK 48

extern const uint8_t message_tuz_start[] asm("_binary_message_txt_tuz_start");
extern const uint8_t message_tuz_end[] asm("_binary_message_txt_tuz_end");
extern const uint8_t message_start[] asm("_binary_message_txt_start");
extern const uint8_t message_end[] asm("_binary_message_txt_end");

/* Stands in for a transport. tinyuz's read contract: fill *size bytes, returning
 * fewer only at the end of the input (a short read means end of stream). */
typedef struct {
    const uint8_t *pos;
    const uint8_t *end;
} blob_reader_t;

static tuz_BOOL blob_read(tuz_TInputStreamHandle handle, tuz_byte *out, tuz_size_t *size)
{
    blob_reader_t *r = (blob_reader_t *)handle;
    size_t n = *size;
    if (n > (size_t)(r->end - r->pos)) {
        n = (size_t)(r->end - r->pos);
    }
    memcpy(out, r->pos, n);
    r->pos += n;
    *size = (tuz_size_t)n;
    return tuz_TRUE; /* return tuz_FALSE on a transport error */
}

void app_main(void)
{
    blob_reader_t reader = {.pos = message_tuz_start, .end = message_tuz_end};
    const size_t expected_size = (size_t)(message_end - message_start);

    tuz_size_t dict_size = tuz_TStream_read_dict_size(&reader, blob_read);
    if (dict_size == 0 || dict_size > MAX_DICT_SIZE) {
        ESP_LOGE(TAG, "unsupported dictionary size %u", (unsigned)dict_size);
        return;
    }
    ESP_LOGI(TAG, "%u compressed bytes, dictionary %u bytes, decoder RAM %u bytes",
             (unsigned)(message_tuz_end - message_tuz_start), (unsigned)dict_size,
             (unsigned)(sizeof(tuz_TStream) + dict_size + CACHE_SIZE + OUT_CHUNK));

    uint8_t *dict_and_cache = malloc(dict_size + CACHE_SIZE);
    if (!dict_and_cache) {
        ESP_LOGE(TAG, "out of memory");
        return;
    }

    tuz_TStream stream;
    tuz_TResult res = tuz_TStream_open(&stream, &reader, blob_read, dict_and_cache, dict_size, CACHE_SIZE);
    uint8_t out[OUT_CHUNK];
    size_t total = 0;
    bool match = true;
    while (res == tuz_OK) {
        tuz_size_t n = sizeof(out);
        res = tuz_TStream_decompress_partial(&stream, out, &n);
        if (res != tuz_OK && res != tuz_STREAM_END) {
            break;
        }
        /* n is the full chunk on tuz_OK and the remaining bytes on tuz_STREAM_END. */
        match = match && total + n <= expected_size && memcmp(out, message_start + total, n) == 0;
        total += n;
        fwrite(out, 1, n, stdout);
    }
    free(dict_and_cache);

    if (res != tuz_STREAM_END) {
        ESP_LOGE(TAG, "decode failed: tuz_TResult %d", (int)res);
    } else if (!match || total != expected_size) {
        ESP_LOGE(TAG, "decoded %u bytes that do not match the original", (unsigned)total);
    } else {
        ESP_LOGI(TAG, "decoded %u bytes, matches the original", (unsigned)total);
    }
}
