function(target_strict_compilation TARGET)
    set(WSHADOW_WARN_OPT "-Wshadow")
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(WSHADOW_WARN_OPT "-Wshadow-all")
    endif()

    target_compile_options(${TARGET} PRIVATE
        "-Wall"
        "-Wextra"
        "-pedantic-errors"
        ${WSHADOW_WARN_OPT}
        "-Werror"
    )

    set_property(TARGET ${TARGET} PROPERTY LINK_LIBRARIES_ONLY_TARGETS ON)
endfunction()
