# decode_blob

Decodes a tinyuz-compressed text embedded in the firmware, feeding the decoder
through a read callback (as a network or BLE transport would) in 48-byte output
chunks, prints the text and checks it against the embedded original.

```sh
idf.py set-target esp32s3
idf.py build flash monitor
```

`main/data/message.txt.tuz` was made with the upstream encoder:
`tinyuz -c-4k message.txt message.txt.tuz` (see `tools/gen_vectors.py`).
