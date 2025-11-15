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

## MeshTNC library section
add_library(
  MeshTNC SHARED
	lib/SerialPort.cpp
	lib/SerialPort.h
)

GENERATE_EXPORT_HEADER(
  MeshTNC
  BASE_NAME MeshTNC
  EXPORT_MACRO_NAME MESHTNC_EXPORTS
  EXPORT_FILE_NAME lib/meshtnc_exports.h
  STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC
)

target_include_directories(
  MeshTNC PUBLIC lib/ "${CMAKE_CURRENT_BINARY_DIR}/lib"
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries(MeshTNC ${Boost_LIBRARIES})

## Main section
add_executable(
  KISSTest
	src/KISSTest.cpp
)

target_link_libraries(
  KISSTest
	MeshTNC
)
