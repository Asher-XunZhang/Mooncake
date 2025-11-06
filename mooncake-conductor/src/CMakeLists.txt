set(MOONCAKE_CONDUCTOR_SOURCES
    conductor_proxy.cpp
    conductor-service/request_handler.cpp
)

# add_library(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})
# target_link_libraries(mooncake_store PUBLIC glog::glog gflags::gflags)

# conductor_proxy binary
add_executable(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})
target_link_libraries(mooncake_conductor PRIVATE
    mooncake_store
    pthread
    mooncake_common
)
install(TARGETS mooncake_conductor DESTINATION bin)