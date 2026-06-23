# ESP-IDF project CMakeLists.txt for can_bridge
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Point to shared components
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../components)

idf_build_set_property(MINIMAL_BUILD ON)
project(can_bridge)
