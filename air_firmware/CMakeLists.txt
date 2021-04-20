cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS "../components")
set(COMPONENT_REQUIRES "common")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(air_firmware)
