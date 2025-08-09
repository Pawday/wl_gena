cmake_minimum_required(VERSION 3.28)

project("${PREF}wl_gena")

option(${PREF}WL_GENA_FIND_PACKAGE_EXPAT "Use find_package for libexpat" ON)
option(${PREF}WL_GENA_BUILD_LIBS "Build wl_gena libraries" ON)
option(${PREF}WL_GENA_BUILD_EXEC "Build wl_gena executable" ON)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(target_cxx23)
include(target_strict_compilation)

if(NOT TARGET ${PREF}libexpat AND ${${PREF}WL_GENA_FIND_PACKAGE_EXPAT})
    if(TARGET ${PREF}libexpat.headers)
        message(FATAL_ERROR "Target [${PREF}libexpat.headers] exist but [${PREF}libexpat] DOES NOT")
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

if(NOT TARGET ${PREF}wl_gena.object)
    add_library(${PREF}wl_gena.object OBJECT)
endif()
target_cxx23(${PREF}wl_gena.object)
target_strict_compilation(${PREF}wl_gena.object)

if(NOT TARGET ${PREF}wl_gena.PIC_object)
    add_library(${PREF}wl_gena.PIC_object OBJECT)
endif()
set_target_properties(${PREF}wl_gena.PIC_object PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_cxx23(${PREF}wl_gena.PIC_object)
target_strict_compilation(${PREF}wl_gena.PIC_object)

list(APPEND SOURCES
    GenaMain.cc
    Parser.cc
)

target_sources(${PREF}wl_gena.object PRIVATE ${SOURCES})
target_sources(${PREF}wl_gena.PIC_object PRIVATE ${SOURCES})

list(APPEND PRIVATE_HEADER_LIBS
    ${PREF}libexpat.headers
    ${PREF}wl_gena.headers
)

target_link_libraries(${PREF}wl_gena.object PRIVATE ${PRIVATE_HEADER_LIBS})
target_link_libraries(${PREF}wl_gena.PIC_object PRIVATE ${PRIVATE_HEADER_LIBS})

if(${PREF}WL_GENA_BUILD_LIBS)
    if(NOT TARGET ${PREF}wl_gena.static)
        add_library(${PREF}wl_gena.static STATIC $<TARGET_OBJECTS:${PREF}wl_gena.object>)
    endif()

    if(NOT TARGET ${PREF}wl_gena.shared)
        add_library(${PREF}wl_gena.shared SHARED $<TARGET_OBJECTS:${PREF}wl_gena.PIC_object>)
    endif()
endif()

if(${PREF}WL_GENA_BUILD_EXEC)
    if(NOT TARGET ${PREF}wl_gena)
        add_executable(${PREF}wl_gena)
    endif()

    target_cxx23(${PREF}wl_gena)
    target_strict_compilation(${PREF}wl_gena)

    target_sources(${PREF}wl_gena PRIVATE Main.cc)
    target_link_libraries(${PREF}wl_gena PRIVATE
        ${PREF}wl_gena.headers
        ${PREF}wl_gena.object
        ${PREF}libexpat
    )
endif()

include(cleanup_collisions)
