# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-master/components/bootloader/subproject"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix/tmp"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix/src"
  "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/JINCHANG/Desktop/Project/0.ESPS3/Ashore_Wink_ESPS3/sample_project/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
