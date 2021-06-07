# Caller who includes this CMakeLists.txt is responsible for providing also the following dependencies:
#   - icystdlib
#   - libpsdisc

file(GLOB_RECURSE SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "src/*.cpp"
    "src/*.h"
)

add_library(hlebios ${SOURCES})
target_include_directories(hlebios PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(hlebios PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_link_libraries(hlebios LINK_PUBLIC libpsdisc icystdlib)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
    add_definitions(/FI"fi-platform-defines.h" /FI"fi-printf-redirect.h")
else()
    add_definitions(-include fi-platform-defines.h -include fi-printf-redirect.h)
endif()
