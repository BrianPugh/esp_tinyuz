idf_component_register(
    SRCS "tinyuz/decompress/tuz_dec.c"
    INCLUDE_DIRS "tinyuz/decompress"
)

# Upstream's compile-time switches, applied as definitions so tinyuz/ stays unmodified.
# tuz_isNeedLiteralLine changes the tuz_TStream layout, so users of tuz_dec.h must see it too.
if(CONFIG_TINYUZ_LITERAL_LINE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC "tuz_isNeedLiteralLine=1")
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC "tuz_isNeedLiteralLine=0")
endif()

# Only tuz_dec.c reads _IS_RUN_MEM_SAFE_CHECK. Keep it private: other libraries use the same
# generic macro name for their own checks, so it must not leak into consuming components.
if(CONFIG_TINYUZ_MEM_SAFE_CHECK)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "_IS_RUN_MEM_SAFE_CHECK=1")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE "_IS_RUN_MEM_SAFE_CHECK=0")
endif()

# tuz_TStream_read_dict_size() asserts that the dictionary size read from the stream is
# non-zero before returning 0 for it, so with assertions enabled (the ESP-IDF default) a
# corrupt header would abort instead of reporting an error. Upstream's other asserts only
# check caller arguments, so compile the decoder without them.
set_source_files_properties("tinyuz/decompress/tuz_dec.c" PROPERTIES COMPILE_DEFINITIONS "NDEBUG")
