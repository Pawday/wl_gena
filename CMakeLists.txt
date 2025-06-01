application_sources(${PREF}WaylandScanner PRIVATE 
    WaylandScanner.cc
    WaylandProtoParser.cc
)

application_link_libraries(${PREF}WaylandScanner
    PRIVATE
        ${PREF}expat.headers
    INTERFACE
        ${PREF}expat.static
)
