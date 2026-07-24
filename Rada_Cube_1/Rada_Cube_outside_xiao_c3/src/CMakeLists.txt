# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    PRIV_REQUIRES framework-arduinoespressif32 bt nvs_flash esp_wifi esp_pm
)
