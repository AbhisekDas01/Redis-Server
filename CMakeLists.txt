cmake_minimum_required(VERSION 3.10)
project(RedisServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Server target
add_executable(server
    server.cpp
    helpers/helper.cpp
    hashtable/hashtable.cpp
    zset/zset.cpp
    AVLtrees/avl.cpp
)

# Client target
add_executable(client
    client.cpp
)
