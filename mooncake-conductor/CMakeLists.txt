project(MooncakeConductor)

file(GLOB_RECURSE MOONCAKE_CONDUCTOR_SOURCES
    "src/*.cpp"
    "src/*.cxx"
    "src/*.cc"
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


add_executable(mooncake_conductor ${MOONCAKE_CONDUCTOR_SOURCES})

target_include_directories(mooncake_conductor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${INCLUDE_DIRS}
)

target_include_directories(mooncake_conductor PRIVATE
    ${CMAKE_SOURCE_DIR}/mooncake-store/include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/fake_include
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/cachelib_memory_allocator/common
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/hf3fs
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/offset_allocator
    ${CMAKE_SOURCE_DIR}/mooncake-store/include/utils
)


target_link_libraries(mooncake_conductor PRIVATE
    mooncake_store
    pthread
    mooncake_common
)

install(TARGETS mooncake_conductor DESTINATION bin)

