project(MooncakeConductor)

# Add include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
    ${CMAKE_CURRENT_SOURCE_DIR}/include/conductor-service/
)

# Add subdirectories
add_subdirectory(src)

# if (BUILD_UNIT_TESTS)
#     add_subdirectory(tests)
# endif()