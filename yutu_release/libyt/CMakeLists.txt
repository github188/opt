include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/sign/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/hmacsha/include")


file(GLOB_RECURSE SRC "*.[ch]pp")
file(GLOB_RECURSE HEADERS "*.h")

add_library(yt STATIC ${SRC} ${HEADERS})
add_library(yt SHARED ${SRC} ${HEADERS})

set_target_properties(yuntu PROPERTIES LINK_FLAGS "-static-libgcc -static-libstdc++ -Wl,--no-undefined")
