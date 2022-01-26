cmake_minimum_required(VERSION 3.21)

project(nirah LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 20)

# From https://github.com/GPUOpen-Drivers/xgl/blob/dev/icd/make/importdefs
set(PAL_CLIENT_INTERFACE_MAJOR_VERSION 697 CACHE PATH "${PROJECT_NAME} override" FORCE)
# From https://github.com/GPUOpen-Drivers/xgl/blob/89a4cf115b9a2972ff00e2fe4959f4f1c8e7175b/cmake/XglOverrides.cmake#L141
set(PAL_CLIENT "VULKAN" CACHE STRING "${PROJECT_NAME} override" FORCE)

add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/MetroHash")
add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/CWPack")
add_subdirectory("${CMAKE_SOURCE_DIR}/subprojects/pal")

set(NIRAH_SOURCES "${CMAKE_SOURCE_DIR}/src/main.cpp")
add_executable(nirah ${NIRAH_SOURCES})
target_link_libraries(nirah pal)
