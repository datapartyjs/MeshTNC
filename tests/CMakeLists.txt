cmake_minimum_required(VERSION 3.10)
project(MeshTNC_Tests)

###############################################################################
## file globbing ##############################################################
###############################################################################

# these instructions search the directory tree when CMake is
# invoked and put all files that match the pattern in the variables
# `sources` and `data`
file(GLOB_RECURSE sources      src/*.cpp src/*.h)
file(GLOB_RECURSE data "")
# you can use set(sources src/main.cpp) etc if you don't want to
# use globbing to find files automatically

find_package(Boost 1.8.3 REQUIRED)

###############################################################################
## target definitions #########################################################
###############################################################################

# add the data to the target, so it becomes visible in some IDE
add_executable(KISSTest ${sources} ${data})

# just for example add some compiler flags
target_compile_options(KISSTest PUBLIC -std=c++1y -Wall)

# this lets me include files relative to the root source directory with a <> pair
target_include_directories(KISSTest PUBLIC src/ ${Boost_INCLUDE_DIRS})

target_link_libraries(KISSTest ${Boost_LIBRARIES})

# this copies all resource files in the build directory
# we need this, because we want to work with paths relative to the executable
file(COPY ${data} DESTINATION resources)