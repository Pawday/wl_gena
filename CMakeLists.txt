cmake_minimum_required(VERSION 3.28)

project("${PREF}wl_gena")

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(${PREF}WL_GENA_FIND_PACKAGE_EXPAT "Use find_package for libexpat" ON)
option(${PREF}WL_GENA_LIB_ONLY "Do not build wl_gena executable" OFF)

if(NOT TARGET ${PREF}libexpat AND ${${PREF}WL_GENA_FIND_PACKAGE_EXPAT})
    if(TARGET ${PREF}libexpat.headers)
        message(FATAL_ERROR "Target ${PREF}libexpat.headers exist but [${PREF}libexpat] DOES NOT")
    endif()

    find_package(expat REQUIRED)
    get_property(EXPAT_INCLUDES TARGET expat::expat PROPERTY INTERFACE_INCLUDE_DIRECTORIES)

    add_library(${PREF}libexpat.headers INTERFACE)
    target_include_directories(${PREF}libexpat.headers INTERFACE ${EXPAT_INCLUDES})

    add_library(${PREF}libexpat ALIAS expat::expat)
endif()

if(NOT TARGET ${PREF}wl_gena.headers)
    add_library(${PREF}wl_gena.headers INTERFACE)
endif()
target_include_directories(${PREF}wl_gena.headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)


if(NOT TARGET ${PREF}wl_gena.static)
    add_library(${PREF}wl_gena.static STATIC)
    set(TARGET_TO_23 "${PREF}wl_gena.static")
    include(target_cxx23)
endif()

set(TARGET_TO_STRICTIFY ${PREF}wl_gena.static)
include(target_strict_compilation)
target_sources(${PREF}wl_gena.static PRIVATE
    GenaMain.cc
    Parser.cc
)

target_link_libraries(${PREF}wl_gena.static PRIVATE
    ${PREF}libexpat.headers
    ${PREF}wl_gena.headers
)

if(${${PREF}WL_GENA_LIB_ONLY})
    return()
endif()

if(NOT TARGET ${PREF}wl_gena)
    add_executable(${PREF}wl_gena)
    set(TARGET_TO_23 "${PREF}wl_gena")
    include(target_cxx23)
endif()

set(TARGET_TO_STRICTIFY ${PREF}wl_gena)
include(target_strict_compilation)

target_sources(${PREF}wl_gena PRIVATE Main.cc)
target_link_libraries(${PREF}wl_gena PRIVATE
    ${PREF}wl_gena.headers
    ${PREF}wl_gena.static
    ${PREF}libexpat
)
