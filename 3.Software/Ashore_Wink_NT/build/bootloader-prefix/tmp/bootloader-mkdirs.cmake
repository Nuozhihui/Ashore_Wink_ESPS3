# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-master/components/bootloader/subproject"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix/tmp"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix/src"
  "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/JINCHANG/Desktop/ESP/ESP_EXE/EX/My_Example/LCD/i80_controller_v3.0/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
