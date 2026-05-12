cmake_minimum_required(VERSION 3.20)
project(mini_db CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)

add_executable(mini_db
    src/main.cpp
    src/storage/disk_manager.cpp
    src/storage/buffer_pool_manager.cpp
    src/storage/table_page.cpp
    src/storage/table_heap.cpp
    src/index/b_plus_tree_leaf.cpp
    src/index/b_plus_tree_internal.cpp
    src/index/b_plus_tree.cpp
    src/recovery/log_record.cpp
    src/recovery/log_manager.cpp
    src/recovery/recovery.cpp
)
