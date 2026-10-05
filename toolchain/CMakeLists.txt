cmake_minimum_required(VERSION 3.28)
project(brad-devices VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BRAD_BUILD_DRIVERS "Build reference driver implementations" ON)
option(BRAD_BUILD_EMU "Build hardware emulation layer" OFF)
option(BRAD_BUILD_CVT "Build Brad-CVT toolchain" OFF)
option(BRAD_BUILD_BVECTOR "Build BradVector platform (bradc/BVRT/tools)" ON)
option(BRAD_BUILD_BVLIB "Build BVML/BVN/BVComm reference libraries" OFF)
option(BRAD_BUILD_TESTS "Build tests" ON)

enable_testing()

add_library(brad-hw INTERFACE)
target_include_directories(brad-hw INTERFACE include)

if(BRAD_BUILD_DRIVERS)
    add_subdirectory(drivers)
endif()

if(BRAD_BUILD_EMU)
    add_subdirectory(emu)
endif()

if(BRAD_BUILD_CVT)
    add_subdirectory(brad-cvt)
endif()

if(BRAD_BUILD_BVECTOR)
    add_subdirectory(bradvector)
endif()

if(BRAD_BUILD_BVLIB AND BRAD_BUILD_BVECTOR)
    add_subdirectory(bvlibs)
endif()

if(BRAD_BUILD_BVECTOR)
    add_subdirectory(braddev)
endif()

if(BRAD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
