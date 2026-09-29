idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES esp_tinyuz
    EMBED_FILES "data/message.txt" "data/message.txt.tuz"
)
