set(MOONCAKE_CONDUCTOR_SOURCES
    conductor_proxy.cpp
    request_handler.cpp
    cli_parse.cpp
)

include_directories("include")
include_directories("include/conductor-service")


# conductor_proxy binary
add_executable(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})
target_link_libraries(mooncake_conductor PRIVATE
    mooncake_store
    pthread
    mooncake_common
)
install(TARGETS mooncake_conductor DESTINATION bin)