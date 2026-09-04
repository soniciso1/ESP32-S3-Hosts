# Top-level project file. The board is chosen at configure time with -DBOARD=s3|esp32
# (see main/CMakeLists.txt), which only affects the partition table and PSRAM options;
# the firmware source is identical across all four boards.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(manuals-ap)
