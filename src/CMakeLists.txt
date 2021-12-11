cmake_minimum_required(VERSION 3.13)
add_library("sqlr" STATIC "sqlr.cpp")
set_property(TARGET "sqlr" PROPERTY CXX_STANDARD 17)
target_include_directories("sqlr" INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
