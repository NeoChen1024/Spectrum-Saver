cmake_minimum_required(VERSION 3.16)
project(SpectrumSaver LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED COMPONENTS CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MagickPP REQUIRED IMPORTED_TARGET Magick++)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/crc32c/CMakeLists.txt" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/tinycolormap/include/tinycolormap.hpp")
  message(FATAL_ERROR "Missing submodules; run git submodule update --init --recursive")
endif()

# The CRC32C submodule supplies its own architecture checks and generated config.
# Its optional tests and benchmarks need additional upstream submodules.
set(CRC32C_BUILD_TESTS OFF)
set(CRC32C_BUILD_BENCHMARKS OFF)
set(CRC32C_USE_GLOG OFF)
set(CRC32C_INSTALL OFF)
add_subdirectory(contrib/crc32c)

# tinycolormap is header-only. Its old CMake minimum is incompatible with CMake 4.
add_library(tinycolormap INTERFACE)
target_include_directories(tinycolormap INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/contrib/tinycolormap/include")

add_library(spectrum_common STATIC common.cpp log_io.cpp)
target_compile_features(spectrum_common PUBLIC cxx_std_20)
target_include_directories(spectrum_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(spectrum_common PUBLIC crc32c Threads::Threads)

add_library(spectrum_serial STATIC serial_protocol.cpp)
target_compile_features(spectrum_serial PUBLIC cxx_std_20)
target_include_directories(spectrum_serial PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(target IN ITEMS spectrum_common spectrum_serial)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

add_executable(spsave spsave.cpp)
target_link_libraries(spsave PRIVATE spectrum_common spectrum_serial)

add_executable(splogconvert splogconvert.cpp)
target_link_libraries(splogconvert PRIVATE spectrum_common)

add_executable(log2png log2png.cpp)
target_link_libraries(log2png PRIVATE spectrum_common tinycolormap
  PkgConfig::MagickPP OpenMP::OpenMP_CXX)

foreach(target IN ITEMS spsave splogconvert log2png)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

install(TARGETS spsave splogconvert log2png RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
  add_executable(spectrum_tests tests/test_main.cpp)
  target_link_libraries(spectrum_tests PRIVATE spectrum_common spectrum_serial)
  target_compile_options(spectrum_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME spectrum_tests COMMAND spectrum_tests)
endif()
