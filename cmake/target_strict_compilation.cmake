set(WSHADOW_WARN_OPT "-Wshadow")
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(WSHADOW_WARN_OPT "-Wshadow-all")
endif()

target_compile_options(${TARGET_TO_STRICTIFY} PRIVATE
    "-Wall"
    "-Wextra"
    "-pedantic-errors"
    ${WSHADOW_WARN_OPT}
    "-Werror"
)

set_property(TARGET ${TARGET_TO_STRICTIFY} PROPERTY LINK_LIBRARIES_ONLY_TARGETS ON)
