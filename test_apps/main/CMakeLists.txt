idf_component_register(
    SRCS "test_main.c" "test_tinyuz.c" "test_vectors.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES unity
    WHOLE_ARCHIVE
)
