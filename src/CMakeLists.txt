project(rgltf C)
set(PROJECT_VERSION 0.1.0)
set(API_VERSION 100)

set(rgltf_public_headers
        rgltf.h
        )

# Sources to be compiled
set(rgltf_sources
        rgltf.c
        )

add_library(rgltf ${rgltf_sources} ${rgltf_public_headers})

set_target_properties(rgltf PROPERTIES
        PUBLIC_HEADER "${rgltf_public_headers}"
        VERSION ${PROJECT_VERSION}
        SOVERSION ${API_VERSION}
        )

file(COPY ${rgltf_public_headers} DESTINATION "include")




