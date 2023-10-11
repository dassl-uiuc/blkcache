add_dependency(argparse https://github.com/p-ranav/argparse 0b51382 TRUE)
add_dependency(spdlog https://github.com/gabime/spdlog v1.11.0 TRUE)

set(CMAKE_C_FLAGS_DEBUG "-g -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

add_executable(writer writer.cpp)
target_link_libraries(writer PRIVATE block_cache argparse spdlog)

add_executable(reader reader.cpp)
target_link_libraries(reader PRIVATE block_cache argparse spdlog)
