set(MOONCAKE_CONDUCTOR_SOURCES
    conductor_proxy.cpp
    conductor-service/request_handler.cpp
)

add_library(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})
target_link_libraries(mooncake_store PUBLIC glog::glog gflags::gflags)

# conductor_proxy binary 当前编二进制有问题
# add_executable(mooncake_conductor conductor_proxy.cpp)

# install(TARGETS mooncake_master DESTINATION bin)