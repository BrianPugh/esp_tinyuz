cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component must build warning-free.
idf_build_set_property(COMPILE_OPTIONS "-Werror" APPEND)

project(test_esp_tinyuz)
