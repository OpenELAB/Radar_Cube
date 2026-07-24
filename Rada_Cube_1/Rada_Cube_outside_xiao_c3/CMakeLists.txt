cmake_minimum_required(VERSION 3.16.0)

# This project only uses components shipped with the installed Arduino core
# and ESP-IDF. Disable registry dependency resolution so Arduino's broad
# idf_component.yml does not populate a project-local managed_components tree.
set(ENV{IDF_COMPONENT_MANAGER} 0)

# PlatformIO exposes its application, Arduino framework and generic compile
# environment as these component names. Listing them makes the mixed-framework
# entry points explicit; their transitive ESP-IDF dependencies remain automatic.
set(COMPONENTS src framework-arduinoespressif32 __pio_env)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Rada_Cube_outside_xiao_c3)
