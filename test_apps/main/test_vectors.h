#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *code; /* tinyuz stream, starting with the 4-byte dict size */
    size_t code_size;
    const uint8_t *expected;
    size_t expected_size;
    size_t dict_size;  /* dictionary size requested from the encoder (upper bound) */
    bool literal_line; /* encoded with literal-line control codes allowed */
} tuz_test_vector_t;

extern const tuz_test_vector_t tuz_test_vectors[];
extern const size_t tuz_test_vector_count;
