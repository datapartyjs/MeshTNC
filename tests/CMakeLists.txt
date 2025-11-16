cmake_minimum_required(VERSION 3.10)

include(GenerateExportHeader)

project(MeshTNC_Tests)

set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    # warning level 4
    add_compile_options(/W4)
else()
    # additional warnings
    add_compile_options(-Wall -Wextra)
endif()

find_package(Boost 1.8.3 REQUIRED)

## duini library section
add_library(
  duini SHARED
  lib/duini/SerialPort.h
  lib/duini/SerialPort.cpp
)

GENERATE_EXPORT_HEADER(
  duini
  BASE_NAME duini
  EXPORT_MACRO_NAME DUINI_EXPORTS
  EXPORT_FILE_NAME lib/duini_exports.h
  STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC
)

target_include_directories(
  duini PUBLIC lib/duini
  "${CMAKE_CURRENT_BINARY_DIR}/lib"
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(duini ${Boost_LIBRARIES})

## MeshTNC library section
add_library(
  MeshTNC SHARED
	lib/meshtnc/KISS.h
	lib/meshtnc/KISS.cpp
)

GENERATE_EXPORT_HEADER(
  MeshTNC
  BASE_NAME MeshTNC
  EXPORT_MACRO_NAME MESHTNC_EXPORTS
  EXPORT_FILE_NAME lib/meshtnc_exports.h
  STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC
)

target_include_directories(
  MeshTNC PUBLIC lib/meshtnc
  "${CMAKE_CURRENT_BINARY_DIR}/lib"
)

target_link_libraries(MeshTNC)

## Main section
add_executable(
  KISSTest
	src/KISSTest.cpp
)

target_link_libraries(
  KISSTest
	duini
)
