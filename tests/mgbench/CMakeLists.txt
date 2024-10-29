set(test_prefix memgraph__mgbench__)

add_executable(${test_prefix}client client.cpp)

add_library(hiredis STATIC IMPORTED)
set_target_properties(hiredis PROPERTIES
    IMPORTED_LOCATION "/home/barak_bar/hiredis/libhiredis.a"
    INTERFACE_INCLUDE_DIRECTORIES "/home/barak_bar/hiredis"
)
#FetchContent_Declare(
#    hiredis
#    GIT_REPOSITORY https://github.com/redis/hiredis
#    GIT_TAG v1.1.0
#    SOURCE_DIR hiredis  # This is important
#)
# FetchContent_MakeAvailable(hiredis)
#message(STATUS "Hiredis source dir: ${hiredis_SOURCE_DIR}")
#message(STATUS "Hiredis binary dir: ${hiredis_BINARY_DIR}")

target_include_directories(${test_prefix}client PRIVATE "/home/barak_bar/hiredis")

#add_executable(${test_prefix}client client.cpp)
set_target_properties(${test_prefix}client PROPERTIES OUTPUT_NAME client)
target_link_libraries(${test_prefix}client mg-communication json hiredis)
