project(MooncakeConductor)

find_package(OpenSSL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(Boost REQUIRED COMPONENTS python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR})

file(GLOB_RECURSE MOONCAKE_CONDUCTOR_SOURCES
    "src/*.cpp"
    "src/*.cxx"
    "src/*.cc"
    "test/*.cpp"
)

file(GLOB_RECURSE ALL_HEADER_DIRS 
    LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*
)
set(INCLUDE_DIRS "")
foreach(dir ${ALL_HEADER_DIRS})
    if(IS_DIRECTORY ${dir})
        list(APPEND INCLUDE_DIRS ${dir})
    endif()
endforeach()
list(REMOVE_DUPLICATES INCLUDE_DIRS)

# 获取项目根目录 (Mooncake/)
set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparties)

# 验证 nlohmann/json.hpp 是否存在
if(NOT EXISTS ${THIRDPARTY_DIR}/nlohmann/json.hpp)
    message(WARNING "nlohmann/json.hpp not found at ${THIRDPARTY_DIR}/nlohmann/json.hpp")
    message(WARNING "Please ensure thirdparty directory structure is correct:")
    message(WARNING "  Mooncake/")
    message(WARNING "  ├── thirdparty/")
    message(WARNING "  │   └── nlohmann/")
    message(WARNING "  │       └── json.hpp")
endif()

add_executable(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})

target_include_directories(mooncake_conductor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${THIRDPARTY_DIR}
)

target_include_directories(mooncake_conductor PRIVATE
    ${CMAKE_SOURCE_DIR}/mooncake-store/include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/fake_include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/common
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/hf3fs
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/offset_allocator
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/utils
)


target_link_libraries(mooncake_conductor PRIVATE
    mooncake_store
    pthread
    mooncake_common
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Boost::python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}
    ${Python3_LIBRARIES}
)

install(TARGETS mooncake_conductor DESTINATION bin)


message(STATUS "Found Python3: ${Python3_VERSION}")
message(STATUS "Found Boost: ${Boost_VERSION}")
message(STATUS "Boost Python component: python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR}")