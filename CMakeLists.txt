cmake_minimum_required(VERSION 3.28)

project("${PREF}wl_gena")



if(NOT TARGET ${PREF}libexpat)
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

add_library(${PREF}WaylandScanner.static STATIC)
target_cxx23(${PREF}WaylandScanner.static)

target_sources(${PREF}WaylandScanner.static PRIVATE
    WaylandScanner.cc
    WaylandProtoParser.cc
)

target_link_libraries(${PREF}WaylandScanner.static
    PRIVATE
        ${PREF}libexpat.headers
)

if(NOT TARGET)
    add_executable(${PREF}WaylandScanner)
    target_cxx23(${PREF}WaylandScanner)
endif()

target_sources(${PREF}WaylandScanner PRIVATE Main.cc)
target_link_libraries(${PREF}WaylandScanner PRIVATE 
    ${PREF}WaylandScanner.static
    ${PREF}libexpat
)
