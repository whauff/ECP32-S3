cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS managed_components/lvgl__lvgl)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(board_bringup)
