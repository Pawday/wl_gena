cmake_minimum_required(VERSION 3.28)

project("${PREF}wl_gena")

option(${PREF}WL_GENA_FIND_PACKAGE_EXPAT "Use find_package for libexpat" ON)

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

function(target_cxx23 tgt)
    set_property(TARGET ${tgt} PROPERTY CXX_STANDARD 23)
    set_property(TARGET ${tgt} PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET ${tgt} PROPERTY CXX_EXTENSIONS ON)
endfunction()


if(NOT TARGET ${PREF}wl_gena.headers)
    add_library(${PREF}wl_gena.headers INTERFACE)
endif()
target_include_directories(${PREF}wl_gena.headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)


if(NOT TARGET ${PREF}wl_gena.static)
    add_library(${PREF}wl_gena.static STATIC)
    target_cxx23(${PREF}wl_gena.static)
endif()

target_sources(${PREF}wl_gena.static PRIVATE
    GenaMain.cc
    Parser.cc
)

target_link_libraries(${PREF}wl_gena.static PRIVATE
    ${PREF}libexpat.headers
    ${PREF}wl_gena.headers
)

if(NOT TARGET ${PREF}wl_gena)
    add_executable(${PREF}wl_gena)
    target_cxx23(${PREF}wl_gena)
endif()

target_sources(${PREF}wl_gena PRIVATE Main.cc)
target_link_libraries(${PREF}wl_gena PRIVATE
    ${PREF}wl_gena.headers
    ${PREF}wl_gena.static
    ${PREF}libexpat
)
