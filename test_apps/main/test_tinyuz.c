#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "test_vectors.h"
#include "tuz_dec.h"
#include "unity.h"

/* Largest dictionary the tests accept from a (possibly corrupted) stream header. */
#define MAX_TEST_DICT_SIZE (64 * 1024)
#define GUARD_SIZE 32
#define GUARD_BYTE 0xA5

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} mem_reader_t;

/* tinyuz's read contract: fill *size bytes, returning fewer only at the end of input. */

static tuz_BOOL mem_read(tuz_TInputStreamHandle handle, tuz_byte *out, tuz_size_t *size)
{
    mem_reader_t *r = (mem_reader_t *)handle;
    size_t n = *size;
    if (n > r->size - r->pos) {
        n = r->size - r->pos;
    }
    memcpy(out, r->data + r->pos, n);
    r->pos += n;
    *size = (tuz_size_t)n;
    return tuz_TRUE;
}

static bool vector_supported(const tuz_test_vector_t *v)
{
#if CONFIG_TINYUZ_LITERAL_LINE
    (void)v;
    return true;
#else
    return !v->literal_line;
#endif
}

/* Stream-decode `code` into `out` (capacity out_cap) in chunks of out_chunk bytes.
 * Returns the final tuz_TResult; *out_size is the number of bytes produced. */
static tuz_TResult stream_decode(const uint8_t *code, size_t code_size, size_t cache_size,
                                 size_t out_chunk, uint8_t *out, size_t out_cap, size_t *out_size,
                                 tuz_size_t *dict_size_out)
{
    mem_reader_t reader = {.data = code, .size = code_size, .pos = 0};
    *out_size = 0;

    tuz_size_t dict_size = tuz_TStream_read_dict_size(&reader, mem_read);
    if (dict_size_out) {
        *dict_size_out = dict_size;
    }
    if (dict_size == 0 || dict_size > MAX_TEST_DICT_SIZE) {
        return tuz_READ_DICT_SIZE_ERROR;
    }

    uint8_t *mem = malloc(dict_size + cache_size);
    TEST_ASSERT_NOT_NULL(mem);
    tuz_TStream stream;
    tuz_TResult res = tuz_TStream_open(&stream, &reader, mem_read, mem, dict_size, (tuz_size_t)cache_size);
    while (res == tuz_OK) {
        size_t room = out_cap - *out_size;
        if (room == 0) {
            break; /* output bound reached before the stream ended */
        }
        tuz_size_t n = (tuz_size_t)(out_chunk < room ? out_chunk : room);
        res = tuz_TStream_decompress_partial(&stream, out + *out_size, &n);
        if (res == tuz_OK || res == tuz_STREAM_END) {
            *out_size += n;
        }
    }
    free(mem);
    return res;
}

TEST_CASE("decompress_mem decodes every vector", "[tinyuz]")
{
    for (size_t i = 0; i < tuz_test_vector_count; i++) {
        const tuz_test_vector_t *v = &tuz_test_vectors[i];
        if (!vector_supported(v)) {
            continue;
        }
        uint8_t *out = malloc(v->expected_size + GUARD_SIZE);
        TEST_ASSERT_NOT_NULL(out);
        memset(out, GUARD_BYTE, v->expected_size + GUARD_SIZE);
        tuz_size_t size = (tuz_size_t)v->expected_size;
        TEST_ASSERT_EQUAL_MESSAGE(tuz_STREAM_END, tuz_decompress_mem(v->code, v->code_size, out, &size), v->name);
        TEST_ASSERT_EQUAL_MESSAGE(v->expected_size, size, v->name);
        if (size) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->expected, out, size, v->name);
        }
        for (size_t g = 0; g < GUARD_SIZE; g++) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(GUARD_BYTE, out[v->expected_size + g], v->name);
        }
        free(out);
    }
}

TEST_CASE("stream decode with bounded memory and odd chunk sizes", "[tinyuz]")
{
    /* The cache size is also the read request size. */
    static const size_t cache_sizes[] = {1, 2, 7, 16, 256, 4096};
    static const size_t out_chunks[] = {1, 13, 256, SIZE_MAX};

    for (size_t i = 0; i < tuz_test_vector_count; i++) {
        const tuz_test_vector_t *v = &tuz_test_vectors[i];
        if (!vector_supported(v)) {
            continue;
        }
        uint8_t *out = malloc(v->expected_size + 1);
        TEST_ASSERT_NOT_NULL(out);
        for (size_t b = 0; b < sizeof(cache_sizes) / sizeof(cache_sizes[0]); b++) {
            for (size_t c = 0; c < sizeof(out_chunks) / sizeof(out_chunks[0]); c++) {
                size_t produced = 0;
                tuz_size_t dict_size = 0;
                /* One spare byte of capacity so the final call can report STREAM_END. */
                tuz_TResult res = stream_decode(v->code, v->code_size, cache_sizes[b], out_chunks[c],
                                                out, v->expected_size + 1, &produced, &dict_size);
                TEST_ASSERT_EQUAL_MESSAGE(tuz_STREAM_END, res, v->name);
                TEST_ASSERT_TRUE_MESSAGE(dict_size >= 1 && dict_size <= v->dict_size, v->name);
                TEST_ASSERT_EQUAL_MESSAGE(v->expected_size, produced, v->name);
                if (produced) {
                    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v->expected, out, produced, v->name);
                }
            }
        }
        free(out);
    }
}

TEST_CASE("truncated streams do not decode completely", "[tinyuz]")
{
    /* A missing tail that happens to be zero bytes can still decode (EOF reads as 0),
     * so only cut deep enough that the full output cannot be produced. */
    for (size_t i = 0; i < tuz_test_vector_count; i++) {
        const tuz_test_vector_t *v = &tuz_test_vectors[i];
        if (!vector_supported(v) || v->expected_size == 0) {
            continue;
        }
        uint8_t *out = malloc(v->expected_size + 1);
        TEST_ASSERT_NOT_NULL(out);
        const size_t cuts[] = {v->code_size / 2, v->code_size * 3 / 4};
        for (size_t c = 0; c < sizeof(cuts) / sizeof(cuts[0]); c++) {
            size_t produced = 0;
            tuz_TResult res = stream_decode(v->code, cuts[c], 64, 256, out, v->expected_size + 1, &produced, NULL);
            TEST_ASSERT_FALSE_MESSAGE(res == tuz_STREAM_END && produced == v->expected_size, v->name);
        }
        free(out);
    }
}

#if CONFIG_TINYUZ_MEM_SAFE_CHECK
/* Corrupted input must never write past the output bound (the dictionary is
 * sized from the stream header, which the caller caps). */
TEST_CASE("corrupted streams stay within the output bound", "[tinyuz]")
{
    uint32_t rng = 0x12345678;
    for (size_t i = 0; i < tuz_test_vector_count; i++) {
        const tuz_test_vector_t *v = &tuz_test_vectors[i];
        if (!vector_supported(v) || v->code_size < 8) {
            continue;
        }
        const size_t cap = v->expected_size + 64;
        uint8_t *code = malloc(v->code_size);
        uint8_t *out = malloc(cap + GUARD_SIZE);
        TEST_ASSERT_NOT_NULL(code);
        TEST_ASSERT_NOT_NULL(out);
        for (int trial = 0; trial < 64; trial++) {
            memcpy(code, v->code, v->code_size);
            for (int flips = 0; flips < 4; flips++) {
                rng = rng * 1664525u + 1013904223u;
                /* Leave the 4-byte dict size header alone so most trials reach the decoder. */
                size_t pos = 4 + (rng >> 8) % (v->code_size - 4);
                code[pos] ^= (uint8_t)(1u << (rng & 7));
            }

            memset(out, GUARD_BYTE, cap + GUARD_SIZE);
            size_t produced = 0;
            (void)stream_decode(code, v->code_size, 32, 97, out, cap, &produced, NULL);
            TEST_ASSERT_TRUE(produced <= cap);

            tuz_size_t size = (tuz_size_t)cap;
            (void)tuz_decompress_mem(code, v->code_size, out, &size);
            TEST_ASSERT_TRUE(size <= cap);
            for (size_t g = 0; g < GUARD_SIZE; g++) {
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(GUARD_BYTE, out[cap + g], v->name);
            }
        }
        free(code);
        free(out);
    }
}
#endif

TEST_CASE("zero dictionary size header is an error", "[tinyuz]")
{
    static const uint8_t code[] = {0x00, 0x00, 0x00, 0x00, 0x0c, 0x00};
    mem_reader_t reader = {.data = code, .size = sizeof(code), .pos = 0};
    TEST_ASSERT_EQUAL(0, tuz_TStream_read_dict_size(&reader, mem_read));
}
