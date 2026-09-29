# esp_tinyuz

ESP-IDF component for the decoder of [tinyuz](https://github.com/sisong/tinyuz), a tiny LZ77-family decompressor by housisong. It decodes from a read callback into caller-sized output chunks, so RAM use is fixed by the stream's dictionary size plus a cache you choose, however large the data is.

The component builds upstream's `decompress/` sources unmodified from a git submodule pinned at tinyuz **v1.1.1** (`1d74ffa`) and exposes upstream's own API (`tuz_dec.h`). The encoder is C++ and runs on the host; it isn't included.

## Install

```sh
idf.py add-dependency "brianpugh/esp_tinyuz^1.0.0"
```

Then add `esp_tinyuz` to your component's `REQUIRES`/`PRIV_REQUIRES` and `#include "tuz_dec.h"`.

The component is plain C with no hardware dependencies, so it builds for every ESP-IDF target (including `linux`) on ESP-IDF 5.0 and newer.

## Streaming decode

A tinyuz stream starts with its dictionary size (4 bytes, little-endian). Read it first, decide whether it fits your memory budget, then give the decoder one buffer holding the dictionary followed by the input cache:

```c
#include "tuz_dec.h"

#define MAX_DICT_SIZE (4 * 1024) // largest dictionary this device accepts
#define CACHE_SIZE    256        // input cache; any size > 0, bigger is faster

// Fill *size bytes. Returning fewer means the input ended; return tuz_FALSE on error.
static tuz_BOOL read_input(tuz_TInputStreamHandle handle, tuz_byte *out, tuz_size_t *size);

esp_err_t decode(void *input)
{
    tuz_size_t dict_size = tuz_TStream_read_dict_size(input, read_input);
    if (dict_size == 0 || dict_size > MAX_DICT_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    tuz_byte *mem = malloc(dict_size + CACHE_SIZE);
    if (!mem) {
        return ESP_ERR_NO_MEM;
    }

    tuz_TStream stream;
    tuz_TResult res = tuz_TStream_open(&stream, input, read_input, mem, dict_size, CACHE_SIZE);
    uint8_t out[128];
    while (res == tuz_OK) {
        tuz_size_t n = sizeof(out);
        res = tuz_TStream_decompress_partial(&stream, out, &n);
        if (res == tuz_OK || res == tuz_STREAM_END) {
            consume(out, n); // n == sizeof(out) on tuz_OK, the remainder on tuz_STREAM_END
        }
    }
    free(mem);
    return res == tuz_STREAM_END ? ESP_OK : ESP_FAIL;
}
```

Decoder RAM is `sizeof(tuz_TStream)` (about 60 bytes on the 32-bit targets) + `dict_size` + `CACHE_SIZE` + your output buffer. The encoder shrinks the dictionary to the input size when the input is smaller, so `dict_size` never exceeds the uncompressed size.

Notes:

- The read callback must fill the whole request unless the input has ended: tinyuz treats a short read as end of stream (and `tuz_TStream_read_dict_size` fails on one). Block in the callback until the transport delivers enough bytes.
- `tuz_TStream_open` doesn't read anything; `dict_and_cache` must stay alive until you finish decoding.
- `tuz_decompress_mem(code, code_size, out, &out_size)` decodes a whole in-memory stream into a buffer that also serves as the dictionary, with no extra RAM.
- tinyuz carries no checksum. Verify the decoded data yourself (e.g. a SHA-256 of the result) when integrity matters.

See [`examples/decode_blob`](examples/decode_blob) for a complete program.

## Compressing

On the host, use the upstream `tinyuz` CLI (`tinyuz -c-4k in out`, or `-ci-4k` without literal lines) or [hdiffpatch](https://pypi.org/project/hdiffpatch/)'s `TuzConfig(dict_size=..., literal_line=...)`, which compresses the tinyuz payload of HPatchLite diffs. Both produce the stream format this component decodes.

## Configuration

`idf.py menuconfig` → *Component config* → *tinyuz decoder*. Both options default to upstream's defaults.

| Option | Upstream macro | Default | Effect |
| --- | --- | --- | --- |
| `CONFIG_TINYUZ_LITERAL_LINE` | `tuz_isNeedLiteralLine` | `y` | Decode literal-line control codes. Disabling saves ~54 bytes of code but only decodes streams encoded without literal lines (`tinyuz -ci`, `TuzConfig(literal_line=False)`). Changes `tuz_TStream`'s layout, so it's a public compile definition. |
| `CONFIG_TINYUZ_MEM_SAFE_CHECK` | `_IS_RUN_MEM_SAFE_CHECK` | `y` | Validate the input while decoding, so corrupt or malicious data returns an error instead of accessing memory out of bounds. Keep it on for anything received over a network or radio. |

Not exposed, on purpose:

- `tuz_kMaxOfDictSize` sets how many bytes the stream header uses for the dictionary size, so changing it breaks compatibility with the stock encoders (which write 4 bytes). Limit the dictionary at runtime instead, as in the example above.
- `tuz_length_t`/`tuz_size_t` stay `unsigned int`; narrower types cap the dictionary and match lengths at encode time.
- `_IS_USED_SHARE_hpatch_lite_types` needs HPatchLite's headers from the consumer and only saves ~52 bytes.

The component compiles `tuz_dec.c` with `NDEBUG`: upstream asserts that the dictionary size read from the stream is non-zero, which would abort on a corrupt header when ESP-IDF assertions are enabled, instead of returning the documented error (0).

## Versioning

The component has its own semantic version, independent of tinyuz's; the tinyuz release it bundles is stated at the top of this README. Releases use [bump-my-version](https://github.com/callowayproject/bump-my-version), which rewrites `idf_component.yml` and the install line above, commits and tags `v<version>`:

```sh
uvx bump-my-version bump patch   # or minor/major
git push --follow-tags
```

Pushing a `v*` tag runs the upload workflow, which publishes the version from `idf_component.yml`.

## Development

```sh
git clone --recursive https://github.com/BrianPugh/esp_tinyuz
cd esp_tinyuz/test_apps
idf.py --preview set-target linux build && ./build/test_esp_tinyuz.elf   # host run
idf.py set-target esp32s3 build flash monitor                             # on a board
```

The test vectors in `test_apps/main/test_vectors.c` come from `tools/gen_vectors.py` (the upstream CLI for raw streams, hdiffpatch 2.4.0 for HPatchLite tinyuz payloads); see the script's docstring to regenerate them.

## License

MIT. tinyuz is Copyright (c) 2012-2025 housisong, MIT licensed ([`tinyuz/LICENSE`](https://github.com/sisong/tinyuz/blob/master/LICENSE)). All credit for the codec goes to [sisong/tinyuz](https://github.com/sisong/tinyuz).
