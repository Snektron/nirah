cmake_minimum_required(VERSION 3.21)

## General setup
project(nirah LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-fdiagnostics-color=always)

find_package(fmt)

## PAL & friends
# From https://github.com/GPUOpen-Drivers/xgl/blob/dev/icd/make/importdefs
set(PAL_CLIENT_INTERFACE_MAJOR_VERSION 697 CACHE PATH "${PROJECT_NAME} override" FORCE)
# From https://github.com/GPUOpen-Drivers/xgl/blob/89a4cf115b9a2972ff00e2fe4959f4f1c8e7175b/cmake/XglOverrides.cmake#L141
set(PAL_CLIENT "VULKAN" CACHE STRING "${PROJECT_NAME} override" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/MetroHash")
add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/CWPack")
add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/pal")

## Generate pipeline binary
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/test.elf"
    COMMAND amdllpc "${CMAKE_SOURCE_DIR}/shaders/test.comp" -auto-layout-desc -gfxip=8.0.3 -o "${CMAKE_BINARY_DIR}/test.elf"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test.comp"
    COMMENT "Compiling shader"
)

add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/test.elf.o"
    COMMAND ${CMAKE_LINKER} --relocatable -m elf_x86_64 --format binary --output "${CMAKE_BINARY_DIR}/test.elf.o" "test.elf"
    DEPENDS "${CMAKE_BINARY_DIR}/test.elf"
)

add_custom_target(
    nirah-pipeline
    DEPENDS "${CMAKE_BINARY_DIR}/test.elf.o"
)

## Final executable
set(NIRAH_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")
add_executable(nirah ${NIRAH_SOURCES})
target_link_libraries(nirah pal fmt::fmt)
target_link_libraries(nirah "${CMAKE_BINARY_DIR}/test.elf.o")
