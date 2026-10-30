# Yeeps mod SDK. Builds the shared BNM + SDK library once, then every mod folder.
cmake_minimum_required(VERSION 3.18...3.31)
project(yeeps_mods CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

file(GLOB BNM_SOURCES sdk/bnm/src/*.cpp)
add_library(ymsdk STATIC ${BNM_SOURCES} sdk/src/ymsdk.cpp)
target_include_directories(ymsdk
    PUBLIC sdk/include sdk/bnm/include sdk/bnm/external/include
    PRIVATE sdk/bnm/external sdk/bnm/external/utf8 sdk/bnm/src/private)
target_compile_options(ymsdk PRIVATE -w)
target_link_libraries(ymsdk PUBLIC log)

# add_yeeps_mod(<name> <sources...>) -> out/<name>.so
function(add_yeeps_mod name)
    add_library(${name} SHARED ${ARGN})
    target_link_libraries(${name} PRIVATE ymsdk)
    target_link_options(${name} PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL -s)
    set_target_properties(${name} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/out)
endfunction()

file(GLOB MOD_DIRS LIST_DIRECTORIES true mods/*)
foreach(dir ${MOD_DIRS})
    if(EXISTS ${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
