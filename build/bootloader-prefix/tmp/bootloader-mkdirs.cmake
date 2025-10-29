# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/chris/esp/v5.5/esp-idf/components/bootloader/subproject"
  "/home/chris/TIC_III/proyecto1/build/bootloader"
  "/home/chris/TIC_III/proyecto1/build/bootloader-prefix"
  "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/tmp"
  "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/src/bootloader-stamp"
  "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/src"
  "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/chris/TIC_III/proyecto1/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
